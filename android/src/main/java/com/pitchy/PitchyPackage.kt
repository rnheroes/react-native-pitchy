package com.pitchy

import com.facebook.react.TurboReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider

class PitchyPackage : TurboReactPackage() {
  override fun getModule(name: String, reactContext: ReactApplicationContext): NativeModule? {
    return when (name) {
      PitchyModuleImpl.NAME -> PitchyModule(reactContext)
      else -> null
    }
  }

  override fun getReactModuleInfoProvider(): ReactModuleInfoProvider {
    return ReactModuleInfoProvider {
      val moduleInfos: MutableMap<String, ReactModuleInfo> = HashMap()
      val isTurboModule = BuildConfig.IS_NEW_ARCHITECTURE_ENABLED

      moduleInfos[PitchyModuleImpl.NAME] = ReactModuleInfo(
        PitchyModuleImpl.NAME,
        PitchyModuleImpl.NAME,
        false,  // canOverrideExistingModule
        true,   // needsEagerInit
        false,  // isCxxModule
        isTurboModule
      )
      moduleInfos
    }
  }
}
