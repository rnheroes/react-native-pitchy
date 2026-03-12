#include "yin-fft.h"
#include <cmath>
#include <algorithm>
#include <complex>

namespace pitchy {

// Simple radix-2 Cooley-Tukey FFT (no external dependencies)
static void fft(std::vector<std::complex<double>> &x) {
    int N = x.size();
    if (N <= 1) return;

    std::vector<std::complex<double>> even(N / 2), odd(N / 2);
    for (int i = 0; i < N / 2; i++) {
        even[i] = x[2 * i];
        odd[i] = x[2 * i + 1];
    }

    fft(even);
    fft(odd);

    for (int k = 0; k < N / 2; k++) {
        auto t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

static void ifft(std::vector<std::complex<double>> &x) {
    int N = x.size();
    for (auto &v : x) v = std::conj(v);
    fft(x);
    for (auto &v : x) v = std::conj(v) / static_cast<double>(N);
}

static int nextPowerOf2(int n) {
    int p = 1;
    while (p < n) p *= 2;
    return p;
}

static double getVolumeDecibel(double rms) {
    return 20.0 * std::log10(rms + 1e-10);
}

PitchDetectionResult yinDetect(const std::vector<double> &buf, double sampleRate, double minVolume, double threshold) {
    PitchDetectionResult result = {-1.0, 0.0};

    int SIZE = buf.size();
    if (SIZE < 4) return result;

    // Volume check
    double rms = 0;
    for (int i = 0; i < SIZE; i++) {
        rms += buf[i] * buf[i];
    }
    rms = std::sqrt(rms / SIZE);
    if (getVolumeDecibel(rms) < minVolume) return result;

    int tauMax = SIZE / 2;

    // Step 1: Autocorrelation via FFT
    int fftSize = nextPowerOf2(SIZE * 2);
    std::vector<std::complex<double>> fftBuf(fftSize, {0, 0});
    for (int i = 0; i < SIZE; i++) {
        fftBuf[i] = {buf[i], 0};
    }
    fft(fftBuf);

    // Power spectrum
    for (int i = 0; i < fftSize; i++) {
        fftBuf[i] = fftBuf[i] * std::conj(fftBuf[i]);
    }
    ifft(fftBuf);

    // Step 2: Difference function from autocorrelation
    double r0 = fftBuf[0].real();
    std::vector<double> d(tauMax);
    d[0] = 0;
    for (int tau = 1; tau < tauMax; tau++) {
        d[tau] = 2.0 * r0 - 2.0 * fftBuf[tau].real();
        if (d[tau] < 0) d[tau] = 0; // Clamp numerical errors
    }

    // Step 3: Cumulative mean normalized difference function (CMNDF)
    std::vector<double> dPrime(tauMax);
    dPrime[0] = 1.0;
    double runningSum = 0;
    for (int tau = 1; tau < tauMax; tau++) {
        runningSum += d[tau];
        dPrime[tau] = (runningSum > 0) ? d[tau] * tau / runningSum : 1.0;
    }

    // Step 4: Linear frequency bias for octave error prevention
    // Biases toward shorter lags (higher frequencies) to prevent octave errors
    // Formula: CMND[i] -= ((N-1-i)/(N-1)) * biasFactor
    const double biasFactor = 0.15;
    std::vector<double> biasedCMND(tauMax);
    for (int tau = 0; tau < tauMax; tau++) {
        double bias = (static_cast<double>(tauMax - 1 - tau) / (tauMax - 1)) * biasFactor;
        biasedCMND[tau] = dPrime[tau] - bias;
    }

    // Step 5: Multi-candidate selection via zero-crossing/lobe analysis
    // Subtract threshold, find zero crossings, find minimum in each lobe
    std::vector<double> thresholded(tauMax);
    for (int tau = 0; tau < tauMax; tau++) {
        thresholded[tau] = biasedCMND[tau] - threshold;
    }

    // Minimum frequency filter: 65.41 Hz (C2)
    const double minFreq = 65.41;
    int maxTauForMinFreq = static_cast<int>(sampleRate / minFreq);
    if (maxTauForMinFreq > tauMax) maxTauForMinFreq = tauMax;

    // Find zero crossings (where thresholded goes from positive to negative)
    struct Candidate {
        int tau;
        double cmndValue; // biased CMND value at this tau
    };
    std::vector<Candidate> candidates;

    int tau = 2; // Start from tau=2 (tau=0,1 are not meaningful)
    while (tau < maxTauForMinFreq) {
        // Find where thresholded crosses zero (goes negative = below threshold)
        if (thresholded[tau] < 0) {
            // We're in a lobe below threshold — find the minimum in this lobe
            int bestInLobe = tau;
            double bestVal = biasedCMND[tau];

            while (tau < maxTauForMinFreq && thresholded[tau] < 0) {
                if (biasedCMND[tau] < bestVal) {
                    bestVal = biasedCMND[tau];
                    bestInLobe = tau;
                }
                tau++;
            }
            candidates.push_back({bestInLobe, bestVal});
        } else {
            tau++;
        }
    }

    if (candidates.empty()) {
        // Fallback: global minimum of biased CMND
        double minVal = biasedCMND[2];
        int bestTau = 2;
        for (int t = 3; t < maxTauForMinFreq; t++) {
            if (biasedCMND[t] < minVal) {
                minVal = biasedCMND[t];
                bestTau = t;
            }
        }
        if (minVal >= 0.5) return result;
        candidates.push_back({bestTau, minVal});
    }

    // Select the candidate with the lowest biased CMND value
    int bestTau = candidates[0].tau;
    double bestCMND = candidates[0].cmndValue;
    for (size_t i = 1; i < candidates.size(); i++) {
        if (candidates[i].cmndValue < bestCMND) {
            bestCMND = candidates[i].cmndValue;
            bestTau = candidates[i].tau;
        }
    }

    // Step 6: Parabolic interpolation on the ORIGINAL (unbiased) CMND
    double refinedTau = bestTau;
    if (bestTau > 0 && bestTau < tauMax - 1) {
        double x0 = dPrime[bestTau - 1];
        double x1 = dPrime[bestTau];
        double x2 = dPrime[bestTau + 1];
        double a = (x0 + x2 - 2.0 * x1) / 2.0;
        if (std::abs(a) > 1e-12) {
            double b = (x2 - x0) / 2.0;
            double delta = -b / (2.0 * a);
            if (std::abs(delta) < 1.0) {
                refinedTau = bestTau + delta;
            }
        }
    }

    if (refinedTau <= 0) return result;

    result.pitch = sampleRate / refinedTau;

    // Step 7: Confidence from CMND value
    // Lower CMND = more periodic = higher confidence
    // Map dPrime[bestTau] from [0, 1] to confidence [1, 0]
    double cmndAtBest = dPrime[bestTau];
    if (cmndAtBest < 0) cmndAtBest = 0;
    if (cmndAtBest > 1) cmndAtBest = 1;
    result.confidence = 1.0 - cmndAtBest;

    return result;
}

} // namespace pitchy
