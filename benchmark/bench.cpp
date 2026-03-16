/**
 * Pitch detection benchmark — measures latency (ms/call) and accuracy (RPA)
 * for all 6 free-tier algorithms.
 *
 * Build:
 *   c++ -std=c++17 -O2 -o bench bench.cpp \
 *       ../cpp/react-native-pitchy.cpp ../cpp/yin-fft.cpp ../cpp/mpm.cpp \
 *       ../cpp/hps.cpp ../cpp/amdf.cpp ../cpp/rapt.cpp ../cpp/pitch-detector.cpp \
 *       -I../cpp -lm
 *
 * Run:
 *   ./bench
 */

#include "pitch-detector.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include <vector>
#include <string>
#include <random>

// ─── Config ───────────────────────────────────────────────
static constexpr int    BUFFER_SIZE  = 4096;
static constexpr double SAMPLE_RATE  = 44100.0;
static constexpr double MIN_VOLUME   = -60.0;
static constexpr int    LATENCY_ITER = 1000;   // iterations for timing

// Test frequencies spanning typical musical range (C2 → C7)
static const double TEST_FREQS[] = {
    65.41,   // C2
    82.41,   // E2
    110.00,  // A2
    130.81,  // C3
    164.81,  // E3
    196.00,  // G3
    220.00,  // A3
    261.63,  // C4 (middle C)
    329.63,  // E4
    392.00,  // G4
    440.00,  // A4 (concert pitch)
    523.25,  // C5
    659.26,  // E5
    783.99,  // G5
    880.00,  // A5
    1046.50, // C6
    1318.51, // E6
    1760.00, // A6
    2093.00, // C7
};
static constexpr int NUM_FREQS = sizeof(TEST_FREQS) / sizeof(TEST_FREQS[0]);

// ─── Signal generators ───────────────────────────────────
static std::vector<double> generateSine(double freq, double amplitude = 0.8) {
    std::vector<double> buf(BUFFER_SIZE);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buf[i] = amplitude * std::sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    }
    return buf;
}

// Sine + harmonics (more realistic instrument-like signal)
static std::vector<double> generateHarmonic(double freq, double amplitude = 0.8) {
    std::vector<double> buf(BUFFER_SIZE, 0.0);
    // fundamental + 2nd, 3rd, 4th harmonics with decreasing amplitude
    double harmonicAmps[] = {1.0, 0.5, 0.25, 0.125};
    for (int h = 0; h < 4; h++) {
        double f = freq * (h + 1);
        if (f >= SAMPLE_RATE / 2.0) break; // Nyquist
        for (int i = 0; i < BUFFER_SIZE; i++) {
            buf[i] += amplitude * harmonicAmps[h] * std::sin(2.0 * M_PI * f * i / SAMPLE_RATE);
        }
    }
    return buf;
}

// Sine + white noise
static std::vector<double> generateNoisy(double freq, double snrDb = 20.0, double amplitude = 0.8) {
    auto buf = generateSine(freq, amplitude);
    double noisePower = amplitude * std::pow(10.0, -snrDb / 20.0);
    std::mt19937 rng(42); // fixed seed for reproducibility
    std::normal_distribution<double> dist(0.0, noisePower);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buf[i] += dist(rng);
    }
    return buf;
}

// ─── Accuracy helpers ────────────────────────────────────
static double centError(double detected, double expected) {
    if (detected <= 0 || expected <= 0) return 9999.0;
    return 1200.0 * std::log2(detected / expected);
}

struct AccuracyResult {
    double rpa;           // Raw Pitch Accuracy (% within 50 cents)
    double rca;           // Raw Chroma Accuracy (% within 50 cents, octave-aware)
    double meanAbsCents;  // Mean absolute cent error (for detected pitches)
    int    detected;      // Number of frames where pitch was detected
    int    total;         // Total frames
};

static AccuracyResult measureAccuracy(pitchy::PitchDetector &detector,
                                       const double *freqs, int nFreqs,
                                       std::vector<double> (*generator)(double, double),
                                       double genArg = 0.8) {
    int correctRPA = 0, correctRCA = 0, detectedCount = 0;
    double totalAbsCents = 0.0;
    int totalFrames = nFreqs;

    for (int f = 0; f < nFreqs; f++) {
        auto buf = generator(freqs[f], genArg);
        auto result = detector.detect(buf, SAMPLE_RATE, MIN_VOLUME);

        if (result.pitch > 0) {
            detectedCount++;
            double cents = centError(result.pitch, freqs[f]);
            double absCents = std::abs(cents);
            totalAbsCents += absCents;

            // RPA: within 50 cents
            if (absCents < 50.0) {
                correctRPA++;
            }

            // RCA: within 50 cents allowing octave errors
            double chromaCents = absCents;
            if (chromaCents >= 1150.0) {
                // Check octave multiples
                double ratio = result.pitch / freqs[f];
                double octaveRatio = std::log2(ratio);
                double nearestOctave = std::round(octaveRatio);
                double octaveCents = std::abs(1200.0 * (octaveRatio - nearestOctave));
                chromaCents = octaveCents;
            }
            if (chromaCents < 50.0) {
                correctRCA++;
            }
        }
    }

    AccuracyResult r;
    r.rpa = totalFrames > 0 ? 100.0 * correctRPA / totalFrames : 0.0;
    r.rca = totalFrames > 0 ? 100.0 * correctRCA / totalFrames : 0.0;
    r.meanAbsCents = detectedCount > 0 ? totalAbsCents / detectedCount : 9999.0;
    r.detected = detectedCount;
    r.total = totalFrames;
    return r;
}

// ─── Latency measurement ─────────────────────────────────
static double measureLatency(pitchy::PitchDetector &detector, int iterations) {
    // Use A4 = 440 Hz sine as standard test signal
    auto buf = generateSine(440.0);

    // Warm up
    for (int i = 0; i < 10; i++) {
        detector.detect(buf, SAMPLE_RATE, MIN_VOLUME);
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        detector.detect(buf, SAMPLE_RATE, MIN_VOLUME);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    return totalMs / iterations;
}

// ─── Main ────────────────────────────────────────────────
int main() {
    struct AlgoInfo {
        std::string name;
        pitchy::Algorithm algo;
    };

    AlgoInfo algos[] = {
        {"ACF2+", pitchy::Algorithm::ACF2Plus},
        {"YIN",   pitchy::Algorithm::YIN},
        {"MPM",   pitchy::Algorithm::MPM},
        {"HPS",   pitchy::Algorithm::HPS},
        {"AMDF",  pitchy::Algorithm::AMDF},
        {"RAPT",  pitchy::Algorithm::RAPT},
    };

    printf("=== react-native-pitchy Benchmark ===\n");
    printf("Buffer: %d samples @ %.0f Hz (%.1f ms of audio)\n",
           BUFFER_SIZE, SAMPLE_RATE, 1000.0 * BUFFER_SIZE / SAMPLE_RATE);
    printf("Latency: %d iterations\n", LATENCY_ITER);
    printf("Test freqs: %d (C2 → C7)\n\n", NUM_FREQS);

    // ── Latency ──
    printf("─── Latency ────────────────────────────────────────\n");
    printf("%-8s  %8s  %8s\n", "Algo", "ms/call", "FPS");
    printf("%-8s  %8s  %8s\n", "────────", "────────", "────────");

    double latencies[6];
    for (int a = 0; a < 6; a++) {
        pitchy::PitchDetector detector;
        detector.setAlgorithm(algos[a].algo);
        latencies[a] = measureLatency(detector, LATENCY_ITER);
        printf("%-8s  %7.2fms  %8.0f\n", algos[a].name.c_str(), latencies[a], 1000.0 / latencies[a]);
    }

    // ── Accuracy: Pure sine ──
    printf("\n─── Accuracy: Pure Sine ─────────────────────────────\n");
    printf("%-8s  %7s  %7s  %10s  %s\n", "Algo", "RPA", "RCA", "Mean|¢|", "Detected");
    printf("%-8s  %7s  %7s  %10s  %s\n", "────────", "───────", "───────", "──────────", "────────");

    for (int a = 0; a < 6; a++) {
        pitchy::PitchDetector detector;
        detector.setAlgorithm(algos[a].algo);
        auto acc = measureAccuracy(detector, TEST_FREQS, NUM_FREQS, generateSine);
        printf("%-8s  %6.1f%%  %6.1f%%  %8.1f ¢  %d/%d\n",
               algos[a].name.c_str(), acc.rpa, acc.rca, acc.meanAbsCents, acc.detected, acc.total);
    }

    // ── Accuracy: Harmonic signal ──
    printf("\n─── Accuracy: Harmonic Signal ───────────────────────\n");
    printf("%-8s  %7s  %7s  %10s  %s\n", "Algo", "RPA", "RCA", "Mean|¢|", "Detected");
    printf("%-8s  %7s  %7s  %10s  %s\n", "────────", "───────", "───────", "──────────", "────────");

    for (int a = 0; a < 6; a++) {
        pitchy::PitchDetector detector;
        detector.setAlgorithm(algos[a].algo);
        auto acc = measureAccuracy(detector, TEST_FREQS, NUM_FREQS, generateHarmonic);
        printf("%-8s  %6.1f%%  %6.1f%%  %8.1f ¢  %d/%d\n",
               algos[a].name.c_str(), acc.rpa, acc.rca, acc.meanAbsCents, acc.detected, acc.total);
    }

    // ── Accuracy: Noisy signal (20 dB SNR) ──
    printf("\n─── Accuracy: Noisy Signal (20 dB SNR) ─────────────\n");
    printf("%-8s  %7s  %7s  %10s  %s\n", "Algo", "RPA", "RCA", "Mean|¢|", "Detected");
    printf("%-8s  %7s  %7s  %10s  %s\n", "────────", "───────", "───────", "──────────", "────────");

    // generateNoisy needs a different signature, use lambda wrapper
    for (int a = 0; a < 6; a++) {
        pitchy::PitchDetector detector;
        detector.setAlgorithm(algos[a].algo);

        int correctRPA = 0, detectedCount = 0;
        double totalAbsCents = 0.0;

        for (int f = 0; f < NUM_FREQS; f++) {
            auto buf = generateNoisy(TEST_FREQS[f], 20.0);
            auto result = detector.detect(buf, SAMPLE_RATE, MIN_VOLUME);
            if (result.pitch > 0) {
                detectedCount++;
                double absCents = std::abs(centError(result.pitch, TEST_FREQS[f]));
                totalAbsCents += absCents;
                if (absCents < 50.0) correctRPA++;
            }
        }
        double rpa = NUM_FREQS > 0 ? 100.0 * correctRPA / NUM_FREQS : 0.0;
        double meanCents = detectedCount > 0 ? totalAbsCents / detectedCount : 9999.0;
        printf("%-8s  %6.1f%%     —     %8.1f ¢  %d/%d\n",
               algos[a].name.c_str(), rpa, meanCents, detectedCount, NUM_FREQS);
    }

    // ── Summary table (for README) ──
    printf("\n─── README Summary ─────────────────────────────────\n");
    printf("| Algorithm | ms/call | FPS | Accuracy (sine) | Accuracy (harmonic) | Accuracy (noisy) |\n");
    printf("|-----------|---------|-----|-----------------|--------------------|-----------------|\n");

    for (int a = 0; a < 6; a++) {
        pitchy::PitchDetector detector;
        detector.setAlgorithm(algos[a].algo);

        auto accSine = measureAccuracy(detector, TEST_FREQS, NUM_FREQS, generateSine);
        auto accHarm = measureAccuracy(detector, TEST_FREQS, NUM_FREQS, generateHarmonic);

        // Noisy
        int correctNoise = 0;
        for (int f = 0; f < NUM_FREQS; f++) {
            auto buf = generateNoisy(TEST_FREQS[f], 20.0);
            auto result = detector.detect(buf, SAMPLE_RATE, MIN_VOLUME);
            if (result.pitch > 0 && std::abs(centError(result.pitch, TEST_FREQS[f])) < 50.0) correctNoise++;
        }
        double noiseRPA = 100.0 * correctNoise / NUM_FREQS;

        printf("| %-9s | %.2f | %.0f | %.1f%% RPA | %.1f%% RPA | %.1f%% RPA |\n",
               algos[a].name.c_str(), latencies[a], 1000.0 / latencies[a],
               accSine.rpa, accHarm.rpa, noiseRPA);
    }

    printf("\nDone.\n");
    return 0;
}
