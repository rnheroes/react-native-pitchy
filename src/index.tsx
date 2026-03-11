import { NativeEventEmitter } from 'react-native';
import NativePitchy from './NativePitchy';

const eventEmitter = new NativeEventEmitter(NativePitchy);

export type PitchyAlgorithm = 'ACF2+';

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
};

export type PitchyEventCallback = ({ pitch }: { pitch: number }) => void;

const Pitchy = {
  init(config?: PitchyConfig) {
    return NativePitchy.configure({
      bufferSize: 4096,
      minVolume: -60,
      algorithm: 'ACF2+',
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
