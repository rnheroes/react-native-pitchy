const mockConfigure = jest.fn();
const mockStart = jest.fn<Promise<boolean>, []>();
const mockStop = jest.fn<Promise<boolean>, []>();
const mockIsRecording = jest.fn<Promise<boolean>, []>();

jest.mock('../NativePitchy', () => ({
  __esModule: true,
  default: {
    configure: (config: object) => mockConfigure(config),
    start: () => mockStart(),
    stop: () => mockStop(),
    isRecording: () => mockIsRecording(),
    addListener: jest.fn(),
    removeListeners: jest.fn(),
  },
}));

import Pitchy from '../index';

describe('Pitchy lifecycle', () => {
  beforeEach(() => {
    jest.useFakeTimers();
    jest.clearAllMocks();
    mockStart.mockResolvedValue(true);
    mockStop.mockResolvedValue(true);
    mockIsRecording.mockResolvedValue(false);
  });

  afterEach(() => {
    jest.useRealTimers();
  });

  test('passes defaults and caller options to native configure', () => {
    Pitchy.init({ algorithm: 'MPM', bufferSize: 2048 });

    expect(mockConfigure).toHaveBeenCalledWith({
      bufferSize: 2048,
      minVolume: -60,
      algorithm: 'MPM',
      minConfidence: 0,
    });
  });

  test('resolves an orphaned native start when native state is recording', async () => {
    mockStart.mockReturnValue(new Promise(() => {}));
    mockIsRecording.mockResolvedValue(true);

    const start = Pitchy.start();
    await jest.advanceTimersByTimeAsync(4000);

    await expect(start).resolves.toBe(true);
    expect(mockIsRecording).toHaveBeenCalledTimes(1);
  });

  test('rejects an orphaned native start when recording never began', async () => {
    mockStart.mockReturnValue(new Promise(() => {}));

    const result = Pitchy.start().catch((error) => error);
    await jest.advanceTimersByTimeAsync(4000);

    await expect(result).resolves.toMatchObject({
      code: 'lifecycle_timeout',
      operation: 'start',
    });
  });

  test('treats an already-stopped native engine as a successful stop', async () => {
    mockStop.mockRejectedValue(new Error('Not recording'));
    mockIsRecording.mockResolvedValue(false);

    await expect(Pitchy.stop()).resolves.toBe(true);
  });

  test('recovers when a native stop callback is orphaned after stopping', async () => {
    mockStop.mockReturnValue(new Promise(() => {}));
    mockIsRecording.mockResolvedValue(false);

    const stop = Pitchy.stop();
    await jest.advanceTimersByTimeAsync(4000);

    await expect(stop).resolves.toBe(true);
  });

  test('rejects an orphaned recording-state query instead of hanging', async () => {
    mockIsRecording.mockReturnValue(new Promise(() => {}));

    const result = Pitchy.isRecording().catch((error) => error);
    await jest.advanceTimersByTimeAsync(4000);

    await expect(result).resolves.toMatchObject({
      code: 'lifecycle_timeout',
      operation: 'isRecording',
    });
  });

  test('preserves a native stop failure while the engine is still recording', async () => {
    const nativeError = new Error('Could not stop');
    mockStop.mockRejectedValue(nativeError);
    mockIsRecording.mockResolvedValue(true);

    await expect(Pitchy.stop()).rejects.toBe(nativeError);
  });
});
