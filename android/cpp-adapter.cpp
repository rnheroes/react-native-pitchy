#include <jni.h>
#include "pitch-detector.h"
#include "react-native-pitchy.h"
#include <vector>
#include <string>
#include <memory>

static std::unique_ptr<pitchy::PitchDetector> g_pitchDetector;

extern "C" JNIEXPORT void JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeConfigurePitchDetector(JNIEnv *env, jobject thiz, jstring algorithm, jstring modelPath) {
    g_pitchDetector = std::make_unique<pitchy::PitchDetector>();

    const char *algoStr = env->GetStringUTFChars(algorithm, nullptr);
    g_pitchDetector->setAlgorithm(std::string(algoStr));
    env->ReleaseStringUTFChars(algorithm, algoStr);
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeDetectPitch(JNIEnv *env, jobject thiz, jshortArray buffer, jint sampleRate, jdouble minVolume) {
    if (!g_pitchDetector) return -1.0;

    jshort *buf = env->GetShortArrayElements(buffer, nullptr);
    jsize size = env->GetArrayLength(buffer);
    std::vector<double> vec(buf, buf + size);
    env->ReleaseShortArrayElements(buffer, buf, 0);

    pitchy::PitchDetectionResult result = g_pitchDetector->detect(vec, sampleRate, minVolume);
    return result.pitch;
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeDetectPitchWithConfidence(JNIEnv *env, jobject thiz, jshortArray buffer, jint sampleRate, jdouble minVolume) {
    jdoubleArray out = env->NewDoubleArray(2);
    double vals[2] = {-1.0, 0.0};

    if (g_pitchDetector) {
        jshort *buf = env->GetShortArrayElements(buffer, nullptr);
        jsize size = env->GetArrayLength(buffer);
        std::vector<double> vec(buf, buf + size);
        env->ReleaseShortArrayElements(buffer, buf, 0);

        pitchy::PitchDetectionResult result = g_pitchDetector->detect(vec, sampleRate, minVolume);
        vals[0] = result.pitch;
        vals[1] = result.confidence;
    }

    env->SetDoubleArrayRegion(out, 0, 2, vals);
    return out;
}

extern "C" JNIEXPORT void JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeReleasePitchDetector(JNIEnv *env, jobject thiz) {
    if (g_pitchDetector) {
        g_pitchDetector->release();
        g_pitchDetector.reset();
    }
}

// Legacy JNI function for backward compatibility
extern "C" JNIEXPORT jdouble JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeAutoCorrelate(JNIEnv *env, jobject thiz, jshortArray buffer, jint sampleRate, jdouble minVolume) {
    jshort *buf = env->GetShortArrayElements(buffer, nullptr);
    jsize size = env->GetArrayLength(buffer);
    std::vector<double> vec(buf, buf + size);
    env->ReleaseShortArrayElements(buffer, buf, 0);

    double result = pitchy::autoCorrelate(vec, sampleRate, minVolume);
    return result;
}
