#ifndef PITCHY_YIN_FFT_H
#define PITCHY_YIN_FFT_H

#include <vector>
#include "pitch-detector.h"

namespace pitchy {
  /**
   * YIN pitch detection (FFT-accelerated) with advanced improvements:
   * - Linear frequency bias for octave error prevention
   * - Multi-candidate lobe analysis
   * - CMND-based confidence metric
   * - Minimum frequency filter
   *
   * @param buf        Audio samples
   * @param sampleRate Sample rate in Hz
   * @param minVolume  Minimum volume threshold in dB
   * @param threshold  CMND threshold (default 0.13)
   * @return PitchDetectionResult with pitch and confidence
   */
  PitchDetectionResult yinDetect(const std::vector<double> &buf, double sampleRate, double minVolume, double threshold = 0.13);
}

#endif /* PITCHY_YIN_FFT_H */
