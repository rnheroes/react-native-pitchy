#include "pitch-detector.h"
#include "react-native-pitchy.h"
#include "yin-fft.h"
#include "mpm.h"
#include "hps.h"
#include "amdf.h"
#include "rapt.h"
#include <cmath>

namespace pitchy {

struct PitchDetector::Impl {
    Algorithm algorithm = Algorithm::ACF2Plus;
};

PitchDetector::PitchDetector() : pImpl(std::make_unique<Impl>()) {}
PitchDetector::~PitchDetector() { release(); }

void PitchDetector::setAlgorithm(Algorithm algo) {
    pImpl->algorithm = algo;
}

void PitchDetector::setAlgorithm(const std::string &algoName) {
    if (algoName == "YIN") {
        pImpl->algorithm = Algorithm::YIN;
    } else if (algoName == "MPM") {
        pImpl->algorithm = Algorithm::MPM;
    } else if (algoName == "HPS") {
        pImpl->algorithm = Algorithm::HPS;
    } else if (algoName == "AMDF") {
        pImpl->algorithm = Algorithm::AMDF;
    } else if (algoName == "RAPT") {
        pImpl->algorithm = Algorithm::RAPT;
    } else {
        pImpl->algorithm = Algorithm::ACF2Plus;
    }
}

Algorithm PitchDetector::getAlgorithm() const {
    return pImpl->algorithm;
}

PitchDetectionResult PitchDetector::detect(const std::vector<double> &buf, double sampleRate, double minVolume) {
    PitchDetectionResult result = {-1.0, 0.0};

    switch (pImpl->algorithm) {
        case Algorithm::ACF2Plus: {
            result.pitch = autoCorrelate(buf, sampleRate, minVolume);
            result.confidence = (result.pitch > 0) ? 1.0 : 0.0;
            break;
        }
        case Algorithm::YIN: {
            result = yinDetect(buf, sampleRate, minVolume);
            break;
        }
        case Algorithm::MPM: {
            result = mpmDetect(buf, sampleRate, minVolume);
            break;
        }
        case Algorithm::HPS: {
            result = hpsDetect(buf, sampleRate, minVolume);
            break;
        }
        case Algorithm::AMDF: {
            result = amdfDetect(buf, sampleRate, minVolume);
            break;
        }
        case Algorithm::RAPT: {
            result = raptDetect(buf, sampleRate, minVolume);
            break;
        }
    }

    return result;
}

void PitchDetector::release() {
    // No stateful resources to release in free tier
}

} // namespace pitchy
