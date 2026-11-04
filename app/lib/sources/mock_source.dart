import 'dart:async';
import 'dart:math';

import '../models/sensor_reading.dart';
import 'reading_source.dart';

/// Fake data source, so the app can be developed and shown without the board.
///
/// It repeats a 40-reading cycle (80 s) so every state shows up in a demo:
///   readings  0-14: normal
///   reading     15: DHT11 read failure
///   readings 20-29: sow lying down (24-26 also crush alarm: scream while lying)
///   readings 32-39: water intake drops (low-water flag)
/// The values are made up to test the UI; they are NOT real measurements.
class MockSource implements ReadingSource {
  final _readings = StreamController<SensorReading>.broadcast();
  final _connected = StreamController<bool>.broadcast();
  final _random = Random();
  final _bootTime = DateTime.now();
  Timer? _timer;
  int _seq = 0;

  @override
  Stream<SensorReading> get readings => _readings.stream;

  @override
  Stream<bool> get connected => _connected.stream;

  @override
  Future<void> start() async {
    _connected.add(true); // the fake node is always "connected"
    _timer = Timer.periodic(const Duration(seconds: 2), (_) {
      _readings.add(_nextReading());
    });
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
    await _readings.close();
    await _connected.close();
  }

  SensorReading _nextReading() {
    final step = _seq % 40; // position inside the demo cycle
    final sensorError = step == 15;
    final lying = step >= 20 && step < 30;
    final crushAlarm = step >= 24 && step < 27;
    final lowWater = step >= 32;

    final reading = SensorReading(
      seq: _seq,
      timestampMs: DateTime.now().difference(_bootTime).inMilliseconds,
      tempC: sensorError ? 0 : _around(22.0, 1.5),
      humidity: sensorError ? 0 : _around(60.0, 5.0),
      waterMlMin: (lowWater ? _around(100, 20) : _around(400, 30)).round(),
      lying: lying,
      crushAlarm: crushAlarm,
      lowWater: lowWater,
      sensorError: sensorError,
    );

    _seq = (_seq + 1) & 0xFFFF; // uint16 counter, wraps like the firmware's
    return reading;
  }

  /// Random value between (center - spread) and (center + spread).
  double _around(double center, double spread) =>
      center + (_random.nextDouble() * 2 - 1) * spread;
}
