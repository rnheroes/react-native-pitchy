#ifdef __cplusplus
#pragma once
#endif

#import <Foundation/Foundation.h>
#import <React/RCTBridgeModule.h>

/**
 * Spec protocol for the Pitchy native module.
 * This protocol defines the interface for the React Native JavaScript code to interact with.
 */
@protocol NativePitchySpec <RCTBridgeModule>

/**
 * Initialize the audio engine with the provided configuration.
 */
- (void)configure:(NSDictionary *)config;

/**
 * Start pitch detection.
 */
- (void)start:(RCTPromiseResolveBlock)resolve
       reject:(RCTPromiseRejectBlock)reject;

/**
 * Stop pitch detection.
 */
- (void)stop:(RCTPromiseResolveBlock)resolve
      reject:(RCTPromiseRejectBlock)reject;

/**
 * Query if pitch detection is currently active.
 */
- (void)isRecording:(RCTPromiseResolveBlock)resolve
            reject:(RCTPromiseRejectBlock)reject;

@end 
