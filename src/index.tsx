import { NativeEventEmitter } from 'react-native';
import NativePitchy from './NativePitchy';

const eventEmitter = new NativeEventEmitter(NativePitchy);

// A native audio operation should settle in a fraction of a second. Keep a
// generous ceiling for slow route hand-offs, but never let an orphaned
// TurboModule promise strand a caller (and its microphone-starting UI)
// indefinitely. The shorter reconciliation timeout is only used to inspect
// native state after an operation failed or timed out.
const LIFECYCLE_TIMEOUT_MS = 4000;
const RECONCILE_TIMEOUT_MS = 750;

type LifecycleOperation = 'start' | 'stop' | 'isRecording';

class PitchyLifecycleTimeoutError extends Error {
  readonly code = 'lifecycle_timeout';
  readonly operation: LifecycleOperation;

  constructor(operation: LifecycleOperation, timeoutMs: number) {
    super(`Pitchy ${operation} did not settle within ${timeoutMs}ms`);
    this.name = 'PitchyLifecycleTimeoutError';
    this.operation = operation;
  }
}

function withLifecycleTimeout<T>(
  operation: LifecycleOperation,
  promise: Promise<T>,
  timeoutMs = LIFECYCLE_TIMEOUT_MS
): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (settled) return;
      settled = true;
      reject(new PitchyLifecycleTimeoutError(operation, timeoutMs));
    }, timeoutMs);

    promise.then(
      (value) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(value);
      },
      (error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        reject(error);
      }
    );
  });
}

async function reconcileRecordingState(): Promise<boolean | null> {
  try {
    return await withLifecycleTimeout(
      'isRecording',
      NativePitchy.isRecording(),
      RECONCILE_TIMEOUT_MS
    );
  } catch {
    return null;
  }
}

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
  async start(): Promise<boolean> {
    try {
      return await withLifecycleTimeout('start', NativePitchy.start());
    } catch (error) {
      // A callback can be lost at the TurboModule boundary even though the
      // native operation completed. Trust the observable state in that case.
      // This also makes start idempotent with older native Pitchy binaries that
      // reject with already_recording.
      if ((await reconcileRecordingState()) === true) return true;
      throw error;
    }
  },
  async stop(): Promise<boolean> {
    try {
      return await withLifecycleTimeout('stop', NativePitchy.stop());
    } catch (error) {
      // Stopping an already-stopped engine has achieved the caller's intent.
      // Reconcile instead of turning overlapping cleanup calls into failures.
      if ((await reconcileRecordingState()) === false) return true;
      throw error;
    }
  },
  isRecording(): Promise<boolean> {
    return withLifecycleTimeout('isRecording', NativePitchy.isRecording());
  },
  addListener(callback: PitchyEventCallback) {
    return eventEmitter.addListener('onPitchDetected', callback);
  },
};

export default Pitchy;
