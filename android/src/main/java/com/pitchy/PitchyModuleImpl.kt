package com.pitchy

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.WritableMap
import com.facebook.react.modules.core.DeviceEventManagerModule

import android.media.AudioRecord
import android.media.AudioFormat
import android.media.MediaRecorder

import kotlin.concurrent.thread

class PitchyModuleImpl(private val reactContext: ReactApplicationContext) {

  private var isRecording = false
  private var isInitialized = false

  private var audioRecord: AudioRecord? = null
  private var recordingThread: Thread? = null

  private var sampleRate: Int = 44100

  private var minVolume: Double = 0.0
  private var bufferSize: Int = 0
  private var algorithm: String = "ACF2+"

  fun configure(config: ReadableMap) {
    minVolume = config.getDouble("minVolume")
    bufferSize = config.getInt("bufferSize")

    if (config.hasKey("algorithm")) {
      algorithm = config.getString("algorithm") ?: "ACF2+"
    }

    nativeConfigurePitchDetector(algorithm, "")

    audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, bufferSize)

    isInitialized = true
  }

  fun isRecording(promise: Promise) {
    promise.resolve(isRecording)
  }

  fun start(promise: Promise) {
    if (!isInitialized) {
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

  fun stop(promise: Promise) {
    if (!isRecording) {
      promise.reject("E_NOT_RECORDING", "Not recording")
      return
    }

    stopRecording()
    promise.resolve(true)
  }

  private fun startRecording() {
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
    nativeReleasePitchDetector()
  }

  private external fun nativeAutoCorrelate(buffer: ShortArray, sampleRate: Int, minVolume: Double): Double
  private external fun nativeConfigurePitchDetector(algorithm: String, modelPath: String)
  private external fun nativeDetectPitch(buffer: ShortArray, sampleRate: Int, minVolume: Double): Double
  private external fun nativeDetectPitchWithConfidence(buffer: ShortArray, sampleRate: Int, minVolume: Double): DoubleArray
  private external fun nativeReleasePitchDetector()

  private fun detectPitch(buffer: ShortArray) {
    // Calculate volume (RMS → dB)
    var rms = 0.0
    for (sample in buffer) {
      val normalized = sample.toDouble() / Short.MAX_VALUE
      rms += normalized * normalized
    }
    rms = Math.sqrt(rms / buffer.size)
    val volume = 20.0 * Math.log10(rms + 1e-10)

    val pitchResult = nativeDetectPitchWithConfidence(buffer, sampleRate, minVolume)
    val pitch = pitchResult[0]
    val confidence = pitchResult[1]
    val params: WritableMap = Arguments.createMap()
    params.putDouble("pitch", pitch)
    params.putDouble("confidence", confidence)
    params.putDouble("volume", volume)
    reactContext.getJSModule(DeviceEventManagerModule.RCTDeviceEventEmitter::class.java).emit("onPitchDetected", params)
  }

  companion object {
    const val NAME = "Pitchy"
    init {
      System.loadLibrary("react-native-pitchy")
    }
  }
}
