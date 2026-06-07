import { NativeEventEmitter } from 'react-native';
import NativePitchy from './NativePitchy';

const eventEmitter = new NativeEventEmitter(NativePitchy);

export type PitchyAlgorithm = 'ACF2+' | 'YIN' | 'MPM' | 'HPS' | 'AMDF' | 'RAPT';

export type PitchyConfig = {
  /**
   * The size of the buffer used to record audio.
   * @default 4096
   */
  bufferSize?: number;
  /**
   * The minimum volume required to start detecting pitch.
   * @default -60
   */
  minVolume?: number;
  /**
   * The algorithm used to detect pitch.
   * @default 'ACF2+'
   */
  algorithm?: PitchyAlgorithm;
  /**
   * Minimum detection confidence (0-1). Frames below this are reported as
   * unvoiced (pitch = -1) — a native-side gate so the JS consumer doesn't filter.
   * @default 0
   */
  minConfidence?: number;
};

export type PitchyEvent = {
  pitch: number;
  confidence: number;
  volume: number;
  /** Age of this sample vs the newest captured frame, in ms (≤ 0). Lets the
   *  consumer timestamp samples that arrive together in one overlap-hop burst. */
  tOffsetMs?: number;
  /** True capture wall-time of this sample (ms, same epoch as Date.now()),
   *  derived from the audio sample clock. Immune to delivery/bridge backlog —
   *  use this instead of Date.now()-at-receipt so the timeline can't drift late. */
  tCaptureMs?: number;
};

export type PitchyEventCallback = (event: PitchyEvent) => void;

const Pitchy = {
  init(config?: PitchyConfig) {
    return NativePitchy.configure({
      bufferSize: 4096,
      minVolume: -60,
      algorithm: 'ACF2+',
      minConfidence: 0,
      ...config,
    });
  },
  start(): Promise<boolean> {
    return NativePitchy.start();
  },
  stop(): Promise<boolean> {
    return NativePitchy.stop();
  },
  isRecording(): Promise<boolean> {
    return NativePitchy.isRecording();
  },
  addListener(callback: PitchyEventCallback) {
    return eventEmitter.addListener('onPitchDetected', callback);
  },
};

export default Pitchy;
