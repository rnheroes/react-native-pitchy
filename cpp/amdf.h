#ifndef PITCHY_AMDF_H
#define PITCHY_AMDF_H

#include <vector>
#include "pitch-detector.h"

namespace pitchy {
  /**
   * Average Magnitude Difference Function (AMDF)
   * Like autocorrelation but uses absolute differences — no multiplications.
   * Extremely fast, used in production by Smule and similar apps.
   * Trade-off: more susceptible to noise than YIN/MPM.
   *
   * @param buf       Audio samples
   * @param sampleRate Sample rate in Hz
   * @param minVolume  Minimum volume threshold in dB
   * @return PitchDetectionResult with pitch and confidence
   */
  PitchDetectionResult amdfDetect(const std::vector<double> &buf, double sampleRate, double minVolume);
}

#endif /* PITCHY_AMDF_H */
