#ifndef PITCHY_PITCH_DETECTOR_H
#define PITCHY_PITCH_DETECTOR_H

#include <vector>
#include <string>
#include <memory>

namespace pitchy {

enum class Algorithm {
    ACF2Plus,
    YIN,
    MPM,
    HPS,
    AMDF,
    RAPT
};

struct PitchDetectionResult {
    double pitch;       // Hz, or -1 if no pitch detected
    double confidence;  // 0.0 - 1.0
};

class PitchDetector {
public:
    PitchDetector();
    ~PitchDetector();

    void setAlgorithm(Algorithm algo);
    void setAlgorithm(const std::string &algoName);
    Algorithm getAlgorithm() const;

    PitchDetectionResult detect(const std::vector<double> &buf, double sampleRate, double minVolume);
    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace pitchy

#endif /* PITCHY_PITCH_DETECTOR_H */
