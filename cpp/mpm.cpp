#include "mpm.h"
#include <cmath>
#include <complex>

namespace pitchy {

// Iterative in-place radix-2 Cooley-Tukey FFT. Much cheaper than a recursive
// version: no per-level vector allocations, and the twiddle factor advances by a
// single complex multiply (`w *= wlen`) instead of a std::polar (sin+cos) per
// butterfly. Run ~86×/sec on a 4096-pt transform, this is what keeps the detector
// inside its real-time budget even under heavy concurrent audio load — so the
// input tap can't fall behind and timestamps can't drift late. `inverse` does the
// IFFT with 1/N scaling. Size must be a power of two (the caller pads to nextPow2).
static void mpmFftCore(std::vector<std::complex<double>> &a, bool inverse) {
    int n = static_cast<int>(a.size());
    if (n <= 1) return;

    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / len * (inverse ? 1.0 : -1.0);
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; k++) {
                std::complex<double> u = a[i + k];
                std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto &x : a) x /= static_cast<double>(n);
    }
}

static void mpmFft(std::vector<std::complex<double>> &x) { mpmFftCore(x, false); }
static void mpmIfft(std::vector<std::complex<double>> &x) { mpmFftCore(x, true); }

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

    // Step 3: Find the key maxima of the NSDF — each marks a period-multiple
    // candidate. Mirrors canonical MPM (TarsosDSP / sevagh): skip the initial
    // positive lobe with a HARD (tauMax-1)/3 bound (so the search can never walk
    // PAST the true fundamental on a high note), then the sub-zero trough, then
    // collect local maxima — gating each behind a 0.5 ABSOLUTE floor BEFORE the
    // relative cutoff. Dropping that 0.5 floor was the bug: low ripples and the
    // 2×period sub-harmonic stayed eligible, so high notes locked onto the
    // octave-down peak at confidence ~1.0 (and stray low peaks showed as spikes).
    struct Peak {
        int index;
        double value;
    };
    constexpr double SMALL_CUTOFF = 0.5; // == TarsosDSP/sevagh MPM_SMALL_CUTOFF

    int startTau = 0;
    int lobeBound = (tauMax - 1) / 3;
    while (startTau < lobeBound && nsdf[startTau] > 0.0) startTau++;   // bounded positive-lobe skip
    while (startTau < tauMax - 1 && nsdf[startTau] <= 0.0) startTau++; // skip the sub-zero trough
    if (startTau == 0) startTau = 1;

    std::vector<Peak> keyMaxima;
    double maxPeakValue = -1.0;
    for (int tau = startTau; tau < tauMax - 1; tau++) {
        // local maximum (strict left / inclusive right, per the references) that
        // clears the absolute floor — only these are eligible candidates.
        if (nsdf[tau] > nsdf[tau - 1] && nsdf[tau] >= nsdf[tau + 1] &&
            nsdf[tau] > SMALL_CUTOFF) {
            keyMaxima.push_back({tau, nsdf[tau]});
            if (nsdf[tau] > maxPeakValue) maxPeakValue = nsdf[tau];
        }
    }

    if (keyMaxima.empty()) return result; // nothing genuinely periodic → unvoiced

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
