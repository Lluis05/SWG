import '../models/sensor_reading.dart';

/// Where the dashboard gets its data from. Two implementations:
///  - MockSource: fake readings generated inside the app (no board needed).
///  - BleSource:  real readings from the nRF52840-DK over BLE.
/// The UI only talks to this interface, so it works the same with both.
abstract class ReadingSource {
  /// A new reading every time the node sends a frame (every 2 s).
  Stream<SensorReading> get readings;

  /// true when connected to the node, false when not.
  Stream<bool> get connected;

  /// Starts producing readings.
  /// MockSource: starts a timer. BleSource: scans, connects and subscribes.
  Future<void> start();

  /// Stops everything and frees resources.
  /// A stopped source cannot be started again: create a new one.
  Future<void> stop();
}
