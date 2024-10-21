#import "Pitchy.h"
#import <AVFoundation/AVFoundation.h>
#import <React/RCTLog.h>

@implementation Pitchy {
    AVAudioEngine *audioEngine;
    double sampleRate;
    double minVolume;
    BOOL isRecording;
    BOOL isInitialized;
}

RCT_EXPORT_MODULE()

- (NSArray<NSString *> *)supportedEvents {
  return @[@"onPitchDetected"];
}

RCT_EXPORT_METHOD(init:(NSDictionary *)config) {
    #if TARGET_IPHONE_SIMULATOR
        RCTLogInfo(@"Pitchy module is not supported on the iOS simulator");
        return;
    #endif
    if (!isInitialized) {
        audioEngine = [[AVAudioEngine alloc] init];
        AVAudioInputNode *inputNode = [audioEngine inputNode];
        
        AVAudioFormat *format = [inputNode inputFormatForBus:0];
        sampleRate = format.sampleRate;
        minVolume = [config[@"minVolume"] doubleValue];

        [inputNode installTapOnBus:0 bufferSize:[config[@"bufferSize"] unsignedIntValue] format:format block:^(AVAudioPCMBuffer * _Nonnull buffer, AVAudioTime * _Nonnull when) {
            [self detectPitch:buffer];
        }];

        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        [session setCategory:AVAudioSessionCategoryPlayAndRecord
                        mode:AVAudioSessionModeMeasurement
                     options:AVAudioSessionCategoryOptionAllowBluetooth|AVAudioSessionCategoryOptionAllowBluetoothA2DP
                       error:&error];
        if (error) {
            RCTLogError(@"Error setting AVAudioSession category: %@", error);
        }
        
        [session setActive:YES error:&error];
        if (error) {
            RCTLogError(@"Error activating AVAudioSession: %@", error);
        }

        isInitialized = YES;
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

    NSError *error = nil;
    [audioEngine startAndReturnError:&error];
    if (error) {
        reject(@"start_error", @"Failed to start audio engine", error);
    } else {
        isRecording = YES;
        resolve(@(YES));
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
    
    [self sendEventWithName:@"onPitchDetected" body:@{@"pitch": @(detectedPitch)}];
}

@end
