#include "rapt.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace pitchy {

// --- Low-pass filter (simple FIR for downsampling) ---

static std::vector<double> lowPassFilter(const std::vector<double> &input, int factor) {
    // Simple averaging FIR filter for anti-aliasing before downsampling
    int outLen = static_cast<int>(input.size()) / factor;
    if (outLen <= 0) return {};

    std::vector<double> output(outLen);
    for (int i = 0; i < outLen; i++) {
        double sum = 0.0;
        int start = i * factor;
        int end = std::min(start + factor, static_cast<int>(input.size()));
        for (int j = start; j < end; j++) {
            sum += input[j];
        }
        output[i] = sum / (end - start);
    }
    return output;
}

// --- Normalized Cross-Correlation Function (NCCF) ---

static std::vector<double> computeNccf(const std::vector<double> &signal,
                                         int tauMin, int tauMax, int windowSize) {
    int N = static_cast<int>(signal.size());
    if (tauMax > N - windowSize) tauMax = N - windowSize;
    if (tauMin < 0) tauMin = 0;

    int nccfSize = tauMax + 1;
    std::vector<double> nccf(nccfSize, 0.0);

    // Energy of reference window
    double refEnergy = 0.0;
    for (int i = 0; i < windowSize; i++) {
        refEnergy += signal[i] * signal[i];
    }

    for (int tau = tauMin; tau < nccfSize; tau++) {
        if (tau + windowSize > N) break;

        // Cross-correlation at lag tau
        double crossCorr = 0.0;
        double shiftedEnergy = 0.0;
        for (int i = 0; i < windowSize; i++) {
            crossCorr += signal[i] * signal[i + tau];
            shiftedEnergy += signal[i + tau] * signal[i + tau];
        }

        // Normalized cross-correlation
        double denom = std::sqrt(refEnergy * shiftedEnergy);
        if (denom > 1e-10) {
            nccf[tau] = crossCorr / denom;
        }
    }

    return nccf;
}

// --- Peak picking in NCCF ---

struct RaptCandidate {
    int lag;
    double nccfValue;
};

static std::vector<RaptCandidate> pickPeaks(const std::vector<double> &nccf,
                                             int tauMin, int tauMax,
                                             double threshold = 0.3) {
    std::vector<RaptCandidate> candidates;
    int size = std::min(static_cast<int>(nccf.size()), tauMax + 1);

    for (int tau = tauMin + 1; tau < size - 1; tau++) {
        // Local maximum check
        if (nccf[tau] > nccf[tau - 1] && nccf[tau] > nccf[tau + 1] && nccf[tau] > threshold) {
            candidates.push_back({tau, nccf[tau]});
        }
    }

    // Sort by NCCF value descending
    std::sort(candidates.begin(), candidates.end(),
              [](const RaptCandidate &a, const RaptCandidate &b) {
                  return a.nccfValue > b.nccfValue;
              });

    // Keep top 5 candidates
    if (candidates.size() > 5) {
        candidates.resize(5);
    }

    return candidates;
}

PitchDetectionResult raptDetect(const std::vector<double> &buf,
                                 double sampleRate,
                                 double minVolume,
                                 double minFreq,
                                 double maxFreq) {
    PitchDetectionResult result = {-1.0, 0.0};

    int N = static_cast<int>(buf.size());
    if (N < 4) return result;

    // Volume check
    double rms = 0;
    for (int i = 0; i < N; i++) {
        rms += buf[i] * buf[i];
    }
    rms = std::sqrt(rms / N);
    double decibel = 20.0 * std::log10(rms + 1e-10);
    if (decibel < minVolume) return result;

    // =================================================================
    // Stage 1: Coarse search on downsampled signal
    // =================================================================
    const int downsampleFactor = 4;
    double coarseSampleRate = sampleRate / downsampleFactor;

    // Lag range for coarse search
    int coarseTauMin = static_cast<int>(coarseSampleRate / maxFreq);
    int coarseTauMax = static_cast<int>(coarseSampleRate / minFreq);
    if (coarseTauMin < 1) coarseTauMin = 1;

    // Downsample
    std::vector<double> coarseSignal = lowPassFilter(buf, downsampleFactor);
    int coarseLen = static_cast<int>(coarseSignal.size());
    if (coarseLen < coarseTauMax + 1) return result;

    // NCCF window size: ~25ms worth of samples at coarse rate
    int coarseWindowSize = static_cast<int>(0.025 * coarseSampleRate);
    if (coarseWindowSize > coarseLen / 2) coarseWindowSize = coarseLen / 2;

    std::vector<double> coarseNccf = computeNccf(coarseSignal, coarseTauMin, coarseTauMax, coarseWindowSize);
    std::vector<RaptCandidate> coarseCandidates = pickPeaks(coarseNccf, coarseTauMin, coarseTauMax, 0.25);

    if (coarseCandidates.empty()) return result;

    // =================================================================
    // Stage 2: Refined search on original signal near coarse candidates
    // =================================================================
    int fineWindowSize = static_cast<int>(0.025 * sampleRate);
    if (fineWindowSize > N / 2) fineWindowSize = N / 2;

    double bestPitch = -1.0;
    double bestConfidence = 0.0;

    for (const auto &coarse : coarseCandidates) {
        // Convert coarse lag to fine lag range
        int centerLag = coarse.lag * downsampleFactor;
        int searchRadius = downsampleFactor * 2; // ±2 coarse samples

        int fineTauMin = std::max(1, centerLag - searchRadius);
        int fineTauMax = std::min(N - fineWindowSize - 1, centerLag + searchRadius);
        if (fineTauMin >= fineTauMax) continue;

        std::vector<double> fineNccf = computeNccf(buf, fineTauMin, fineTauMax, fineWindowSize);

        // Find best lag in fine search
        int bestFineLag = fineTauMin;
        double bestFineNccf = -1.0;
        for (int tau = fineTauMin; tau <= fineTauMax && tau < static_cast<int>(fineNccf.size()); tau++) {
            if (fineNccf[tau] > bestFineNccf) {
                bestFineNccf = fineNccf[tau];
                bestFineLag = tau;
            }
        }

        if (bestFineNccf > bestConfidence) {
            // Parabolic interpolation for sub-sample precision
            double refinedLag = bestFineLag;
            if (bestFineLag > fineTauMin && bestFineLag < fineTauMax &&
                bestFineLag > 0 && bestFineLag < static_cast<int>(fineNccf.size()) - 1) {
                double x0 = fineNccf[bestFineLag - 1];
                double x1 = fineNccf[bestFineLag];
                double x2 = fineNccf[bestFineLag + 1];
                double a = (x0 + x2 - 2.0 * x1) / 2.0;
                if (std::abs(a) > 1e-12) {
                    double b = (x2 - x0) / 2.0;
                    double delta = -b / (2.0 * a);
                    if (std::abs(delta) < 1.0) {
                        refinedLag = bestFineLag + delta;
                    }
                }
            }

            if (refinedLag > 0) {
                double pitch = sampleRate / refinedLag;
                if (pitch >= minFreq && pitch <= maxFreq) {
                    bestPitch = pitch;
                    bestConfidence = bestFineNccf;
                }
            }
        }
    }

    if (bestPitch > 0) {
        result.pitch = bestPitch;
        result.confidence = std::max(0.0, std::min(1.0, bestConfidence));
    }

    return result;
}

} // namespace pitchy
