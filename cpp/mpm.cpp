#include "mpm.h"
#include <cmath>
#include <complex>

namespace pitchy {

// Reuse FFT from yin-fft — simple radix-2 Cooley-Tukey
static void mpmFft(std::vector<std::complex<double>> &x) {
    int N = x.size();
    if (N <= 1) return;

    std::vector<std::complex<double>> even(N / 2), odd(N / 2);
    for (int i = 0; i < N / 2; i++) {
        even[i] = x[2 * i];
        odd[i] = x[2 * i + 1];
    }

    mpmFft(even);
    mpmFft(odd);

    for (int k = 0; k < N / 2; k++) {
        auto t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}

static void mpmIfft(std::vector<std::complex<double>> &x) {
    int N = x.size();
    for (auto &v : x) v = std::conj(v);
    mpmFft(x);
    for (auto &v : x) v = std::conj(v) / static_cast<double>(N);
}

static int nextPow2(int n) {
    int p = 1;
    while (p < n) p *= 2;
    return p;
}

// Parabolic interpolation around index p
static double parabolicInterp(const std::vector<double> &data, int p) {
    if (p <= 0 || p >= static_cast<int>(data.size()) - 1) {
        return static_cast<double>(p);
    }
    double x0 = data[p - 1];
    double x1 = data[p];
    double x2 = data[p + 1];
    double a = (x0 + x2 - 2.0 * x1) / 2.0;
    if (std::abs(a) < 1e-12) return static_cast<double>(p);
    double b = (x2 - x0) / 2.0;
    return p - b / (2.0 * a);
}

PitchDetectionResult mpmDetect(const std::vector<double> &buf, double sampleRate, double minVolume, double cutoff) {
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

    // Step 1: Compute autocorrelation via FFT
    int fftSize = nextPow2(N * 2);
    std::vector<std::complex<double>> fftBuf(fftSize, {0, 0});
    for (int i = 0; i < N; i++) {
        fftBuf[i] = {buf[i], 0};
    }
    mpmFft(fftBuf);

    // Power spectrum → autocorrelation
    for (int i = 0; i < fftSize; i++) {
        fftBuf[i] = fftBuf[i] * std::conj(fftBuf[i]);
    }
    mpmIfft(fftBuf);

    int tauMax = N / 2;

    // Step 2: Compute NSDF (Normalized Squared Difference Function)
    // NSDF(τ) = 2r(τ) / m(τ)
    double m = 0;
    for (int i = 0; i < N; i++) {
        m += buf[i] * buf[i];
    }
    m *= 2.0;

    std::vector<double> nsdf(tauMax);
    nsdf[0] = 1.0;

    for (int tau = 1; tau < tauMax; tau++) {
        m -= buf[tau - 1] * buf[tau - 1] + buf[N - tau] * buf[N - tau];
        if (m > 1e-10) {
            nsdf[tau] = 2.0 * fftBuf[tau].real() / m;
        } else {
            nsdf[tau] = 0;
        }
    }

    // Step 3: Find positive key maxima
    // Skip the first positive region (near-zero lag autocorrelation, not real periodicity)
    struct Peak {
        int index;
        double value;
    };
    std::vector<Peak> keyMaxima;

    // Find first zero crossing to skip the trivial autocorrelation region
    int startTau = 1;
    while (startTau < tauMax && nsdf[startTau] > 0) {
        startTau++;
    }

    bool positiveRegion = false;
    Peak currentMax = {0, -1.0};

    for (int tau = startTau; tau < tauMax; tau++) {
        if (nsdf[tau] > 0 && !positiveRegion) {
            positiveRegion = true;
            currentMax = {tau, nsdf[tau]};
        } else if (nsdf[tau] > 0 && positiveRegion) {
            if (nsdf[tau] > currentMax.value) {
                currentMax = {tau, nsdf[tau]};
            }
        } else if (nsdf[tau] <= 0 && positiveRegion) {
            keyMaxima.push_back(currentMax);
            positiveRegion = false;
        }
    }
    if (positiveRegion && currentMax.value > 0) {
        keyMaxima.push_back(currentMax);
    }

    if (keyMaxima.empty()) return result;

    // Step 4: Find the highest key maximum
    double maxPeakValue = -1;
    for (const auto &peak : keyMaxima) {
        if (peak.value > maxPeakValue) {
            maxPeakValue = peak.value;
        }
    }

    // Step 5: Select the first key maximum above the cutoff threshold
    double thresh = maxPeakValue * cutoff;
    int bestTau = -1;
    double bestPeakValue = 0;

    for (const auto &peak : keyMaxima) {
        if (peak.value >= thresh) {
            bestTau = peak.index;
            bestPeakValue = peak.value;
            break;
        }
    }

    if (bestTau < 1) return result;

    // Step 6: Parabolic interpolation for sub-sample precision
    double refinedTau = parabolicInterp(nsdf, bestTau);
    if (refinedTau <= 0) return result;

    result.pitch = sampleRate / refinedTau;

    // Step 7: Confidence from NSDF peak height
    // NSDF peak value is in [-1, 1], higher = more periodic
    // Clamp to [0, 1] for confidence
    result.confidence = std::max(0.0, std::min(1.0, bestPeakValue));

    return result;
}

} // namespace pitchy
