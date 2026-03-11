#import "Pitchy.h"
#import <AVFoundation/AVFoundation.h>
#import <React/RCTLog.h>

@implementation Pitchy {
    AVAudioEngine *audioEngine;
    double sampleRate;
    double minVolume;
    BOOL isRecording;
    BOOL isInitialized;
    BOOL hasListeners;
}

RCT_EXPORT_MODULE()

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
    // Required for RCTEventEmitter
}

RCT_EXPORT_METHOD(removeListeners:(double)count) {
    // Required for RCTEventEmitter
}

RCT_EXPORT_METHOD(configure:(NSDictionary *)config) {
    if (!isInitialized) {
        @try {
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;
            [session setCategory:AVAudioSessionCategoryPlayAndRecord
                            mode:AVAudioSessionModeMeasurement
                         options:AVAudioSessionCategoryOptionDefaultToSpeaker
                           error:&error];
            if (error) {
                RCTLogError(@"Error setting AVAudioSession category: %@", error);
                return;
            }

            [session setActive:YES error:&error];
            if (error) {
                RCTLogError(@"Error activating AVAudioSession: %@", error);
                return;
            }

            audioEngine = [[AVAudioEngine alloc] init];
            AVAudioInputNode *inputNode = [audioEngine inputNode];

            AVAudioFormat *format = [inputNode inputFormatForBus:0];
            sampleRate = format.sampleRate;
            minVolume = [config[@"minVolume"] doubleValue];

            [inputNode installTapOnBus:0 bufferSize:[config[@"bufferSize"] unsignedIntValue] format:format block:^(AVAudioPCMBuffer * _Nonnull buffer, AVAudioTime * _Nonnull when) {
                [self detectPitch:buffer];
            }];

            isInitialized = YES;
        } @catch (NSException *exception) {
            RCTLogError(@"Pitchy configure error: %@ - %@", exception.name, exception.reason);
        }
    }
}

RCT_EXPORT_METHOD(isRecording:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject) {
    resolve([NSNumber numberWithBool:isRecording]);
}

RCT_EXPORT_METHOD(start:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject) {
    if (!isInitialized) {
        reject(@"not_initialized", @"Pitchy module is not initialized", nil);
        return;
    }

    if(isRecording){
        reject(@"already_recording", @"Already recording", nil);
        return;
    }

    @try {
        NSError *error = nil;
        [audioEngine startAndReturnError:&error];
        if (error) {
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

    if (!isRecording) {
        reject(@"not_recording", @"Not recording", nil);
        return;
    }

    [audioEngine stop];
    isRecording = NO;
    resolve(@(YES));
}

- (void)detectPitch:(AVAudioPCMBuffer *)buffer {
    float *channelData = buffer.floatChannelData[0];
    std::vector<double> buf(channelData, channelData + buffer.frameLength);

    double detectedPitch = pitchy::autoCorrelate(buf, sampleRate, minVolume);

    if (hasListeners) {
        [self sendEventWithName:@"onPitchDetected" body:@{@"pitch": @(detectedPitch)}];
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
