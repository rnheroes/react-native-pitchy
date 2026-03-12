#!/usr/bin/env python3
"""
Download real instrument and voice recordings for pitch detection benchmarking.

Sources:
- Philharmonia Orchestra Sound Samples (CC BY-SA 3.0)
  Mirror: https://github.com/skratchdot/philharmonia-samples
- Human Voice Dataset (MIT License)
  https://github.com/vocobox/human-voice-dataset

Instrument files are converted from MP3 to WAV using ffmpeg.
Voice files are already WAV (mono, may need resampling to 44.1kHz).

Usage:
    python3 download_real_audio.py
"""

import os
import subprocess
import urllib.request
import sys

AUDIO_DIR = os.path.join(os.path.dirname(__file__), "audio_real")
BASE_URL = "https://skratchdot.github.io/philharmonia-samples/audio"

# Note frequencies (A4 = 440 Hz, equal temperament)
NOTE_FREQ = {
    "C2": 65.41, "D2": 73.42, "E2": 82.41, "F2": 87.31, "G2": 98.00, "A2": 110.00, "B2": 123.47,
    "C3": 130.81, "D3": 146.83, "E3": 164.81, "F3": 174.61, "G3": 196.00, "A3": 220.00, "B3": 246.94,
    "C4": 261.63, "D4": 293.66, "E4": 329.63, "F4": 349.23, "G4": 392.00, "A4": 440.00, "B4": 493.88,
    "C5": 523.25, "D5": 587.33, "E5": 659.26, "F5": 698.46, "G5": 783.99, "A5": 880.00, "B5": 987.77,
    "C6": 1046.50, "D6": 1174.66, "E6": 1318.51, "A6": 1760.00,
}

# Curated instrument samples: (folder_name, filename_prefix, note, duration, dynamic, articulation)
# Selected for: forte dynamic, normal articulation, longest available duration
SAMPLES = [
    # Cello (bowed string) — low to high
    ("cello", "cello", "C2", "15", "forte", "arco-normal"),
    ("cello", "cello", "A2", "15", "forte", "arco-normal"),
    ("cello", "cello", "D3", "15", "forte", "arco-normal"),
    ("cello", "cello", "A3", "15", "forte", "arco-normal"),
    ("cello", "cello", "A4", "15", "forte", "arco-normal"),

    # Guitar (plucked string) — standard tuning notes
    ("guitar", "guitar", "E2", "very-long", "forte", "normal"),
    ("guitar", "guitar", "A2", "very-long", "forte", "normal"),
    ("guitar", "guitar", "D3", "very-long", "forte", "normal"),
    ("guitar", "guitar", "G3", "very-long", "forte", "normal"),
    ("guitar", "guitar", "B3", "very-long", "forte", "normal"),
    ("guitar", "guitar", "E4", "very-long", "forte", "normal"),

    # Flute (woodwind) — mid to high
    ("flute", "flute", "C5", "15", "forte", "normal"),
    ("flute", "flute", "A5", "15", "forte", "normal"),
    ("flute", "flute", "D6", "15", "forte", "normal"),

    # Clarinet (woodwind) — wide range
    ("clarinet", "clarinet", "D3", "15", "forte", "normal"),
    ("clarinet", "clarinet", "A3", "15", "forte", "normal"),
    ("clarinet", "clarinet", "D4", "15", "forte", "normal"),
    ("clarinet", "clarinet", "A4", "15", "forte", "normal"),

    # Oboe (woodwind)
    ("oboe", "oboe", "C4", "15", "forte", "normal"),
    ("oboe", "oboe", "A4", "15", "forte", "normal"),
    ("oboe", "oboe", "D5", "15", "forte", "normal"),

    # French horn (brass)
    ("french horn", "french-horn", "A2", "1", "mezzo-forte", "normal"),
    ("french horn", "french-horn", "D3", "1", "mezzo-forte", "normal"),
    ("french horn", "french-horn", "A3", "1", "mezzo-forte", "normal"),
    ("french horn", "french-horn", "D4", "1", "mezzo-forte", "normal"),

    # Saxophone (reed)
    ("saxophone", "saxophone", "A3", "15", "forte", "normal"),
    ("saxophone", "saxophone", "D4", "15", "forte", "normal"),
    ("saxophone", "saxophone", "A4", "15", "forte", "normal"),

    # Trombone (brass) — low to mid (low notes only have mezzo-forte)
    ("trombone", "trombone", "E2", "05", "forte", "normal"),
    ("trombone", "trombone", "A2", "15", "mezzo-forte", "normal"),
    ("trombone", "trombone", "D3", "15", "mezzo-forte", "normal"),
    ("trombone", "trombone", "A3", "15", "forte", "normal"),
]

# Voice samples from vocobox/human-voice-dataset (male singer, single sustained notes)
# Format: (github_filename, note_name) — files are already WAV
VOICE_BASE_URL = "https://raw.githubusercontent.com/vocobox/human-voice-dataset/master/data/voices/martin/notes/exports/mono"
VOICE_SAMPLES = [
    ("D2.wav", "D2"),
    ("E2.wav", "E2"),
    ("G2-2.wav", "G2"),
    ("A3.wav", "A3"),
    ("B3.wav", "B3"),
    ("C3.wav", "C3"),
    ("D3.wav", "D3"),
    ("E3.wav", "E3"),
    ("F3.wav", "F3"),
    ("G3.wav", "G3"),
    ("A4.wav", "A4"),
]


def download_and_convert():
    """Download MP3 files and convert to WAV."""
    os.makedirs(AUDIO_DIR, exist_ok=True)

    success = 0
    fail = 0

    for folder, prefix, note, dur, dynamic, artic in SAMPLES:
        filename = f"{prefix}_{note}_{dur}_{dynamic}_{artic}.mp3"
        url_folder = folder.replace(" ", "%20")
        url = f"{BASE_URL}/{url_folder}/{filename}"

        out_name = f"{prefix}_{note}_{NOTE_FREQ[note]:.2f}.wav"
        out_path = os.path.join(AUDIO_DIR, out_name)

        if os.path.exists(out_path):
            print(f"  SKIP (exists): {out_name}")
            success += 1
            continue

        mp3_path = os.path.join(AUDIO_DIR, filename)

        # Download
        print(f"  Downloading {filename}...", end=" ", flush=True)
        try:
            urllib.request.urlretrieve(url, mp3_path)
        except Exception as e:
            print(f"FAIL ({e})")
            fail += 1
            continue

        # Convert MP3 → WAV (44.1kHz, 16-bit, mono)
        try:
            subprocess.run(
                ["ffmpeg", "-i", mp3_path, "-ar", "44100", "-ac", "1",
                 "-sample_fmt", "s16", out_path, "-y"],
                capture_output=True, check=True,
            )
            os.remove(mp3_path)
            print(f"OK → {out_name}")
            success += 1
        except subprocess.CalledProcessError as e:
            print(f"CONVERT FAIL: {e.stderr.decode()[-200:]}")
            if os.path.exists(mp3_path):
                os.remove(mp3_path)
            fail += 1

    print(f"\nInstruments: {success} files ({fail} failed)")
    return success


def download_voice_samples():
    """Download voice WAV files and resample to 44.1kHz."""
    os.makedirs(AUDIO_DIR, exist_ok=True)

    success = 0
    fail = 0

    for github_file, note in VOICE_SAMPLES:
        freq = NOTE_FREQ[note]
        out_name = f"voice_{note}_{freq:.2f}.wav"
        out_path = os.path.join(AUDIO_DIR, out_name)

        if os.path.exists(out_path):
            print(f"  SKIP (exists): {out_name}")
            success += 1
            continue

        url = f"{VOICE_BASE_URL}/{urllib.request.quote(github_file)}"
        tmp_path = os.path.join(AUDIO_DIR, f"_tmp_{github_file}")

        print(f"  Downloading {github_file}...", end=" ", flush=True)
        try:
            urllib.request.urlretrieve(url, tmp_path)
        except Exception as e:
            print(f"FAIL ({e})")
            fail += 1
            continue

        # Resample to 44.1kHz 16-bit mono
        try:
            subprocess.run(
                ["ffmpeg", "-i", tmp_path, "-ar", "44100", "-ac", "1",
                 "-sample_fmt", "s16", out_path, "-y"],
                capture_output=True, check=True,
            )
            os.remove(tmp_path)
            print(f"OK → {out_name}")
            success += 1
        except subprocess.CalledProcessError as e:
            print(f"CONVERT FAIL: {e.stderr.decode()[-200:]}")
            if os.path.exists(tmp_path):
                os.remove(tmp_path)
            fail += 1

    print(f"\nVoice: {success} files ({fail} failed)")
    return success


def generate_ground_truth():
    """Generate ground_truth.csv from WAV filenames."""
    csv_path = os.path.join(AUDIO_DIR, "ground_truth.csv")
    files = sorted(f for f in os.listdir(AUDIO_DIR) if f.endswith(".wav"))

    with open(csv_path, "w") as fp:
        fp.write("file,source,note,freq_hz\n")
        for f in files:
            name = f.replace(".wav", "")
            parts = name.split("_")
            source = parts[0]
            # For "french-horn", use full hyphenated name
            freq = float(parts[-1])
            note = parts[-2]  # e.g., A3, D4, etc.
            fp.write(f"{f},{source},{note},{freq:.2f}\n")

    print(f"Ground truth: {csv_path} ({len(files)} files)")


if __name__ == "__main__":
    print("=== Downloading Real Audio Test Files ===\n")

    print("--- Philharmonia Orchestra Instruments (CC BY-SA 3.0) ---")
    download_and_convert()

    print("\n--- Human Voice Dataset (MIT) ---")
    download_voice_samples()

    generate_ground_truth()
