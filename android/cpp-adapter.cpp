#include <jni.h>
#include "react-native-pitchy.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

extern "C" JNIEXPORT jdouble JNICALL
Java_com_pitchy_PitchyModuleImpl_nativeAutoCorrelate(JNIEnv *env, jobject thiz, jshortArray buffer, jint sampleRate, jdouble minVolume)
{
    // pitchy::autoCorrelate(const std::vector<double> &buf, double sampleRate, double minVolume)
    // Convert jshortArray to std::vector<double>
    jshort *buf = env->GetShortArrayElements(buffer, 0);
    jsize size = env->GetArrayLength(buffer);
    std::vector<double> vec(buf, buf + size);
    env->ReleaseShortArrayElements(buffer, buf, 0);

    // Call pitchy::autoCorrelate
    double result = pitchy::autoCorrelate(vec, sampleRate, minVolume);
    return result;
}