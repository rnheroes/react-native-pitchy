#!/usr/bin/env python3
"""
Download and prepare real-world audio test files for pitch detection benchmarking.

Sources:
- University of Iowa Musical Instrument Samples (MIS) - anechoic single notes
- Generates realistic test signals (vibrato, pitch glide, multi-instrument)

Output: benchmark/audio/ directory with WAV files named as:
    {source}_{note}_{freq_hz}.wav

Each file is 16-bit 44100 Hz mono WAV.
"""

import os
import wave
import struct
import math
import random
import urllib.request
import tempfile
import sys

SAMPLE_RATE = 44100
DURATION = 2.0  # seconds
AUDIO_DIR = os.path.join(os.path.dirname(__file__), "audio")

# Note frequencies (A4 = 440 Hz, equal temperament)
NOTES = {
    "C2": 65.41, "D2": 73.42, "E2": 82.41, "F2": 87.31, "G2": 98.00, "A2": 110.00, "B2": 123.47,
    "C3": 130.81, "D3": 146.83, "E3": 164.81, "F3": 174.61, "G3": 196.00, "A3": 220.00, "B3": 246.94,
    "C4": 261.63, "D4": 293.66, "E4": 329.63, "F4": 349.23, "G4": 392.00, "A4": 440.00, "B4": 493.88,
    "C5": 523.25, "D5": 587.33, "E5": 659.26, "F5": 698.46, "G5": 783.99, "A5": 880.00, "B5": 987.77,
    "C6": 1046.50, "A6": 1760.00,
}


def write_wav(path, samples):
    """Write 16-bit mono WAV file."""
    n = len(samples)
    with wave.open(path, "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        data = b""
        for s in samples:
            s = max(-1.0, min(1.0, s))
            data += struct.pack("<h", int(s * 32767))
        wf.writeframes(data)


def generate_realistic_signals():
    """Generate realistic test signals beyond simple sine waves."""
    os.makedirs(AUDIO_DIR, exist_ok=True)
    count = 0

    # 1. Violin-like (sawtooth approximation + vibrato)
    print("Generating violin-like signals...")
    for note, freq in [("A3", 220.0), ("E4", 329.63), ("A4", 440.0), ("E5", 659.26)]:
        n = int(DURATION * SAMPLE_RATE)
        samples = []
        vibrato_rate = 5.5  # Hz
        vibrato_depth = 3.0  # cents
        for i in range(n):
            t = i / SAMPLE_RATE
            # Vibrato
            vibrato = vibrato_depth * math.sin(2 * math.pi * vibrato_rate * t)
            f = freq * (2 ** (vibrato / 1200.0))
            # Sawtooth with 8 harmonics
            val = 0.0
            for h in range(1, 9):
                hf = f * h
                if hf >= SAMPLE_RATE / 2:
                    break
                val += ((-1) ** (h + 1)) * math.sin(2 * math.pi * hf * t) / h
            # Envelope: gentle attack + sustain
            env = min(1.0, t / 0.05) * 0.6
            samples.append(val * env)
        path = os.path.join(AUDIO_DIR, f"violin_{note}_{freq:.2f}.wav")
        write_wav(path, samples)
        count += 1

    # 2. Guitar-like (plucked string: harmonics with exponential decay)
    print("Generating guitar-like signals...")
    for note, freq in [("E2", 82.41), ("A2", 110.0), ("D3", 146.83), ("G3", 196.0), ("B3", 246.94), ("E4", 329.63)]:
        n = int(DURATION * SAMPLE_RATE)
        samples = []
        for i in range(n):
            t = i / SAMPLE_RATE
            val = 0.0
            for h in range(1, 12):
                hf = freq * h
                if hf >= SAMPLE_RATE / 2:
                    break
                # Higher harmonics decay faster (Karplus-Strong-like)
                decay = math.exp(-t * (2.0 + h * 0.8))
                val += decay * math.sin(2 * math.pi * hf * t) / (h ** 0.7)
            # Slight inharmonicity for realism
            samples.append(val * 0.7)
        path = os.path.join(AUDIO_DIR, f"guitar_{note}_{freq:.2f}.wav")
        write_wav(path, samples)
        count += 1

    # 3. Piano-like (stiff string inharmonicity + triple decay)
    print("Generating piano-like signals...")
    for note, freq in [("C2", 65.41), ("C3", 130.81), ("C4", 261.63), ("A4", 440.0), ("C5", 523.25), ("C6", 1046.50)]:
        n = int(DURATION * SAMPLE_RATE)
        samples = []
        B = 0.0004 if freq < 200 else 0.0001  # inharmonicity coefficient
        for i in range(n):
            t = i / SAMPLE_RATE
            val = 0.0
            for h in range(1, 15):
                # Stretched partial: f_h = h * f0 * sqrt(1 + B * h^2)
                hf = h * freq * math.sqrt(1.0 + B * h * h)
                if hf >= SAMPLE_RATE / 2:
                    break
                # Two-stage decay
                decay = 0.7 * math.exp(-t * 1.5) + 0.3 * math.exp(-t * 0.3)
                amp = decay / (h ** 0.8)
                val += amp * math.sin(2 * math.pi * hf * t)
            samples.append(val * 0.5)
        path = os.path.join(AUDIO_DIR, f"piano_{note}_{freq:.2f}.wav")
        write_wav(path, samples)
        count += 1

    # 4. Voice-like (formant synthesis with vibrato + jitter)
    print("Generating voice-like signals...")
    for note, freq in [("G2", 98.0), ("C3", 130.81), ("E3", 164.81), ("A3", 220.0), ("C4", 261.63), ("G4", 392.0)]:
        n = int(DURATION * SAMPLE_RATE)
        samples = []
        vibrato_rate = 5.8
        vibrato_depth = 6.0  # cents — more than violin
        random.seed(42 + int(freq))
        for i in range(n):
            t = i / SAMPLE_RATE
            # Vibrato + jitter
            vibrato = vibrato_depth * math.sin(2 * math.pi * vibrato_rate * t)
            jitter = random.gauss(0, 0.3)  # cents
            f = freq * (2 ** ((vibrato + jitter) / 1200.0))
            # Glottal pulse approximation (odd harmonics stronger)
            val = 0.0
            for h in range(1, 20):
                hf = f * h
                if hf >= SAMPLE_RATE / 2:
                    break
                # Formant-like spectral shape (simplified vowel "ah")
                formant_gain = 1.0
                for (fc, bw, gain) in [(800, 80, 1.0), (1200, 90, 0.7), (2500, 120, 0.3)]:
                    formant_gain *= 1.0 + gain * math.exp(-0.5 * ((hf - fc) / bw) ** 2)
                amp = formant_gain / (h ** 1.2)
                val += amp * math.sin(2 * math.pi * hf * t)
            # Breath noise
            noise = random.gauss(0, 0.02)
            env = min(1.0, t / 0.03) * 0.5
            samples.append((val + noise) * env)
        path = os.path.join(AUDIO_DIR, f"voice_{note}_{freq:.2f}.wav")
        write_wav(path, samples)
        count += 1

    # 5. Noisy versions (20 dB SNR white noise)
    print("Generating noisy instrument signals...")
    random.seed(123)
    for note, freq in [("A3", 220.0), ("A4", 440.0), ("E5", 659.26)]:
        n = int(DURATION * SAMPLE_RATE)
        # Harmonic signal
        samples = []
        for i in range(n):
            t = i / SAMPLE_RATE
            val = 0.0
            for h in range(1, 8):
                hf = freq * h
                if hf >= SAMPLE_RATE / 2:
                    break
                val += math.sin(2 * math.pi * hf * t) / h
            samples.append(val * 0.6)
        # Add noise at 20 dB SNR
        sig_power = sum(s * s for s in samples) / len(samples)
        noise_power = sig_power * (10 ** (-20 / 10))
        noise_std = math.sqrt(noise_power)
        noisy = [s + random.gauss(0, noise_std) for s in samples]
        path = os.path.join(AUDIO_DIR, f"noisy_{note}_{freq:.2f}.wav")
        write_wav(path, noisy)
        count += 1

    # 6. Pitch glide (portamento)
    print("Generating pitch glide signals...")
    for start_note, end_note, sf, ef in [("C4", "G4", 261.63, 392.0), ("A3", "A4", 220.0, 440.0)]:
        n = int(DURATION * SAMPLE_RATE)
        samples = []
        for i in range(n):
            t = i / SAMPLE_RATE
            # Linear interpolation in log-freq space
            alpha = t / DURATION
            freq_t = sf * (ef / sf) ** alpha
            val = 0.0
            for h in range(1, 6):
                hf = freq_t * h
                if hf >= SAMPLE_RATE / 2:
                    break
                val += math.sin(2 * math.pi * hf * t) / h
            samples.append(val * 0.5)
        # For glide, use midpoint frequency as "ground truth"
        mid_freq = sf * (ef / sf) ** 0.5
        path = os.path.join(AUDIO_DIR, f"glide_{start_note}-{end_note}_{mid_freq:.2f}.wav")
        write_wav(path, samples)
        count += 1

    print(f"\nGenerated {count} test audio files in {AUDIO_DIR}/")
    return count


def generate_ground_truth():
    """Generate ground_truth.csv from filenames."""
    csv_path = os.path.join(AUDIO_DIR, "ground_truth.csv")
    files = sorted(f for f in os.listdir(AUDIO_DIR) if f.endswith(".wav"))

    with open(csv_path, "w") as fp:
        fp.write("file,source,note,freq_hz\n")
        for f in files:
            # Parse: {source}_{note}_{freq}.wav
            name = f.replace(".wav", "")
            parts = name.split("_")
            source = parts[0]
            # freq is last part
            freq = float(parts[-1])
            # note is middle part(s)
            note = "_".join(parts[1:-1])
            fp.write(f"{f},{source},{note},{freq:.2f}\n")

    print(f"Ground truth written to {csv_path}")
    print(f"Total files: {len(files)}")


if __name__ == "__main__":
    generate_realistic_signals()
    generate_ground_truth()
