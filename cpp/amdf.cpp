#include "amdf.h"
#include <cmath>

namespace pitchy {

PitchDetectionResult amdfDetect(const std::vector<double> &buf, double sampleRate, double minVolume) {
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

    // Frequency range: 50 Hz to 5000 Hz
    int tauMin = static_cast<int>(sampleRate / 5000.0);
    int tauMax = static_cast<int>(sampleRate / 50.0);
    if (tauMin < 1) tauMin = 1;
    if (tauMax > N / 2) tauMax = N / 2;
    if (tauMax <= tauMin) return result;

    // Step 1: Compute AMDF — no multiplications, only abs and subtract
    // AMDF(τ) = (1/W) * Σ_{i=0}^{W-1} |x[i] - x[i+τ]|
    // Minimum of AMDF corresponds to the fundamental period
    std::vector<double> amdf(tauMax + 1);
    amdf[0] = 0;

    for (int tau = tauMin; tau <= tauMax; tau++) {
        int W = N - tau;
        double sum = 0;
        for (int i = 0; i < W; i++) {
            sum += std::abs(buf[i] - buf[i + tau]);
        }
        amdf[tau] = sum / W;
    }

    // Step 2: Normalize AMDF for confidence computation
    // Find the maximum AMDF value for normalization
    double maxAmdf = 0;
    for (int tau = tauMin; tau <= tauMax; tau++) {
        if (amdf[tau] > maxAmdf) maxAmdf = amdf[tau];
    }
    if (maxAmdf < 1e-10) return result;

    // Step 3: Find the first significant minimum
    // Require a descent→rise pattern (true local minimum), not just edge effects
    int bestTau = -1;
    double bestVal = maxAmdf;

    // State machine: track whether we've seen a descent
    bool hasDescended = false;

    for (int tau = tauMin + 1; tau <= tauMax; tau++) {
        if (amdf[tau] < amdf[tau - 1]) {
            // Descending
            hasDescended = true;
        } else if (amdf[tau] > amdf[tau - 1] && hasDescended) {
            // Rising after descent — tau-1 is a true local minimum
            int minTau = tau - 1;
            if (amdf[minTau] < bestVal) {
                bestVal = amdf[minTau];
                bestTau = minTau;
                // Use first good minimum to avoid octave errors
                double ratio = bestVal / maxAmdf;
                if (ratio < 0.3) break;
            }
            hasDescended = false;
        }
    }

    if (bestTau < 1) return result;

    // Step 4: Parabolic interpolation for sub-sample precision
    double refinedTau = bestTau;
    if (bestTau > tauMin && bestTau < tauMax) {
        double x0 = amdf[bestTau - 1];
        double x1 = amdf[bestTau];
        double x2 = amdf[bestTau + 1];
        double a = (x0 + x2 - 2.0 * x1) / 2.0;
        if (std::abs(a) > 1e-12) {
            double b = (x2 - x0) / 2.0;
            double delta = -b / (2.0 * a);
            if (std::abs(delta) < 1.0) {
                refinedTau = bestTau + delta;
            }
        }
    }

    result.pitch = sampleRate / refinedTau;

    // Step 5: Confidence from AMDF depth
    // Lower AMDF minimum relative to max = more periodic = higher confidence
    double depthRatio = 1.0 - (bestVal / maxAmdf);
    result.confidence = std::max(0.0, std::min(1.0, depthRatio));

    return result;
}

} // namespace pitchy
