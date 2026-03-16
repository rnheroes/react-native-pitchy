/**
 * Pitch detection algorithm test suite
 * Generates synthetic audio signals and tests each algorithm against its intended use case.
 *
 * Compile: clang++ -std=c++17 -O2 -o test_algorithms test_algorithms.cpp \
 *          react-native-pitchy.cpp yin-fft.cpp mpm.cpp hps.cpp amdf.cpp pyin.cpp salience.cpp \
 *          -I. && ./test_algorithms
 */

#include "react-native-pitchy.h"
#include "yin-fft.h"
#include "mpm.h"
#include "hps.h"
#include "amdf.h"
#include "pyin.h"
#include "salience.h"
#include "pitch-detector.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <chrono>
#include <string>
#include <random>

// ── Signal generators ──────────────────────────────────────────────

static std::vector<double> generateSine(double freq, double sampleRate, int numSamples, double amplitude = 0.8) {
    std::vector<double> buf(numSamples);
    for (int i = 0; i < numSamples; i++) {
        buf[i] = amplitude * std::sin(2.0 * M_PI * freq * i / sampleRate);
    }
    return buf;
}

static std::vector<double> generateHarmonics(double fundamental, double sampleRate, int numSamples,
                                              int numHarmonics = 5, double amplitude = 0.8) {
    std::vector<double> buf(numSamples, 0.0);
    for (int h = 1; h <= numHarmonics; h++) {
        double harmonicAmp = amplitude / h; // natural harmonic decay
        for (int i = 0; i < numSamples; i++) {
            buf[i] += harmonicAmp * std::sin(2.0 * M_PI * fundamental * h * i / sampleRate);
        }
    }
    // Normalize
    double maxVal = 0;
    for (auto &v : buf) maxVal = std::max(maxVal, std::abs(v));
    if (maxVal > 0) for (auto &v : buf) v *= amplitude / maxVal;
    return buf;
}

static std::vector<double> addNoise(const std::vector<double> &signal, double snrDb) {
    std::mt19937 rng(42);
    std::normal_distribution<double> dist(0.0, 1.0);

    double signalPower = 0;
    for (auto v : signal) signalPower += v * v;
    signalPower /= signal.size();

    double noisePower = signalPower / std::pow(10.0, snrDb / 10.0);
    double noiseAmp = std::sqrt(noisePower);

    std::vector<double> noisy(signal.size());
    for (size_t i = 0; i < signal.size(); i++) {
        noisy[i] = signal[i] + noiseAmp * dist(rng);
    }
    return noisy;
}

static std::vector<double> generateGlissando(double startFreq, double endFreq, double sampleRate, int numSamples) {
    std::vector<double> buf(numSamples);
    double phase = 0;
    for (int i = 0; i < numSamples; i++) {
        double t = (double)i / numSamples;
        double freq = startFreq * std::pow(endFreq / startFreq, t);
        buf[i] = 0.8 * std::sin(phase);
        phase += 2.0 * M_PI * freq / sampleRate;
    }
    return buf;
}

// ── Test helpers ────────────────────────────────────────────────────

struct TestResult {
    std::string name;
    double expectedHz;
    double detectedHz;
    double confidence;
    double errorCents;
    double timeMs;
    bool passed;
};

static double centError(double expected, double detected) {
    if (expected <= 0 || detected <= 0) return 9999.0;
    return 1200.0 * std::log2(detected / expected);
}

static void printResult(const TestResult &r) {
    const char *status = r.passed ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m";
    printf("  [%s] %-42s  expected=%6.1f Hz  detected=%6.1f Hz  error=%+6.1f cents  conf=%.3f  time=%.2fms\n",
           status, r.name.c_str(), r.expectedHz, r.detectedHz, r.errorCents, r.confidence, r.timeMs);
}

static const double SAMPLE_RATE = 44100.0;
static const int BUF_SIZE = 4096;
static const double MIN_VOLUME = -60.0;
static const double CENT_TOLERANCE = 15.0; // ±15 cents = acceptable

// ── Tests ──────────────────────────────────────────────────────────

int main() {
    printf("\n\033[1m╔══════════════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1m║          react-native-pitchy Algorithm Test Suite                ║\033[0m\n");
    printf("\033[1m╚══════════════════════════════════════════════════════════════════╝\033[0m\n\n");

    std::vector<TestResult> results;
    int passed = 0, failed = 0;

    // ── Generate test signals ──
    auto sine440  = generateSine(440.0, SAMPLE_RATE, BUF_SIZE);
    auto sine100  = generateSine(100.0, SAMPLE_RATE, BUF_SIZE);
    auto sine1000 = generateSine(1000.0, SAMPLE_RATE, BUF_SIZE);
    auto sine330  = generateSine(329.63, SAMPLE_RATE, BUF_SIZE); // E4 guitar string

    auto harmonics220 = generateHarmonics(220.0, SAMPLE_RATE, BUF_SIZE, 5); // A3 with 5 harmonics
    auto harmonics110 = generateHarmonics(110.0, SAMPLE_RATE, BUF_SIZE, 8); // A2 with 8 harmonics (bass)

    auto noisySine440_20dB = addNoise(sine440, 20.0);  // moderate noise
    auto noisySine440_10dB = addNoise(sine440, 10.0);   // heavy noise
    auto noisyHarmonics220 = addNoise(harmonics220, 15.0);

    // ════════════════════════════════════════════════════════════════
    // TEST 1: ACF2+ — General purpose autocorrelation
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── ACF2+ (Autocorrelation — general purpose) ──\033[0m\n");
    {
        auto testACF = [&](const std::string &name, const std::vector<double> &buf, double expected) {
            auto t0 = std::chrono::high_resolution_clock::now();
            double pitch = pitchy::autoCorrelate(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, pitch);
            bool ok = std::abs(err) < CENT_TOLERANCE;
            results.push_back({name, expected, pitch, pitch > 0 ? 1.0 : 0.0, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };
        testACF("ACF2+ / 440 Hz sine", sine440, 440.0);
        testACF("ACF2+ / 100 Hz sine", sine100, 100.0);
        testACF("ACF2+ / 1000 Hz sine", sine1000, 1000.0);
        testACF("ACF2+ / E4 (329.63 Hz) sine", sine330, 329.63);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 2: YIN — Guitar tuning, sub-cent precision
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── YIN (FFT-optimized — guitar tuning) ──\033[0m\n");
    {
        auto testYIN = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = pitchy::yinDetect(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, r.pitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };
        testYIN("YIN / 440 Hz sine (sub-cent?)", sine440, 440.0, 2.0);
        testYIN("YIN / E4 guitar string (329.63 Hz)", sine330, 329.63, 5.0);
        testYIN("YIN / 100 Hz low note", sine100, 100.0);
        testYIN("YIN / noisy 440 Hz (SNR 20dB)", noisySine440_20dB, 440.0, 25.0);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 3: MPM — Instrument tuning, NSDF reliability
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── MPM (McLeod Pitch Method — instrument tuning) ──\033[0m\n");
    {
        auto testMPM = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = pitchy::mpmDetect(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, r.pitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };
        testMPM("MPM / 440 Hz sine", sine440, 440.0, 5.0);
        testMPM("MPM / E4 guitar (329.63 Hz)", sine330, 329.63, 5.0);
        testMPM("MPM / A3 harmonics (220 Hz fund.)", harmonics220, 220.0);
        testMPM("MPM / 1000 Hz sine", sine1000, 1000.0);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 4: HPS — Harmonic-rich signals, fundamental extraction
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── HPS (Harmonic Product Spectrum — harmonic signals) ──\033[0m\n");
    {
        auto testHPS = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = pitchy::hpsDetect(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, r.pitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };
        testHPS("HPS / A3 harmonics (220 Hz, 5 harm.)", harmonics220, 220.0, 25.0);
        testHPS("HPS / A2 harmonics (110 Hz, 8 harm.)", harmonics110, 110.0, 25.0);
        testHPS("HPS / 440 Hz pure sine", sine440, 440.0, 25.0);
        testHPS("HPS / noisy harmonics (SNR 15dB)", noisyHarmonics220, 220.0, 50.0);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 5: AMDF — Ultra-fast, no-multiply
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── AMDF (No-multiply — ultra-fast) ──\033[0m\n");
    {
        auto testAMDF = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = pitchy::amdfDetect(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, r.pitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };
        testAMDF("AMDF / 440 Hz sine", sine440, 440.0);
        testAMDF("AMDF / 100 Hz sine", sine100, 100.0);
        testAMDF("AMDF / E4 guitar (329.63 Hz)", sine330, 329.63);
        testAMDF("AMDF / 1000 Hz sine", sine1000, 1000.0);

        // Speed benchmark: run 1000 iterations
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000; i++) {
            pitchy::amdfDetect(sine440, SAMPLE_RATE, MIN_VOLUME);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("  \033[33m[BENCH]\033[0m AMDF 1000 iterations: %.1fms total, %.3fms/call\n", totalMs, totalMs / 1000.0);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 6: pYIN — Temporal smoothing, glissando tracking
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── pYIN (Probabilistic YIN + HMM — vocal melody) ──\033[0m\n");
    {
        pitchy::PYinDetector pyin;
        pyin.init(50.0, 5000.0, 480);

        // Feed several frames of stable 440 Hz to let HMM converge
        auto testPYIN = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = pyin.detect(buf, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, r.pitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };

        // Warm up HMM with a few frames
        for (int i = 0; i < 5; i++) {
            pyin.detect(sine440, SAMPLE_RATE, MIN_VOLUME);
        }
        testPYIN("pYIN / 440 Hz (after HMM warmup)", sine440, 440.0, 20.0);

        // Now test glissando tracking — feed chunks of rising pitch
        pyin.reset();
        pyin.init(50.0, 5000.0, 480);

        // Generate 10 frames of glissando from 300 to 500 Hz
        printf("  \033[33m[GLISSANDO]\033[0m pYIN tracking 300→500 Hz over 10 frames:\n");
        for (int frame = 0; frame < 10; frame++) {
            double t = (double)frame / 9.0;
            double freq = 300.0 * std::pow(500.0 / 300.0, t);
            auto buf = generateSine(freq, SAMPLE_RATE, BUF_SIZE);
            auto r = pyin.detect(buf, SAMPLE_RATE, MIN_VOLUME);
            printf("    frame %d: target=%6.1f Hz  detected=%6.1f Hz  error=%+5.1f cents  conf=%.3f\n",
                   frame, freq, r.pitch, centError(freq, r.pitch), r.confidence);
        }

        pyin.reset();
        pyin.init(50.0, 5000.0, 480);
        // Test noisy signal — pYIN should be more robust than plain YIN
        for (int i = 0; i < 5; i++) {
            pyin.detect(noisySine440_20dB, SAMPLE_RATE, MIN_VOLUME);
        }
        testPYIN("pYIN / noisy 440 Hz (SNR 20dB)", noisySine440_20dB, 440.0, 30.0);
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 7: Salience — Noisy harmonic signals, pitch tracking
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── Salience (Harmonic salience — instrument tuning) ──\033[0m\n");
    {
        pitchy::PitchTracker tracker;
        tracker.init(50.0, 4000.0);

        auto testSalience = [&](const std::string &name, const std::vector<double> &buf, double expected, double tolerance = CENT_TOLERANCE) {
            auto t0 = std::chrono::high_resolution_clock::now();
            std::vector<double> salienceSpectrum, pitchGrid;
            auto sr = pitchy::salienceDetectFull(buf, SAMPLE_RATE, MIN_VOLUME, 10, nullptr, &salienceSpectrum, &pitchGrid);
            tracker.processNewFrame(sr.pitch, sr.confidence, salienceSpectrum, pitchGrid);
            double trackedPitch = tracker.getLatestPitchValue();
            double trackedConf = tracker.getLatestConfidence();
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(expected, trackedPitch);
            bool ok = std::abs(err) < tolerance;
            results.push_back({name, expected, trackedPitch, trackedConf, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        };

        // Warm up tracker
        for (int i = 0; i < 3; i++) {
            std::vector<double> ss, pg;
            auto sr = pitchy::salienceDetectFull(sine440, SAMPLE_RATE, MIN_VOLUME, 10, nullptr, &ss, &pg);
            tracker.processNewFrame(sr.pitch, sr.confidence, ss, pg);
        }
        testSalience("Salience / 440 Hz (with tracking)", sine440, 440.0, 25.0);

        tracker.reset();
        tracker.init(50.0, 4000.0);
        for (int i = 0; i < 3; i++) {
            std::vector<double> ss, pg;
            auto sr = pitchy::salienceDetectFull(harmonics220, SAMPLE_RATE, MIN_VOLUME, 10, nullptr, &ss, &pg);
            tracker.processNewFrame(sr.pitch, sr.confidence, ss, pg);
        }
        testSalience("Salience / A3 harmonics (220 Hz)", harmonics220, 220.0, 30.0);

        tracker.reset();
        tracker.init(50.0, 4000.0);
        for (int i = 0; i < 3; i++) {
            std::vector<double> ss, pg;
            auto sr = pitchy::salienceDetectFull(noisyHarmonics220, SAMPLE_RATE, MIN_VOLUME, 10, nullptr, &ss, &pg);
            tracker.processNewFrame(sr.pitch, sr.confidence, ss, pg);
        }
        testSalience("Salience / noisy harmonics (SNR 15dB)", noisyHarmonics220, 220.0, 50.0);

        // Raw salience (no tracker) test
        auto t0 = std::chrono::high_resolution_clock::now();
        auto rawResult = pitchy::salienceDetectFull(sine440, SAMPLE_RATE, MIN_VOLUME);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double err = centError(440.0, rawResult.pitch);
        bool ok = std::abs(err) < 25.0;
        results.push_back({"Salience / 440 Hz (raw, no tracker)", 440.0, rawResult.pitch, rawResult.confidence, err, ms, ok});
        printResult(results.back());
        ok ? passed++ : failed++;
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // TEST 8: PitchDetector unified API
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── PitchDetector unified API ──\033[0m\n");
    {
        pitchy::PitchDetector detector;
        std::string algos[] = {"ACF2+", "YIN", "MPM", "HPS", "AMDF", "pYIN", "Salience"};
        for (auto &algoName : algos) {
            detector.setAlgorithm(algoName);
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = detector.detect(sine440, SAMPLE_RATE, MIN_VOLUME);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double err = centError(440.0, r.pitch);
            bool ok = std::abs(err) < 30.0;
            std::string name = "PitchDetector(" + algoName + ") / 440 Hz";
            results.push_back({name, 440.0, r.pitch, r.confidence, err, ms, ok});
            printResult(results.back());
            ok ? passed++ : failed++;
        }
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // BENCHMARK: Performance comparison (1000 iterations each)
    // ════════════════════════════════════════════════════════════════
    printf("\033[1;36m── Performance Benchmark (1000 iterations, 4096 samples @ 44.1kHz) ──\033[0m\n");
    {
        const int ITERS = 1000;
        auto bench = [&](const std::string &name, auto fn) {
            // Warm up
            for (int i = 0; i < 10; i++) fn();

            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERS; i++) fn();
            auto t1 = std::chrono::high_resolution_clock::now();
            double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double perCall = totalMs / ITERS;
            double fps = 1000.0 / perCall;
            printf("  %-20s  %7.3f ms/call  %6.0f fps  (%.0fms total)\n",
                   name.c_str(), perCall, fps, totalMs);
            return perCall;
        };

        double t_acf = bench("ACF2+", [&]() { pitchy::autoCorrelate(sine440, SAMPLE_RATE, MIN_VOLUME); });
        double t_yin = bench("YIN", [&]() { pitchy::yinDetect(sine440, SAMPLE_RATE, MIN_VOLUME); });
        double t_mpm = bench("MPM", [&]() { pitchy::mpmDetect(sine440, SAMPLE_RATE, MIN_VOLUME); });
        double t_hps = bench("HPS", [&]() { pitchy::hpsDetect(harmonics220, SAMPLE_RATE, MIN_VOLUME); });
        double t_amdf = bench("AMDF", [&]() { pitchy::amdfDetect(sine440, SAMPLE_RATE, MIN_VOLUME); });

        // pYIN: stateful, so reset between bench runs
        pitchy::PYinDetector pyinBench;
        pyinBench.init(50.0, 5000.0, 480);
        double t_pyin = bench("pYIN", [&]() { pyinBench.detect(sine440, SAMPLE_RATE, MIN_VOLUME); });

        // Salience with tracker
        pitchy::PitchTracker trackerBench;
        trackerBench.init(50.0, 4000.0);
        double t_sal = bench("Salience", [&]() {
            std::vector<double> ss, pg;
            auto sr = pitchy::salienceDetectFull(sine440, SAMPLE_RATE, MIN_VOLUME, 10, nullptr, &ss, &pg);
            trackerBench.processNewFrame(sr.pitch, sr.confidence, ss, pg);
        });

        printf("\n  \033[1mRanking (fastest → slowest):\033[0m\n");
        struct Rank { std::string name; double ms; };
        std::vector<Rank> ranks = {
            {"ACF2+", t_acf}, {"YIN", t_yin}, {"MPM", t_mpm},
            {"HPS", t_hps}, {"AMDF", t_amdf}, {"pYIN", t_pyin},
            {"Salience", t_sal}
        };
        std::sort(ranks.begin(), ranks.end(), [](const Rank &a, const Rank &b) { return a.ms < b.ms; });
        for (size_t i = 0; i < ranks.size(); i++) {
            printf("  %zu. %-12s %.3f ms\n", i + 1, ranks[i].name.c_str(), ranks[i].ms);
        }
    }
    printf("\n");

    // ════════════════════════════════════════════════════════════════
    // Summary
    // ════════════════════════════════════════════════════════════════
    printf("\033[1m══════════════════════════════════════════════════════════════════\033[0m\n");
    printf("\033[1m  RESULTS: %d passed, %d failed, %d total\033[0m\n", passed, failed, passed + failed);
    printf("\033[1m══════════════════════════════════════════════════════════════════\033[0m\n\n");

    return failed > 0 ? 1 : 0;
}
