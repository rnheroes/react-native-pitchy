#import "Pitchy.h"
#import <AVFoundation/AVFoundation.h>
#import <React/RCTLog.h>
#import "pitch-detector.h"

@implementation Pitchy {
    AVAudioEngine *audioEngine;
    double sampleRate;
    double minVolume;
    double minConfidence;
    BOOL isRecording;
    BOOL isInitialized;
    BOOL hasListeners;
    pitchy::PitchDetector *pitchDetector;
    std::vector<float> accumBuffer; // sliding analysis window (overlap-hop → low latency)
    UInt32 windowSize;
    UInt32 hopSize;
    UInt32 samplesSinceDetect;
    uint64_t totalSamples;   // cumulative samples captured this session (the audio clock)
    double calibWallMs;      // wall-clock ms anchored to calibSample (once, at first buffer)
    uint64_t calibSample;    // sample index that calibWallMs corresponds to
    BOOL calibrated;
    int diagN;               // throttles the backlog diagnostic log
}

RCT_EXPORT_MODULE()

- (instancetype)init {
    self = [super init];
    if (self) {
        pitchDetector = new pitchy::PitchDetector();
    }
    return self;
}

- (void)dealloc {
    if (pitchDetector) {
        pitchDetector->release();
        delete pitchDetector;
        pitchDetector = nullptr;
    }
}

- (NSArray<NSString *> *)supportedEvents {
  return @[@"onPitchDetected"];
}

- (void)startObserving {
    hasListeners = YES;
}

- (void)stopObserving {
    hasListeners = NO;
}

RCT_EXPORT_METHOD(addListener:(NSString *)eventName) {
    [super addListener:eventName];
}

RCT_EXPORT_METHOD(removeListeners:(double)count) {
    [super removeListeners:count];
}

RCT_EXPORT_METHOD(configure:(NSDictionary *)config) {
    if (!isInitialized) {
        @try {
            // Set algorithm
            NSString *algorithm = config[@"algorithm"];
            if (algorithm) {
                pitchDetector->setAlgorithm(std::string([algorithm UTF8String]));
            }

            // Configure audio session
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;
            // AllowBluetoothA2DP exposes the high-quality BT output route (without it
            // DefaultToSpeaker pins playback to the built-in speaker even when BT
            // headphones are connected). NOT AllowBluetooth (HFP) — that would force the
            // whole route to call-quality; the mic stays the built-in one.
            [session setCategory:AVAudioSessionCategoryPlayAndRecord
                            mode:AVAudioSessionModeDefault
                         options:(AVAudioSessionCategoryOptionDefaultToSpeaker |
                                  AVAudioSessionCategoryOptionAllowBluetoothA2DP |
                                  AVAudioSessionCategoryOptionAllowAirPlay)
                           error:&error];
            if (error) {
                RCTLogError(@"Error setting AVAudioSession category: %@", error);
                return;
            }

            // Small hardware IO buffer → the input tap fires frequently → low
            // capture latency. (Real devices honor this; the Simulator's audio
            // HAL caps the callback rate, so the win shows only on device.)
            [session setPreferredIOBufferDuration:0.005 error:nil];

            [session setActive:YES error:&error];
            if (error) {
                RCTLogError(@"Error activating AVAudioSession: %@", error);
                return;
            }

            audioEngine = [[AVAudioEngine alloc] init];
            AVAudioInputNode *inputNode = [audioEngine inputNode];

            AVAudioFormat *format = [inputNode inputFormatForBus:0];
            // The iOS Simulator (and a real device during a transient route
            // hand-off) can expose an input node with a zero sample rate or no
            // channels. installTapOnBus throws for that format. Keep the module
            // uninitialised so the next attempt can retry after the route settles.
            if (format.sampleRate <= 0 || format.channelCount == 0) {
                RCTLogInfo(@"Pitchy input route is not ready yet");
                return;
            }
            sampleRate = format.sampleRate;
            minVolume = [config[@"minVolume"] doubleValue];
            minConfidence = config[@"minConfidence"] ? [config[@"minConfidence"] doubleValue] : 0.0;

            // Analysis window (accuracy) decoupled from hop (latency): detect on
            // the latest `windowSize` samples but re-run every `hopSize` samples
            // (overlap), so the live pitch reflects ~one hop of latency instead of
            // a whole window. Tapping at hopSize makes the callback fire that often.
            windowSize = [config[@"bufferSize"] unsignedIntValue];
            if (windowSize < 256) windowSize = 2048;
            hopSize = windowSize / 4; // ~sampleRate/hop detections/sec → smooth live trace
            samplesSinceDetect = 0;
            accumBuffer.clear();
            accumBuffer.reserve(windowSize * 2);
            totalSamples = 0;
            calibrated = NO;
            diagN = 0;

            [inputNode installTapOnBus:0 bufferSize:hopSize format:format block:^(AVAudioPCMBuffer * _Nonnull buffer, AVAudioTime * _Nonnull when) {
                [self detectPitch:buffer];
            }];

            isInitialized = YES;
        } @catch (NSException *exception) {
            // Configuration failures are recoverable: configure runs again on
            // the next attempt. Keep this informational so clients can present
            // their own recovery UI without a React Native developer LogBox.
            RCTLogInfo(@"Pitchy configure deferred: %@ - %@", exception.name, exception.reason);
        }
    }
}

RCT_EXPORT_METHOD(isRecording:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject) {
    @try {
        // AVAudioEngine may stop itself after an interruption or route change.
        // Report observable engine state so JS lifecycle reconciliation never
        // relies on stale bookkeeping.
        BOOL engineIsRunning = audioEngine != nil && audioEngine.isRunning;
        isRecording = engineIsRunning;
        resolve(@(engineIsRunning));
    } @catch (NSException *exception) {
        reject(@"state_error", [NSString stringWithFormat:@"Failed to inspect recording state: %@", exception.reason], nil);
    }
}

RCT_EXPORT_METHOD(start:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject) {
    @try {
        if (!isInitialized || audioEngine == nil) {
            reject(@"not_initialized", @"Pitchy module is not initialized", nil);
            return;
        }

        // AVAudioEngine can stop itself after a route/configuration change. The
        // engine is the source of truth; never let a stale bookkeeping flag turn
        // a recoverable restart into already_recording.
        if (audioEngine.isRunning) {
            isRecording = YES;
            resolve(@(YES));
            return;
        }
        isRecording = NO;

        // Re-anchor the capture clock for THIS recording session. configure() runs
        // only once (isInitialized guard), so calibration and buffered samples must
        // be reset on EVERY start(). The engine is stopped here, so its input tap
        // cannot race these mutations.
        totalSamples = 0;
        calibSample = 0;
        calibrated = NO;
        samplesSinceDetect = 0;
        accumBuffer.clear();

        // Another audio component may have changed or deactivated the shared
        // session since configure(). Re-activation is cheap when already active
        // and makes stop -> start reliable across route hand-offs.
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *sessionError = nil;
        BOOL sessionActivated = [session setActive:YES error:&sessionError];
        if (!sessionActivated || sessionError) {
            reject(@"audio_session_error", @"Failed to activate audio session", sessionError);
            return;
        }

        [audioEngine prepare];
        NSError *error = nil;
        BOOL didStart = [audioEngine startAndReturnError:&error];
        if (!didStart || error) {
            reject(@"start_error", @"Failed to start audio engine", error);
        } else {
            isRecording = YES;
            resolve(@(YES));
        }
    } @catch (NSException *exception) {
        reject(@"start_error", [NSString stringWithFormat:@"Failed to start: %@", exception.reason], nil);
    }
}

RCT_EXPORT_METHOD(stop:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject) {
    @try {
        // Cleanup is intentionally idempotent. React lifecycles can issue a
        // best-effort stop while the next run performs its own cleanup; both
        // callers must settle successfully instead of racing on isRecording.
        if (audioEngine != nil && audioEngine.isRunning) {
            [audioEngine stop];
        }
        isRecording = NO;
        resolve(@(YES));
    } @catch (NSException *exception) {
        isRecording = audioEngine != nil && audioEngine.isRunning;
        reject(@"stop_error", [NSString stringWithFormat:@"Failed to stop: %@", exception.reason], nil);
    }
}

- (void)detectPitch:(AVAudioPCMBuffer *)buffer {
    if (!hasListeners) return;

    float *channelData = buffer.floatChannelData[0];
    UInt32 frameLength = buffer.frameLength;
    accumBuffer.insert(accumBuffer.end(), channelData, channelData + frameLength);
    totalSamples += frameLength;

    // Anchor the audio-sample clock to wall-clock ONCE, on the first buffer (no
    // processing backlog yet). Every detection's capture time is then derived from
    // its sample index — NOT from when JS receives the event — so if pitch
    // analysis falls behind real-time (CPU spikes on device), the timestamps stay
    // correct instead of drifting ever-later — so a consumer placing samples on a
    // timeline doesn't see them lag cumulatively as recording progresses.
    double nowWallMs = [[NSDate date] timeIntervalSince1970] * 1000.0;
    if (!calibrated) {
        calibWallMs = nowWallMs;
        calibSample = totalSamples;
        calibrated = YES;
    }

    if (accumBuffer.size() < windowSize) return; // window not full yet
    size_t total = accumBuffer.size();
    samplesSinceDetect += frameLength;

    // Overlap-hop: emit a detection every `hopSize` samples on a sliding
    // `windowSize` window — decoupled from the tap callback size. Real devices
    // deliver tiny buffers (one detection per several callbacks); the Simulator
    // delivers fat ones (several per callback). Either way the effective rate is
    // ~sampleRate/hopSize, so the live trace advances smoothly, not in steps.
    // Each detection carries `tOffsetMs` (≤0) = its age vs the newest sample, so
    // JS can place it at the right time even when several arrive in one burst.
    while (samplesSinceDetect >= hopSize) {
        samplesSinceDetect -= hopSize;
        size_t ageFromNewest = samplesSinceDetect;        // samples after this window's end
        if (ageFromNewest + windowSize > total) continue; // window predates the buffer
        size_t endIdx = total - ageFromNewest;
        size_t startIdx = endIdx - windowSize;

        std::vector<double> buf(accumBuffer.begin() + startIdx, accumBuffer.begin() + endIdx);

        double rms = 0;
        for (size_t i = 0; i < buf.size(); i++) rms += buf[i] * buf[i];
        rms = sqrt(rms / buf.size());
        double volume = 20.0 * log10(rms + 1e-10);

        pitchy::PitchDetectionResult result = pitchDetector->detect(buf, sampleRate, minVolume);

        // Confidence gate (native — moved off the JS hook to keep DSP/filtering
        // native-side). Below the threshold the pitch is unreliable, so mark it
        // unvoiced (pitch = -1); volume + confidence are still emitted so breath
        // scoring and diagnostics stay unaffected.
        if (minConfidence > 0.0 && result.confidence < minConfidence) {
            result.pitch = -1;
        }

        double tOffsetMs = -((double)ageFromNewest / sampleRate) * 1000.0;
        // True capture wall-time of THIS detection, from its sample index — immune
        // to delivery/bridge backlog (vs Date.now() at JS receipt, which drifts).
        long long detSample = (long long)totalSamples - (long long)ageFromNewest;
        double tCaptureMs = calibWallMs + ((double)(detSample - (long long)calibSample) / sampleRate) * 1000.0;
        [self sendEventWithName:@"onPitchDetected" body:@{
            @"pitch": @(result.pitch),
            @"confidence": @(result.confidence),
            @"volume": @(volume),
            @"tOffsetMs": @(tOffsetMs),
            @"tCaptureMs": @(tCaptureMs)
        }];
    }

    // Keep only what the next sliding windows need (bounds memory between calls).
    size_t keep = (size_t)windowSize + hopSize;
    if (accumBuffer.size() > keep) {
        accumBuffer.erase(accumBuffer.begin(), accumBuffer.end() - keep);
    }
}

#ifdef RCT_NEW_ARCH_ENABLED
- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
    return std::make_shared<facebook::react::NativePitchySpecJSI>(params);
}
#endif

@end
