/**
 * Real-world audio benchmark for pitch detection.
 * Reads WAV files from audio/ directory with ground_truth.csv.
 *
 * Build:
 *   c++ -std=c++17 -O2 -o bench_real bench_real.cpp \
 *       ../cpp/react-native-pitchy.cpp ../cpp/yin-fft.cpp ../cpp/mpm.cpp \
 *       ../cpp/hps.cpp ../cpp/amdf.cpp ../cpp/rapt.cpp ../cpp/pitch-detector.cpp \
 *       -I../cpp -lm
 */

#include "pitch-detector.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <dirent.h>

// ─── Simple WAV reader (16-bit PCM mono) ─────────────────
struct WavFile {
    std::vector<double> samples;
    int sampleRate = 0;
    bool valid = false;
};

static WavFile readWav(const std::string &path) {
    WavFile wav;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return wav;

    // RIFF header
    char riff[4]; fread(riff, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) != 0) { fclose(f); return wav; }

    uint32_t fileSize; fread(&fileSize, 4, 1, f);
    char wave[4]; fread(wave, 1, 4, f);
    if (memcmp(wave, "WAVE", 4) != 0) { fclose(f); return wav; }

    // Find fmt and data chunks
    int16_t numChannels = 0, bitsPerSample = 0;
    int32_t dataSize = 0;

    while (!feof(f)) {
        char chunkId[4];
        uint32_t chunkSize;
        if (fread(chunkId, 1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            int16_t audioFormat;
            fread(&audioFormat, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            int32_t sr; fread(&sr, 4, 1, f);
            wav.sampleRate = sr;
            int32_t byteRate; fread(&byteRate, 4, 1, f);
            int16_t blockAlign; fread(&blockAlign, 2, 1, f);
            fread(&bitsPerSample, 2, 1, f);
            // Skip any extra fmt bytes
            if (chunkSize > 16) {
                fseek(f, chunkSize - 16, SEEK_CUR);
            }
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            if (bitsPerSample == 16 && numChannels >= 1) {
                int numSamples = dataSize / (2 * numChannels);
                wav.samples.resize(numSamples);
                for (int i = 0; i < numSamples; i++) {
                    int16_t sample;
                    fread(&sample, 2, 1, f);
                    wav.samples[i] = sample / 32768.0;
                    // Skip extra channels
                    for (int ch = 1; ch < numChannels; ch++) {
                        int16_t skip; fread(&skip, 2, 1, f);
                    }
                }
                wav.valid = true;
            }
            break;
        } else {
            fseek(f, chunkSize, SEEK_CUR);
        }
    }

    fclose(f);
    return wav;
}

// ─── Ground truth CSV reader ─────────────────────────────
struct TestFile {
    std::string filename;
    std::string source;
    std::string note;
    double freq;
};

static std::vector<TestFile> readGroundTruth(const std::string &csvPath) {
    std::vector<TestFile> files;
    std::ifstream in(csvPath);
    if (!in.is_open()) return files;

    std::string line;
    std::getline(in, line); // skip header

    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string file, source, note, freqStr;
        std::getline(ss, file, ',');
        std::getline(ss, source, ',');
        std::getline(ss, note, ',');
        std::getline(ss, freqStr, ',');
        if (!file.empty() && !freqStr.empty()) {
            files.push_back({file, source, note, std::stod(freqStr)});
        }
    }
    return files;
}

// ─── Helpers ─────────────────────────────────────────────
static double centError(double detected, double expected) {
    if (detected <= 0 || expected <= 0) return 9999.0;
    return std::abs(1200.0 * std::log2(detected / expected));
}

static constexpr int BUFFER_SIZE = 4096;
static constexpr double MIN_VOLUME = -60.0;

struct AlgoResult {
    std::string name;
    int totalFiles = 0;
    int detected = 0;
    int correctRPA = 0;   // within 50 cents
    int correctRCA = 0;   // within 50 cents, octave-aware
    double totalCentErr = 0.0;
    int detectedForCents = 0;
    double totalMs = 0.0;

    // Per-source breakdown
    struct SourceResult {
        int total = 0, detected = 0, correctRPA = 0;
        double totalCents = 0.0;
        int detectedForCents = 0;
    };
    std::map<std::string, SourceResult> bySource;
};

// ─── Main ────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    // Default to audio_real (Philharmonia recordings), fallback to audio (synthesized)
    std::string audioDir = "audio_real";
    if (argc > 1) audioDir = argv[1];

    std::string csvPath = audioDir + "/ground_truth.csv";

    auto testFiles = readGroundTruth(csvPath);
    if (testFiles.empty()) {
        // Try fallback
        if (audioDir == "audio_real") {
            audioDir = "audio";
            csvPath = audioDir + "/ground_truth.csv";
            testFiles = readGroundTruth(csvPath);
        }
        if (testFiles.empty()) {
            printf("Error: No test files found. Run download_real_audio.py first.\n");
            return 1;
        }
    }

    printf("=== Real-World Audio Benchmark ===\n");
    printf("Audio dir: %s\n", audioDir.c_str());
    printf("Test files: %zu\n", testFiles.size());
    printf("Buffer size: %d samples\n\n", BUFFER_SIZE);

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

    std::vector<AlgoResult> results(6);
    for (int a = 0; a < 6; a++) results[a].name = algos[a].name;

    // Process each test file
    for (const auto &tf : testFiles) {
        std::string path = audioDir + "/" + tf.filename;
        WavFile wav = readWav(path);
        if (!wav.valid) {
            printf("  SKIP (can't read): %s\n", tf.filename.c_str());
            continue;
        }

        // Use middle portion of audio (skip attack transient)
        int startSample = wav.sampleRate / 4; // skip first 250ms
        if (startSample + BUFFER_SIZE > (int)wav.samples.size()) {
            startSample = std::max(0, (int)wav.samples.size() - BUFFER_SIZE);
        }
        int endSample = std::min(startSample + BUFFER_SIZE, (int)wav.samples.size());
        std::vector<double> buf(wav.samples.begin() + startSample,
                                wav.samples.begin() + endSample);

        // Pad if too short
        while ((int)buf.size() < BUFFER_SIZE) buf.push_back(0.0);

        // Test each algorithm
        for (int a = 0; a < 6; a++) {
            pitchy::PitchDetector detector;
            detector.setAlgorithm(algos[a].algo);

            // Measure multiple frames for more reliable results
            double bestPitch = -1.0;
            double bestConf = 0.0;
            int numFrames = 0;
            double totalPitch = 0.0;
            int validFrames = 0;

            // Slide through the audio
            for (int offset = startSample;
                 offset + BUFFER_SIZE <= (int)wav.samples.size() && numFrames < 10;
                 offset += BUFFER_SIZE / 2) {
                std::vector<double> frame(wav.samples.begin() + offset,
                                          wav.samples.begin() + offset + BUFFER_SIZE);
                auto r = detector.detect(frame, wav.sampleRate, MIN_VOLUME);
                numFrames++;
                if (r.pitch > 0) {
                    validFrames++;
                    totalPitch += r.pitch;
                    if (r.confidence > bestConf) {
                        bestConf = r.confidence;
                        bestPitch = r.pitch;
                    }
                }
            }

            // Use average pitch for stability
            double detectedPitch = validFrames > 0 ? totalPitch / validFrames : -1.0;

            auto &res = results[a];
            res.totalFiles++;
            auto &src = res.bySource[tf.source];
            src.total++;

            if (detectedPitch > 0) {
                res.detected++;
                src.detected++;
                double cents = centError(detectedPitch, tf.freq);
                res.totalCentErr += cents;
                res.detectedForCents++;
                src.totalCents += cents;
                src.detectedForCents++;

                if (cents < 50.0) {
                    res.correctRPA++;
                    src.correctRPA++;
                }

                // RCA (octave-aware)
                double ratio = detectedPitch / tf.freq;
                double octaveRatio = std::log2(ratio);
                double nearestOctave = std::round(octaveRatio);
                double octaveCents = std::abs(1200.0 * (octaveRatio - nearestOctave));
                if (octaveCents < 50.0) {
                    res.correctRCA++;
                }
            }
        }
    }

    // ─── Print results ───────────────────────────────────

    // Overall results
    printf("─── Overall Results ────────────────────────────────\n");
    printf("%-8s  %7s  %7s  %7s  %10s  %s\n",
           "Algo", "RPA", "RCA", "Detect", "Mean|¢|", "Files");
    printf("%-8s  %7s  %7s  %7s  %10s  %s\n",
           "────────", "───────", "───────", "───────", "──────────", "─────");

    for (int a = 0; a < 6; a++) {
        auto &r = results[a];
        double rpa = r.totalFiles > 0 ? 100.0 * r.correctRPA / r.totalFiles : 0;
        double rca = r.totalFiles > 0 ? 100.0 * r.correctRCA / r.totalFiles : 0;
        double detect = r.totalFiles > 0 ? 100.0 * r.detected / r.totalFiles : 0;
        double meanCents = r.detectedForCents > 0 ? r.totalCentErr / r.detectedForCents : 9999;
        printf("%-8s  %6.1f%%  %6.1f%%  %6.1f%%  %8.1f ¢  %d/%d\n",
               r.name.c_str(), rpa, rca, detect, meanCents, r.correctRPA, r.totalFiles);
    }

    // Per-source breakdown — collect actual sources from data
    std::set<std::string> sourceSet;
    for (int a = 0; a < 6; a++)
        for (const auto &kv : results[a].bySource)
            sourceSet.insert(kv.first);
    std::vector<std::string> sources(sourceSet.begin(), sourceSet.end());
    for (const auto &src : sources) {
        printf("\n─── %s ────────────────────────────────────────\n", src.c_str());
        printf("%-8s  %7s  %10s  %s\n", "Algo", "RPA", "Mean|¢|", "Files");
        printf("%-8s  %7s  %10s  %s\n", "────────", "───────", "──────────", "─────");

        for (int a = 0; a < 6; a++) {
            auto it = results[a].bySource.find(src);
            if (it == results[a].bySource.end()) continue;
            auto &s = it->second;
            double rpa = s.total > 0 ? 100.0 * s.correctRPA / s.total : 0;
            double mc = s.detectedForCents > 0 ? s.totalCents / s.detectedForCents : 9999;
            printf("%-8s  %6.1f%%  %8.1f ¢  %d/%d\n",
                   results[a].name.c_str(), rpa, mc, s.correctRPA, s.total);
        }
    }

    // ─── README-ready table ──────────────────────────────
    printf("\n─── README Table ───────────────────────────────────\n");
    // Dynamic header from actual sources
    printf("| Algorithm ");
    for (const auto &src : sources) printf("| %-7s ", src.c_str());
    printf("| Overall RPA |\n");
    printf("|----------- ");
    for (size_t i = 0; i < sources.size(); i++) printf("|--------");
    printf("|-------------|\n");

    for (int a = 0; a < 6; a++) {
        printf("| %-9s ", results[a].name.c_str());
        for (const auto &src : sources) {
            auto it = results[a].bySource.find(src);
            if (it != results[a].bySource.end() && it->second.total > 0) {
                double rpa = 100.0 * it->second.correctRPA / it->second.total;
                printf("| %5.0f%% ", rpa);
            } else {
                printf("|    —  ");
            }
        }
        double overall = results[a].totalFiles > 0
            ? 100.0 * results[a].correctRPA / results[a].totalFiles : 0;
        printf("| %5.1f%% |\n", overall);
    }

    printf("\nDone.\n");
    return 0;
}
