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
| `ACF2+` | Classical | General purpose | ~4.0ms | Good |
| `YIN` | Classical (FFT) | Guitar tuning | ~1.3ms | Sub-cent on clean signals |
| `MPM` | Classical (NSDF) | Instrument tuning | ~1.2ms | Sub-cent (±0.02 cents) |
| `HPS` | Frequency domain | Harmonic-rich signals | ~0.3ms | Good on strings/brass |
| `AMDF` | Time domain | Low-power devices | ~1.6ms | ±2 cents on clean signals |
| `RAPT` | Two-stage NCCF | Speech processing | ~2.0ms | Very robust for speech |

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

| Algorithm | ms/call | FPS | Notes |
|-----------|---------|-----|-------|
| HPS | 0.31 | 3228 | Fastest — frequency domain |
| MPM | 1.24 | 806 | FFT-based NSDF |
| YIN | 1.26 | 793 | FFT-accelerated CMND |
| AMDF | 1.60 | 625 | Time-domain, no FFT |
| RAPT | 2.00 | 500 | Two-stage NCCF |
| ACF2+ | 4.04 | 248 | Legacy time-domain |

All algorithms run well within real-time requirements (a 4096-sample buffer at 44.1kHz represents ~93ms of audio).

## Need more accuracy?

**[react-native-pitchy-pro](https://appvision.dev/pitchy-pro)** adds 6 advanced algorithms including ML-powered pitch detection:

| Algorithm | Type | Accuracy | Best For |
|-----------|------|----------|----------|
| **CREPE** | ML (CNN) | ~97.8% RPA | Highest accuracy, state-of-the-art |
| **PESTO** | ML (Lightweight) | ~96.1% RPA | Fast ML, only 29K params |
| **SwiftF0** | ML (CNN) | ~93% RPA | Noise-robust environments |
| **pYIN** | HMM + Viterbi | ~92% RPA | Vocal melody extraction |
| **Salience** | Harmonic Analysis | ~88% RPA | Instrument tuning with tracking |
| **SWIPE'** | ERB Matching | ~90% RPA | Noise-robust classical |

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
