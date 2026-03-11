#ifdef __cplusplus
#import "react-native-pitchy.h"
#endif

#ifdef RCT_NEW_ARCH_ENABLED
#import <RNPitchySpec/RNPitchySpec.h>
#import <React/RCTEventEmitter.h>

@interface Pitchy : RCTEventEmitter <NativePitchySpec>
#else
#import <React/RCTBridgeModule.h>
#import <React/RCTEventEmitter.h>

@interface Pitchy : RCTEventEmitter <RCTBridgeModule>
#endif

@end
