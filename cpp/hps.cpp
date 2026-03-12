#include "hps.h"
#include <cmath>
#include <complex>

namespace pitchy {

// Radix-2 Cooley-Tukey FFT
static void hpsFft(std::vector<std::complex<double>> &x) {
    int N = x.size();
    if (N <= 1) return;

    std::vector<std::complex<double>> even(N / 2), odd(N / 2);
    for (int i = 0; i < N / 2; i++) {
        even[i] = x[2 * i];
        odd[i] = x[2 * i + 1];
    }

    hpsFft(even);
    hpsFft(odd);

    for (int k = 0; k < N / 2; k++) {
        auto t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

static int nextPow2(int n) {
    int p = 1;
    while (p < n) p *= 2;
    return p;
}

PitchDetectionResult hpsDetect(const std::vector<double> &buf, double sampleRate, double minVolume, int numHarmonics) {
    PitchDetectionResult result = {-1.0, 0.0};

    int N = buf.size();
    if (N < 4) return result;

    // Volume check
    double rms = 0;
    for (int i = 0; i < N; i++) {
        rms += buf[i] * buf[i];
    }
    rms = std::sqrt(rms / N);
    double decibel = 20.0 * std::log10(rms + 1e-10);
    if (decibel < minVolume) return result;

    // Apply Hanning window to reduce spectral leakage
    int fftSize = nextPow2(N);
    std::vector<std::complex<double>> fftBuf(fftSize, {0, 0});
    for (int i = 0; i < N; i++) {
        double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (N - 1)));
        fftBuf[i] = {buf[i] * window, 0};
    }

    hpsFft(fftBuf);

    // Compute magnitude spectrum (only positive frequencies)
    int specSize = fftSize / 2;
    std::vector<double> magnitude(specSize);
    for (int i = 0; i < specSize; i++) {
        magnitude[i] = std::abs(fftBuf[i]);
    }

    // Frequency resolution
    double freqResolution = sampleRate / fftSize;

    // Limit search range: 50 Hz to 5000 Hz
    int minBin = static_cast<int>(50.0 / freqResolution);
    int maxBin = static_cast<int>(5000.0 / freqResolution);
    if (minBin < 1) minBin = 1;
    if (maxBin > specSize / numHarmonics) maxBin = specSize / numHarmonics;
    if (maxBin <= minBin) return result;

    // Harmonic Product Spectrum: multiply downsampled copies
    int hpsLen = maxBin - minBin;
    std::vector<double> hps(hpsLen, 1.0);

    for (int h = 1; h <= numHarmonics; h++) {
        for (int i = minBin; i < maxBin; i++) {
            int idx = i * h;
            if (idx < specSize) {
                hps[i - minBin] *= magnitude[idx];
            } else {
                hps[i - minBin] = 0;
            }
        }
    }

    // Find the peak and second-best peak in the HPS
    int bestBin = 0;
    double bestVal = -1;
    int secondBin = 0;
    double secondVal = -1;

    for (int i = 0; i < hpsLen; i++) {
        if (hps[i] > bestVal) {
            secondVal = bestVal;
            secondBin = bestBin;
            bestVal = hps[i];
            bestBin = i;
        } else if (hps[i] > secondVal) {
            secondVal = hps[i];
            secondBin = i;
        }
    }

    if (bestVal <= 0) return result;

    // Convert back to absolute bin index
    int absBin = bestBin + minBin;

    // Parabolic interpolation on the magnitude spectrum for sub-bin precision
    double refinedBin = absBin;
    if (absBin > 0 && absBin < specSize - 1) {
        double x0 = std::log(magnitude[absBin - 1] + 1e-10);
        double x1 = std::log(magnitude[absBin] + 1e-10);
        double x2 = std::log(magnitude[absBin + 1] + 1e-10);
        double a = (x0 + x2 - 2.0 * x1) / 2.0;
        if (std::abs(a) > 1e-12) {
            double b = (x2 - x0) / 2.0;
            double delta = -b / (2.0 * a);
            if (std::abs(delta) < 1.0) {
                refinedBin = absBin + delta;
            }
        }
    }

    result.pitch = refinedBin * freqResolution;

    // Confidence from peak prominence ratio
    // How much stronger is the best peak compared to the second best
    if (secondVal > 0) {
        double ratio = bestVal / secondVal;
        // Map ratio to [0, 1]: ratio of 1 = no prominence (0.0), ratio >= 10 = very prominent (1.0)
        result.confidence = std::min(1.0, std::max(0.0, (ratio - 1.0) / 9.0));
    } else {
        result.confidence = 1.0; // Only one peak — very confident
    }

    return result;
}

} // namespace pitchy
