package com.pitchy

import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.module.annotations.ReactModule

@ReactModule(name = PitchyModuleImpl.NAME)
class PitchyModule(reactContext: ReactApplicationContext) :
  NativePitchySpec(reactContext) {

  private val impl = PitchyModuleImpl(reactContext)

  override fun getName(): String = PitchyModuleImpl.NAME

  override fun configure(config: ReadableMap) {
    impl.configure(config)
  }

  override fun start(promise: Promise) {
    impl.start(promise)
  }

  override fun stop(promise: Promise) {
    impl.stop(promise)
  }

  override fun isRecording(promise: Promise) {
    impl.isRecording(promise)
  }

  override fun addListener(eventName: String) {
    // Required for RCTEventEmitter
  }

  override fun removeListeners(count: Double) {
    // Required for RCTEventEmitter
  }
}
