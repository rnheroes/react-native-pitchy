import { NativeModules, NativeEventEmitter, Platform } from 'react-native';

const LINKING_ERROR =
  `The package 'react-native-pitchy' doesn't seem to be linked. Make sure: \n\n` +
  Platform.select({ ios: "- You have run 'pod install'\n", default: '' }) +
  '- You rebuilt the app after installing the package\n' +
  '- You are not using Expo Go\n';

const PitchyNativeModule = NativeModules.Pitchy
  ? NativeModules.Pitchy
  : new Proxy(
      {},
      {
        get() {
          throw new Error(LINKING_ERROR);
        },
      }
    );

const eventEmitter = new NativeEventEmitter(PitchyNativeModule);

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
    return PitchyNativeModule.configure({
      bufferSize: 4096,
      minVolume: -60,
      algorithm: 'ACF2+',
      ...config,
    });
  },
  start(): Promise<void> {
    return PitchyNativeModule.start();
  },
  stop(): Promise<void> {
    return PitchyNativeModule.stop();
  },
  isRecording(): Promise<boolean> {
    return PitchyNativeModule.isRecording();
  },
  addListener(callback: PitchyEventCallback) {
    return eventEmitter.addListener('onPitchDetected', callback);
  },
};

export default Pitchy;
