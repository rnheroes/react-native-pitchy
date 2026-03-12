#include "rapt.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace pitchy {

/**
 * Compute NCCF at a specific lag.
 */
static double nccfAtLag(const double *signal, int N, int lag, int windowSize) {
    if (lag + windowSize > N || windowSize <= 0 || lag < 0) return 0.0;

    double crossCorr = 0.0;
    double energy0 = 0.0;
    double energyL = 0.0;

    for (int i = 0; i < windowSize; i++) {
        crossCorr += signal[i] * signal[i + lag];
        energy0   += signal[i] * signal[i];
        energyL   += signal[i + lag] * signal[i + lag];
    }

    double denom = std::sqrt(energy0 * energyL);
    if (denom < 1e-12) return 0.0;
    return crossCorr / denom;
}

/**
 * Compute full NCCF array between lagMin and lagMax.
 */
static std::vector<double> computeFullNccf(const double *signal, int N,
                                            int lagMin, int lagMax, int windowSize) {
    std::vector<double> nccf(lagMax + 1, 0.0);
    for (int tau = lagMin; tau <= lagMax; tau++) {
        nccf[tau] = nccfAtLag(signal, N, tau, windowSize);
    }
    return nccf;
}

/**
 * Pick all local maxima in NCCF above threshold.
 * Returns (lag, nccfValue) pairs sorted by nccfValue descending.
 */
struct Candidate { int lag; double nccf; };

static std::vector<Candidate> pickPeaks(const std::vector<double> &nccf,
                                         int lagMin, int lagMax,
                                         double threshold = 0.2) {
    std::vector<Candidate> peaks;
    for (int tau = lagMin + 1; tau < lagMax; tau++) {
        if (nccf[tau] > nccf[tau - 1] && nccf[tau] >= nccf[tau + 1] && nccf[tau] > threshold) {
            peaks.push_back({tau, nccf[tau]});
        }
    }
    std::sort(peaks.begin(), peaks.end(),
              [](const Candidate &a, const Candidate &b) { return a.nccf > b.nccf; });
    return peaks;
}

/**
 * Apply parabolic interpolation around a peak lag to get sub-sample precision.
 */
static double parabolicRefine(const std::vector<double> &nccf, int lag,
                               int lagMin, int lagMax) {
    if (lag <= lagMin || lag >= lagMax ||
        lag <= 0 || lag >= static_cast<int>(nccf.size()) - 1)
        return lag;

    double x0 = nccf[lag - 1];
    double x1 = nccf[lag];
    double x2 = nccf[lag + 1];
    double a = (x0 + x2 - 2.0 * x1) / 2.0;
    if (std::abs(a) < 1e-12) return lag;

    double delta = -(x2 - x0) / (4.0 * a);
    if (std::abs(delta) > 1.0) return lag;
    return lag + delta;
}

PitchDetectionResult raptDetect(const std::vector<double> &buf,
                                 double sampleRate,
                                 double minVolume,
                                 double minFreq,
                                 double maxFreq) {
    PitchDetectionResult result = {-1.0, 0.0};

    int N = static_cast<int>(buf.size());
    if (N < 64) return result;

    // Volume check
    double rms = 0;
    for (int i = 0; i < N; i++) rms += buf[i] * buf[i];
    rms = std::sqrt(rms / N);
    if (20.0 * std::log10(rms + 1e-10) < minVolume) return result;

    const double *data = buf.data();

    // Lag bounds: lag = sampleRate / freq
    int lagMin = std::max(2, static_cast<int>(std::floor(sampleRate / maxFreq)));
    int lagMax = static_cast<int>(std::ceil(sampleRate / minFreq));

    // Window size — use ~75% of the buffer, leaving room for max lag
    int windowSize = N - lagMax;
    if (windowSize < 64) {
        windowSize = N / 2;
        lagMax = N - windowSize - 1;
    }
    if (lagMin >= lagMax || windowSize < 32) return result;

    // ═══════════════════════════════════════════════════════
    // Stage 1: Coarse search on 4× downsampled signal
    // ═══════════════════════════════════════════════════════
    const int DS = 4;
    int coarseN = N / DS;
    int coarseLagMin = std::max(1, lagMin / DS);
    int coarseLagMax = lagMax / DS;

    std::vector<double> coarse(coarseN);
    for (int i = 0; i < coarseN; i++) {
        double s = 0;
        for (int j = 0; j < DS; j++) s += buf[i * DS + j];
        coarse[i] = s / DS;
    }

    int coarseWindow = coarseN - coarseLagMax;
    if (coarseWindow < 16) coarseWindow = coarseN / 2;
    if (coarseLagMax > coarseN - coarseWindow) coarseLagMax = coarseN - coarseWindow;

    std::vector<Candidate> coarsePeaks;
    if (coarseLagMin < coarseLagMax && coarseWindow >= 16) {
        auto coarseNccf = computeFullNccf(coarse.data(), coarseN,
                                           coarseLagMin, coarseLagMax, coarseWindow);
        coarsePeaks = pickPeaks(coarseNccf, coarseLagMin, coarseLagMax, 0.15);
    }

    // ═══════════════════════════════════════════════════════
    // Stage 2: Fine NCCF on original signal
    // ═══════════════════════════════════════════════════════
    auto fineNccf = computeFullNccf(data, N, lagMin, lagMax, windowSize);
    auto finePeaks = pickPeaks(fineNccf, lagMin, lagMax, 0.15);

    if (finePeaks.empty()) return result;

    // ═══════════════════════════════════════════════════════
    // Candidate selection with shortest-lag bias (octave correction)
    //
    // RAPT's key insight: among NCCF peaks that are multiples of each
    // other (subharmonics), prefer the SHORTEST lag (highest frequency)
    // as long as its NCCF value is reasonably close to the best.
    // This prevents octave-down errors.
    // ═══════════════════════════════════════════════════════

    double globalBestNccf = finePeaks[0].nccf;

    // Threshold: accept any peak within 85% of the best NCCF
    double acceptThresh = globalBestNccf * 0.85;

    // Among acceptable peaks, pick the shortest lag (highest freq)
    int bestLag = finePeaks[0].lag;
    double bestNccf = finePeaks[0].nccf;

    for (const auto &peak : finePeaks) {
        if (peak.nccf >= acceptThresh && peak.lag < bestLag) {
            bestLag = peak.lag;
            bestNccf = peak.nccf;
        }
    }

    // Cross-validate with coarse candidates if available
    // If the coarse stage found a candidate near our fine best, boost confidence
    bool coarseConfirmed = false;
    for (const auto &cp : coarsePeaks) {
        int coarseFine = cp.lag * DS;
        if (std::abs(coarseFine - bestLag) <= DS * 2) {
            coarseConfirmed = true;
            break;
        }
    }

    // Parabolic refinement
    double refinedLag = parabolicRefine(fineNccf, bestLag, lagMin, lagMax);
    if (refinedLag <= 0) return result;

    double pitch = sampleRate / refinedLag;
    if (pitch < minFreq || pitch > maxFreq) return result;

    result.pitch = pitch;
    result.confidence = std::max(0.0, std::min(1.0, bestNccf));

    // Slight confidence boost if coarse confirmed
    if (coarseConfirmed && result.confidence < 1.0) {
        result.confidence = std::min(1.0, result.confidence + 0.05);
    }

    return result;
}

} // namespace pitchy
