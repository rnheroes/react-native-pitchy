#ifndef PITCHY_MPM_H
#define PITCHY_MPM_H

#include <vector>
#include "pitch-detector.h"

namespace pitchy {
  /**
   * McLeod Pitch Method (MPM)
   * Uses Normalized Squared Difference Function (NSDF) with key-maxima peak picking.
   * Bounded output [-1, 1] makes thresholding reliable.
   * Now returns real confidence from NSDF peak height.
   *
   * @param buf       Audio samples (normalized [-1, 1])
   * @param sampleRate Sample rate in Hz
   * @param minVolume  Minimum volume threshold in dB
   * @param cutoff     Key maxima selection cutoff (0.0-1.0, default 0.93)
   * @return PitchDetectionResult with pitch and confidence
   */
  PitchDetectionResult mpmDetect(const std::vector<double> &buf, double sampleRate, double minVolume, double cutoff = 0.93);
}

#endif /* PITCHY_MPM_H */
