# react-native-pitchy

A real-time pitch detection library for React Native with 6 classical algorithms. Cross-platform shared C++ core for iOS and Android.

## Features

- **6 algorithms**: ACF2+, YIN, MPM, HPS, AMDF, RAPT
- **New Architecture** (TurboModules) and Old Architecture support
- **Zero dependencies**: No ML frameworks, no ONNX — pure C++ signal processing
- **YIN-FFT**: O(N log N) classical pitch detection — low latency, sub-cent precision
- **MPM**: McLeod Pitch Method — bounded NSDF with reliable threshold selection
- **RAPT**: Two-stage pitch tracking — gold standard for speech processing
- **Cross-platform**: Shared C++ core for iOS and Android

## Installation

```sh
npm install react-native-pitchy
```

```sh
npx pod-install
```

Rebuild your app after installing the package.

## Permissions

Microphone permissions are required. Use [react-native-permissions](https://github.com/zoontek/react-native-permissions) or similar.

- **iOS**: `Microphone`
- **Android**: `RECORD_AUDIO`

## Usage

```tsx
import Pitchy from 'react-native-pitchy';

// Initialize with algorithm choice
Pitchy.init({
  algorithm: 'YIN',       // 'ACF2+' | 'YIN' | 'MPM' | 'HPS' | 'AMDF' | 'RAPT'
  bufferSize: 4096,
  minVolume: -60,
});

// Start detection
await Pitchy.start();

// Listen to pitch events
const subscription = Pitchy.addListener(({ pitch, confidence, volume }) => {
  console.log(`Pitch: ${pitch} Hz, Confidence: ${confidence}, Volume: ${volume} dB`);
});

// Stop detection
await Pitchy.stop();
subscription.remove();
```

## Algorithms

| Algorithm | Type | Best For | Latency | Accuracy |
|-----------|------|----------|---------|----------|
| `YIN` | Classical (FFT) | Guitar tuning | ~1.3ms | 86.0% RPA |
| `RAPT` | Two-stage NCCF | Speech processing | ~2.5ms | 83.7% RPA |
| `ACF2+` | Classical | General purpose | ~4.2ms | 81.4% RPA |
| `MPM` | Classical (NSDF) | Instrument tuning | ~1.4ms | 79.1% RPA |
| `AMDF` | Time domain | Low-power devices | ~1.7ms | 74.4% RPA |
| `HPS` | Frequency domain | Harmonic-rich signals | ~0.3ms | 55.8% RPA |

### YIN
FFT-accelerated autocorrelation with cumulative mean normalized difference. Best for instrument tuning where low latency and high precision on clean signals matter.

### MPM (McLeod Pitch Method)
Uses the Normalized Squared Difference Function (NSDF) bounded to [-1, 1], making threshold selection reliable. Key-maxima peak picking with parabolic interpolation. Excellent for musical instruments — used in many professional tuner apps.

### HPS (Harmonic Product Spectrum)
Frequency-domain approach that multiplies downsampled magnitude spectra to amplify the fundamental frequency. Fast and effective for harmonic-rich signals (guitar, bass, brass). Uses Hanning windowing and log-domain parabolic interpolation.

### AMDF (Average Magnitude Difference Function)
No-multiply pitch detection — uses absolute differences instead of multiplications, making it extremely fast on low-power hardware. Parabolic interpolation for sub-bin precision, with confidence derived from the depth of the AMDF minimum. Best for scenarios where CPU budget is tight.

### RAPT (Robust Algorithm for Pitch Tracking)
Two-stage pitch detection: first a coarse search on 4x downsampled signal for fast candidate generation, then refined search on the original signal near coarse candidates. Uses Normalized Cross-Correlation Function (NCCF) at both stages. Gold standard for speech processing — robust voiced/unvoiced detection.

## Benchmarks

Measured on Apple Silicon, 4096 samples @ 44.1kHz, 1000 iterations:

| Algorithm | ms/call | FPS | Clean RPA | Noisy RPA | Notes |
|-----------|---------|-----|-----------|-----------|-------|
| HPS | 0.31 | 3198 | 100%* | 42.1% | Fastest — frequency domain |
| YIN | 1.34 | 744 | 100% | 100% | FFT-accelerated CMND |
| MPM | 1.35 | 743 | 100% | 100% | FFT-based NSDF |
| AMDF | 1.69 | 591 | 100% | 100% | Time-domain, no FFT |
| RAPT | 2.48 | 403 | 100% | 100% | Two-stage NCCF |
| ACF2+ | 4.24 | 236 | 100% | 89.5% | Legacy time-domain |

\* HPS requires harmonic-rich signals (100% on instrument-like signals, 0% on pure sine).

All algorithms run well within real-time requirements (a 4096-sample buffer at 44.1kHz represents ~93ms of audio).

### Real-World Audio Accuracy

Tested on 43 real recordings: 32 orchestral instruments from [Philharmonia Orchestra](https://philharmonia.co.uk/resources/sound-samples/) (CC BY-SA 3.0) + 11 male singing voice from [Human Voice Dataset](https://github.com/vocobox/human-voice-dataset) (MIT):

| Algorithm | Cello | Guitar | Flute | Clarinet | Fr. Horn | Voice | Overall RPA |
|-----------|-------|--------|-------|----------|----------|-------|-------------|
| YIN       | 100%  | 100%   | 100%  | 100%     | 100%     | 73%   | 86.0%       |
| RAPT      | 100%  | 100%   | 100%  | 100%     | 100%     | 73%   | 83.7%       |
| ACF2+     | 100%  | 83%    | 100%  | 100%     | 100%     | 64%   | 81.4%       |
| MPM       | 100%  | 83%    | 100%  | 100%     | 100%     | 64%   | 79.1%       |
| AMDF      | 100%  | 67%    | 100%  | 100%     | 100%     | 73%   | 74.4%       |
| HPS       | 40%   | 83%    | 100%  | 50%      | 50%      | 45%   | 55.8%       |

RPA = Raw Pitch Accuracy (within 50 cents of ground truth). Voice failures are octave errors on higher male voice notes (A3+) where the second harmonic dominates the fundamental — a known limitation of classical pitch detectors.

## Need more accuracy?

**[react-native-pitchy-pro](https://appvision.dev/pitchy-pro)** adds 6 advanced algorithms including ML-powered pitch detection:

| Algorithm | Type | ms/call | Accuracy | Best For |
|-----------|------|---------|----------|----------|
| **CREPE** | ML (CNN) | — | ~97.8% RPA* | Highest accuracy, state-of-the-art |
| **PESTO** | ML (Lightweight) | — | ~96.1% RPA* | Fast ML, only 29K params |
| **SwiftF0** | ML (CNN) | — | ~93% RPA* | Noise-robust environments |
| **pYIN** | HMM + Viterbi | 1.27ms | 100% RPA | Vocal melody extraction |
| **Salience** | Harmonic Analysis | 0.39ms | 100% RPA | Instrument tuning with tracking |
| **SWIPE'** | ERB Matching | 0.11ms | 100% RPA | Noise-robust classical |

\* ML accuracy from published papers (Kim et al. 2018, Riou et al. 2023). ML latency depends on device and ONNX Runtime.

Pro features:
- 3 ML models via ONNX Runtime (CREPE, PESTO, SwiftF0)
- pYIN with HMM temporal smoothing for stable vocal tracking
- Salience with pitch tracking, octave correction, and ERB weighting
- SWIPE' with sawtooth harmonic template matching

[Learn more](https://appvision.dev/pitchy-pro) | [Contact for licensing](mailto:license@appvision.dev)

## API

### Types

- `PitchyAlgorithm`: `'ACF2+' | 'YIN' | 'MPM' | 'HPS' | 'AMDF' | 'RAPT'`
- `PitchyConfig`:
  - `bufferSize?`: Audio buffer size (default: 4096)
  - `minVolume?`: Minimum volume threshold in dB (default: -60)
  - `algorithm?`: Detection algorithm (default: `'ACF2+'`)
- `PitchyEvent`:
  - `pitch`: Detected pitch in Hz (-1 if no pitch)
  - `confidence`: Detection confidence (0.0 - 1.0)
  - `volume`: Input volume in dB
- `PitchyEventCallback`: `(event: PitchyEvent) => void`

### Methods

- `Pitchy.init(config?)` — Initialize with optional configuration
- `Pitchy.start()` — Start pitch detection (Promise)
- `Pitchy.stop()` — Stop pitch detection (Promise)
- `Pitchy.isRecording()` — Check if running (Promise\<boolean\>)
- `Pitchy.addListener(callback)` — Listen to `onPitchDetected` events

## Examples

See the [example app](example) for a working debug implementation with volume metering, raw data display, and algorithm switching.

## Contributing

See the [contributing guide](CONTRIBUTING.md).

## License

MIT — Copyright (c) 2024 App Vision
