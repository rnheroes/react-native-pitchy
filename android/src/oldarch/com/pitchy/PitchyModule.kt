package com.pitchy

import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule

@ReactModule(name = PitchyModuleImpl.NAME)
class PitchyModule(reactContext: ReactApplicationContext) :
  ReactContextBaseJavaModule(reactContext) {

  private val impl = PitchyModuleImpl(reactContext)

  override fun getName(): String = PitchyModuleImpl.NAME

  @ReactMethod
  fun configure(config: ReadableMap) {
    impl.configure(config)
  }

  @ReactMethod
  fun start(promise: Promise) {
    impl.start(promise)
  }

  @ReactMethod
  fun stop(promise: Promise) {
    impl.stop(promise)
  }

  @ReactMethod
  fun isRecording(promise: Promise) {
    impl.isRecording(promise)
  }

  @ReactMethod
  fun addListener(eventName: String) {
    // Required for RCTEventEmitter
  }

  @ReactMethod
  fun removeListeners(count: Int) {
    // Required for RCTEventEmitter
  }
}
