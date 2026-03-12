import { useEffect, useState, useCallback, useRef } from 'react';
import {
  StyleSheet,
  Text,
  View,
  TouchableOpacity,
  SafeAreaView,
  ScrollView,
} from 'react-native';
import Pitchy, { type PitchyAlgorithm, type PitchyEvent } from 'react-native-pitchy';

import { useMicrophonePermission } from './hooks/use-microphone-permission';

const ALGORITHMS: { key: PitchyAlgorithm; label: string; desc: string }[] = [
  { key: 'ACF2+', label: 'ACF2+', desc: 'Autocorrelation (legacy)' },
  { key: 'YIN', label: 'YIN', desc: 'FFT-optimized (guitar tuning)' },
  { key: 'MPM', label: 'MPM', desc: 'McLeod Pitch Method (instruments)' },
  { key: 'HPS', label: 'HPS', desc: 'Harmonic Product Spectrum' },
  { key: 'AMDF', label: 'AMDF', desc: 'No-multiply (ultra-fast)' },
  { key: 'pYIN', label: 'pYIN', desc: 'Probabilistic YIN + HMM' },
  { key: 'Salience', label: 'Salience', desc: 'Harmonic salience (tuner)' },
  { key: 'SwiftF0', label: 'SwiftF0', desc: 'ML / CNN (vocal training)' },
];

function frequencyToNote(freq: number): string {
  if (freq <= 0) return '—';
  const noteNames = [
    'C', 'C#', 'D', 'D#', 'E', 'F',
    'F#', 'G', 'G#', 'A', 'A#', 'B',
  ];
  const semitone = 12 * Math.log2(freq / 440) + 69;
  const noteIndex = Math.round(semitone) % 12;
  const octave = Math.floor(Math.round(semitone) / 12) - 1;
  const cents = Math.round((semitone - Math.round(semitone)) * 100);
  return `${noteNames[noteIndex]}${octave} ${cents >= 0 ? '+' : ''}${cents}c`;
}

function clamp(v: number, min: number, max: number) {
  return Math.max(min, Math.min(max, v));
}

export default function App() {
  const permissionGranted = useMicrophonePermission();

  const [event, setEvent] = useState<PitchyEvent>({ pitch: -1, confidence: 0, volume: -100 });
  const [algorithm, setAlgorithm] = useState<PitchyAlgorithm>('YIN');
  const [isRunning, setIsRunning] = useState(false);
  const [eventCount, setEventCount] = useState(0);
  const [pitchCount, setPitchCount] = useState(0);
  const eventCountRef = useRef(0);
  const pitchCountRef = useRef(0);

  const startDetection = useCallback(
    (algo: PitchyAlgorithm) => {
      Pitchy.stop().catch(() => {});
      setIsRunning(false);
      setEvent({ pitch: -1, confidence: 0, volume: -100 });
      eventCountRef.current = 0;
      pitchCountRef.current = 0;
      setEventCount(0);
      setPitchCount(0);

      setTimeout(() => {
        Pitchy.init({ algorithm: algo, minVolume: -60, bufferSize: 4096 });
        Pitchy.start().then(() => setIsRunning(true));
      }, 300);
    },
    []
  );

  useEffect(() => {
    const subscription = Pitchy.addListener((data: PitchyEvent) => {
      setEvent(data);
      eventCountRef.current += 1;
      setEventCount(eventCountRef.current);
      if (data.pitch > 0) {
        pitchCountRef.current += 1;
        setPitchCount(pitchCountRef.current);
      }
    });
    return () => subscription.remove();
  }, []);

  useEffect(() => {
    if (!permissionGranted) return;
    startDetection(algorithm);
    return () => {
      Pitchy.stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [permissionGranted]);

  const switchAlgorithm = (algo: PitchyAlgorithm) => {
    setAlgorithm(algo);
    if (permissionGranted) {
      startDetection(algo);
    }
  };

  const volumePercent = clamp((event.volume + 100) / 100, 0, 1);
  const hasPitch = event.pitch > 0;

  return (
    <SafeAreaView style={styles.container}>
      <ScrollView contentContainerStyle={styles.scroll}>
        <Text style={styles.title}>react-native-pitchy debug</Text>

        {/* Status bar */}
        <View style={styles.statusBar}>
          <View style={styles.statusItem}>
            <View style={[styles.dot, { backgroundColor: permissionGranted ? '#4CAF50' : '#F44336' }]} />
            <Text style={styles.statusLabel}>Mic</Text>
          </View>
          <View style={styles.statusItem}>
            <View style={[styles.dot, { backgroundColor: isRunning ? '#4CAF50' : '#999' }]} />
            <Text style={styles.statusLabel}>Engine</Text>
          </View>
          <View style={styles.statusItem}>
            <View style={[styles.dot, { backgroundColor: hasPitch ? '#4CAF50' : '#FF9800' }]} />
            <Text style={styles.statusLabel}>Pitch</Text>
          </View>
        </View>

        {/* Main display */}
        <View style={styles.display}>
          <Text style={styles.frequency}>
            {hasPitch ? `${event.pitch.toFixed(1)} Hz` : '—'}
          </Text>
          <Text style={styles.note}>
            {hasPitch ? frequencyToNote(event.pitch) : 'Waiting for audio...'}
          </Text>
        </View>

        {/* Debug data grid */}
        <Text style={styles.sectionTitle}>Raw Data</Text>
        <View style={styles.dataGrid}>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>pitch</Text>
            <Text style={[styles.dataValue, hasPitch && styles.dataValueActive]}>
              {hasPitch ? event.pitch.toFixed(2) : '-1'} Hz
            </Text>
          </View>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>confidence</Text>
            <Text style={styles.dataValue}>{event.confidence.toFixed(3)}</Text>
          </View>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>volume</Text>
            <Text style={[styles.dataValue, event.volume > -60 && styles.dataValueActive]}>
              {event.volume.toFixed(1)} dB
            </Text>
          </View>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>events</Text>
            <Text style={styles.dataValue}>{eventCount}</Text>
          </View>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>detections</Text>
            <Text style={styles.dataValue}>
              {pitchCount} ({eventCount > 0 ? ((pitchCount / eventCount) * 100).toFixed(0) : 0}%)
            </Text>
          </View>
          <View style={styles.dataRow}>
            <Text style={styles.dataLabel}>algorithm</Text>
            <Text style={[styles.dataValue, styles.dataValueActive]}>{algorithm}</Text>
          </View>
        </View>

        {/* Volume meter */}
        <Text style={styles.sectionTitle}>Volume</Text>
        <View style={styles.meterContainer}>
          <View style={styles.meterTrack}>
            <View
              style={[
                styles.meterFill,
                {
                  width: `${volumePercent * 100}%`,
                  backgroundColor: event.volume > -10 ? '#F44336' : event.volume > -30 ? '#FF9800' : '#4CAF50',
                },
              ]}
            />
          </View>
          <View style={styles.meterLabels}>
            <Text style={styles.meterLabel}>-100</Text>
            <Text style={styles.meterLabel}>-60</Text>
            <Text style={styles.meterLabel}>-30</Text>
            <Text style={styles.meterLabel}>0 dB</Text>
          </View>
        </View>

        {/* Algorithm selector */}
        <Text style={styles.sectionTitle}>Algorithm</Text>
        <View style={styles.algorithms}>
          {ALGORITHMS.map((a) => (
            <TouchableOpacity
              key={a.key}
              style={[
                styles.algoButton,
                algorithm === a.key && styles.algoButtonActive,
              ]}
              onPress={() => switchAlgorithm(a.key)}
            >
              <View style={styles.algoRow}>
                <Text
                  style={[
                    styles.algoLabel,
                    algorithm === a.key && styles.algoLabelActive,
                  ]}
                >
                  {a.label}
                </Text>
                <Text
                  style={[
                    styles.algoDesc,
                    algorithm === a.key && styles.algoDescActive,
                  ]}
                >
                  {a.desc}
                </Text>
              </View>
            </TouchableOpacity>
          ))}
        </View>
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0a0a0a',
  },
  scroll: {
    paddingHorizontal: 16,
    paddingBottom: 40,
  },
  title: {
    color: '#666',
    fontSize: 13,
    fontWeight: '600',
    fontFamily: 'Courier',
    textAlign: 'center',
    marginTop: 12,
    marginBottom: 8,
  },
  statusBar: {
    flexDirection: 'row',
    justifyContent: 'center',
    gap: 20,
    paddingVertical: 8,
    borderBottomWidth: 1,
    borderBottomColor: '#1a1a1a',
  },
  statusItem: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
  },
  dot: {
    width: 7,
    height: 7,
    borderRadius: 4,
  },
  statusLabel: {
    color: '#555',
    fontSize: 11,
    fontFamily: 'Courier',
    textTransform: 'uppercase',
  },
  display: {
    alignItems: 'center',
    paddingVertical: 28,
  },
  frequency: {
    color: '#fff',
    fontSize: 42,
    fontWeight: '700',
    fontVariant: ['tabular-nums'],
    fontFamily: 'Courier',
  },
  note: {
    color: '#888',
    fontSize: 18,
    marginTop: 4,
    fontFamily: 'Courier',
  },
  sectionTitle: {
    color: '#555',
    fontSize: 10,
    fontWeight: '700',
    fontFamily: 'Courier',
    textTransform: 'uppercase',
    letterSpacing: 2,
    marginTop: 16,
    marginBottom: 6,
  },
  dataGrid: {
    backgroundColor: '#111',
    borderRadius: 8,
    borderWidth: 1,
    borderColor: '#1a1a1a',
    overflow: 'hidden',
  },
  dataRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingHorizontal: 12,
    paddingVertical: 7,
    borderBottomWidth: 1,
    borderBottomColor: '#1a1a1a',
  },
  dataLabel: {
    color: '#555',
    fontSize: 12,
    fontFamily: 'Courier',
  },
  dataValue: {
    color: '#888',
    fontSize: 12,
    fontFamily: 'Courier',
    fontVariant: ['tabular-nums'],
  },
  dataValueActive: {
    color: '#4CAF50',
  },
  meterContainer: {
    marginBottom: 4,
  },
  meterTrack: {
    height: 8,
    backgroundColor: '#1a1a1a',
    borderRadius: 4,
    overflow: 'hidden',
  },
  meterFill: {
    height: '100%',
    borderRadius: 4,
  },
  meterLabels: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 3,
  },
  meterLabel: {
    color: '#333',
    fontSize: 9,
    fontFamily: 'Courier',
  },
  algorithms: {
    gap: 6,
  },
  algoButton: {
    backgroundColor: '#111',
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    borderWidth: 1,
    borderColor: '#1a1a1a',
  },
  algoButtonActive: {
    borderColor: '#4CAF50',
    backgroundColor: '#0a1a0a',
  },
  algoRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  algoLabel: {
    color: '#aaa',
    fontSize: 14,
    fontWeight: '600',
    fontFamily: 'Courier',
  },
  algoLabelActive: {
    color: '#4CAF50',
  },
  algoDesc: {
    color: '#444',
    fontSize: 11,
    fontFamily: 'Courier',
  },
  algoDescActive: {
    color: '#4CAF50',
    opacity: 0.6,
  },
});
