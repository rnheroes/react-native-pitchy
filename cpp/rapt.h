#ifndef PITCHY_RAPT_H
#define PITCHY_RAPT_H

#include <vector>
#include "pitch-detector.h"

namespace pitchy {
  /**
   * RAPT (Robust Algorithm for Pitch Tracking)
   * Talkin, 1995
   *
   * Two-stage pitch detection:
   * 1. Coarse search on downsampled signal (fast candidate generation)
   * 2. Refined search on original signal near coarse candidates
   *
   * Uses Normalized Cross-Correlation Function (NCCF) at both stages.
   * Gold standard for speech processing, robust voiced/unvoiced detection.
   *
   * @param buf        Audio samples
   * @param sampleRate Sample rate in Hz
   * @param minVolume  Minimum volume threshold in dB
   * @param minFreq    Minimum candidate frequency (default 50 Hz)
   * @param maxFreq    Maximum candidate frequency (default 500 Hz, speech-optimized)
   * @return PitchDetectionResult with pitch and confidence
   */
  PitchDetectionResult raptDetect(const std::vector<double> &buf,
                                   double sampleRate,
                                   double minVolume,
                                   double minFreq = 50.0,
                                   double maxFreq = 500.0);
}

#endif /* PITCHY_RAPT_H */
