#include "pitch-detector.h"
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr int BUFFER_SIZE = 4096;
static constexpr double SAMPLE_RATE = 44100.0;

static std::vector<double> generateSine(double freq, double amp = 0.8) {
    std::vector<double> buf(BUFFER_SIZE);
    for (int i = 0; i < BUFFER_SIZE; i++)
        buf[i] = amp * std::sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    return buf;
}

static std::vector<double> generateHarmonic(double freq, double amp = 0.8) {
    std::vector<double> buf(BUFFER_SIZE, 0.0);
    double amps[] = {1.0, 0.5, 0.25, 0.125};
    for (int h = 0; h < 4; h++) {
        double f = freq * (h + 1);
        if (f >= SAMPLE_RATE / 2.0) break;
        for (int i = 0; i < BUFFER_SIZE; i++)
            buf[i] += amp * amps[h] * std::sin(2.0 * M_PI * f * i / SAMPLE_RATE);
    }
    return buf;
}

int main() {
    double freqs[] = {65.41, 110.0, 220.0, 440.0, 880.0, 1760.0, 2093.0};
    int n = 7;

    pitchy::PitchDetector det;
    det.setAlgorithm(pitchy::Algorithm::RAPT);

    printf("=== RAPT Debug ===\n\n");
    printf("%-10s %-12s %-12s %-10s\n", "Expected", "Detected", "Cents Err", "Signal");
    printf("%-10s %-12s %-12s %-10s\n", "─────────", "──────────", "──────────", "──────");

    for (int i = 0; i < n; i++) {
        auto buf = generateSine(freqs[i]);
        auto r = det.detect(buf, SAMPLE_RATE, -60.0);
        double cents = (r.pitch > 0) ? 1200.0 * std::log2(r.pitch / freqs[i]) : 9999.0;
        printf("%8.1f Hz  %8.1f Hz  %+8.1f ¢   sine\n", freqs[i], r.pitch, cents);
    }
    printf("\n");
    for (int i = 0; i < n; i++) {
        auto buf = generateHarmonic(freqs[i]);
        auto r = det.detect(buf, SAMPLE_RATE, -60.0);
        double cents = (r.pitch > 0) ? 1200.0 * std::log2(r.pitch / freqs[i]) : 9999.0;
        printf("%8.1f Hz  %8.1f Hz  %+8.1f ¢   harmonic\n", freqs[i], r.pitch, cents);
    }

    // Also test what lag range looks like
    printf("\n=== Lag analysis ===\n");
    for (int i = 0; i < n; i++) {
        int expectedLag = (int)(SAMPLE_RATE / freqs[i]);
        int coarseLag = expectedLag / 4;
        printf("%.0f Hz -> lag=%d, coarseLag=%d\n", freqs[i], expectedLag, coarseLag);
    }

    return 0;
}
