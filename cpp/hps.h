#ifndef PITCHY_HPS_H
#define PITCHY_HPS_H

#include <vector>
#include "pitch-detector.h"

namespace pitchy {
  /**
   * Harmonic Product Spectrum (HPS)
   * Frequency-domain pitch detection that multiplies downsampled magnitude spectra.
   * The fundamental frequency is amplified while harmonics are suppressed.
   * Now returns real confidence from peak prominence ratio.
   *
   * @param buf        Audio samples (normalized [-1, 1])
   * @param sampleRate Sample rate in Hz
   * @param minVolume  Minimum volume threshold in dB
   * @param numHarmonics Number of harmonic products (default 5)
   * @return PitchDetectionResult with pitch and confidence
   */
  PitchDetectionResult hpsDetect(const std::vector<double> &buf, double sampleRate, double minVolume, int numHarmonics = 5);
}

#endif /* PITCHY_HPS_H */
