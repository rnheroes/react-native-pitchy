package com.pitchy

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.WritableMap
import com.facebook.react.modules.core.DeviceEventManagerModule

import android.media.AudioRecord
import android.media.AudioFormat
import android.media.MediaRecorder

import kotlin.concurrent.thread

class PitchyModule(reactContext: ReactApplicationContext) :
  ReactContextBaseJavaModule(reactContext) {

  private var isRecording = false
  private var isInitialized = false
 
  private var audioRecord: AudioRecord? = null
  private var recordingThread: Thread? = null
  
  private var sampleRate: Int = 44100

  private var minVolume: Double = 0.0
  private var bufferSize: Int = 0

  override fun getName(): String {
    return NAME
  }

  @ReactMethod
  fun configure(config: ReadableMap) {
      minVolume = config.getDouble("minVolume")
      bufferSize = config.getInt("bufferSize")

      audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, bufferSize)

      isInitialized = true
  }

  @ReactMethod
  fun isRecording(promise: Promise) {
      promise.resolve(isRecording)
  }

  @ReactMethod
  fun addListener(eventName: String) {
    // Keep: Required for RN built in Event Emitter Calls.
  }

  @ReactMethod
  fun removeListeners(count: Int) {
    // Keep: Required for RN built in Event Emitter Calls.
  }

  @ReactMethod
  fun start(promise: Promise) {

    if(!isInitialized) {
      promise.reject("E_NOT_INITIALIZED", "Not initialized")
      return
    }

    if (isRecording) {
      promise.reject("E_ALREADY_RECORDING", "Already recording")
      return
    }

    startRecording()
    promise.resolve(true)
  }

  @ReactMethod
  fun stop(promise: Promise) {
    if (!isRecording) {
      promise.reject("E_NOT_RECORDING", "Not recording")
      return
    }

    stopRecording()
    promise.resolve(true)
  }

  private fun startRecording(){
    audioRecord?.startRecording()
    isRecording = true

    recordingThread = thread(start = true) {
      val buffer = ShortArray(bufferSize)
      while (isRecording) {
        val read = audioRecord?.read(buffer, 0, bufferSize)
        if (read != null && read > 0) {
          detectPitch(buffer)
        }
      }
    }
  }

  private fun stopRecording() {
    isRecording = false
    audioRecord?.stop()
    audioRecord?.release()
    audioRecord = null
    recordingThread?.interrupt()
    recordingThread = null
  }

  private external fun nativeAutoCorrelate(buffer: ShortArray, sampleRate: Int, minVolume: Double): Double

  private fun detectPitch(buffer: ShortArray){
    val pitch = nativeAutoCorrelate(buffer, sampleRate, minVolume)
    val params: WritableMap = Arguments.createMap()
    params.putDouble("pitch", pitch)
    reactApplicationContext.getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java).emit("onPitchDetected", params)
  }

  companion object {
    const val NAME = "Pitchy"
    init {
      System.loadLibrary("react-native-pitchy")
    }
  }
}