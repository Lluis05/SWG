import 'dart:async';

import 'package:flutter/foundation.dart'; // debugPrint
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../models/sensor_reading.dart';
import 'frame_decoder.dart';
import 'reading_source.dart';

/// UUIDs from docs/protocol.md. Do not change them here without
/// changing the protocol (and the firmware) first.
final Guid kServiceUuid = Guid('5781a4a8-0001-41a3-b91f-baa566bff75f');
final Guid kDataCharUuid = Guid('5781a4a8-0002-41a3-b91f-baa566bff75f');

/// Real data source: the nRF52840-DK over BLE (library: flutter_blue_plus).
///
/// Steps:
///   1. wait until Bluetooth is on
///   2. scan for devices advertising OUR service UUID
///   3. connect
///   4. discover services and find our characteristic
///   5. enable notifications (writes 0x0001 to the CCCD)
///   6. decode every notification and send it to the UI
/// If the link drops, it tries to reconnect every 3 s.
class BleSource implements ReadingSource {
  final _readings = StreamController<SensorReading>.broadcast();
  final _connected = StreamController<bool>.broadcast();

  BluetoothDevice? _device;
  StreamSubscription<BluetoothConnectionState>? _stateSub;
  Timer? _retryTimer;
  bool _wasConnected = false;
  bool _stopped = false;

  @override
  Stream<SensorReading> get readings => _readings.stream;

  @override
  Stream<bool> get connected => _connected.stream;

  @override
  Future<void> start() async {
    _connected.add(false);

    // 1. Bluetooth must be on. On Android we can ask the user to turn it on.
    try {
      await FlutterBluePlus.turnOn();
    } catch (e) {
      debugPrint('BLE: could not turn Bluetooth on: $e');
    }
    await FlutterBluePlus.adapterState
        .where((state) => state == BluetoothAdapterState.on)
        .first;

    // 2. Scan until the node is found (each scan lasts up to 15 s).
    while (!_stopped && _device == null) {
      _device = await _scanForNode();
    }
    final device = _device;
    if (_stopped || device == null) return;
    debugPrint('BLE: node found (${device.remoteId})');

    // Watch the connection: when it drops, reconnect.
    _stateSub = device.connectionState.listen(_onConnectionState);

    // 3-6.
    await _connectAndSubscribe();
  }

  @override
  Future<void> stop() async {
    _stopped = true;
    _retryTimer?.cancel();
    await _stateSub?.cancel();
    try {
      await FlutterBluePlus.stopScan();
      await _device?.disconnect();
    } catch (e) {
      debugPrint('BLE: error while stopping: $e');
    }
    await _readings.close();
    await _connected.close();
  }

  /// Scans for up to 15 s. Returns the node, or null if it was not found.
  Future<BluetoothDevice?> _scanForNode() async {
    BluetoothDevice? found;

    // Listen to results BEFORE starting the scan, so none is missed.
    final sub = FlutterBluePlus.onScanResults.listen(
      (results) {
        if (found == null && results.isNotEmpty) {
          found = results.first.device;
          FlutterBluePlus.stopScan(); // we have it, stop scanning
        }
      },
      onError: (e) => debugPrint('BLE: scan error: $e'),
    );
    FlutterBluePlus.cancelWhenScanComplete(sub);

    try {
      // The filter makes Android report only devices advertising our UUID.
      await FlutterBluePlus.startScan(
        withServices: [kServiceUuid],
        timeout: const Duration(seconds: 15),
      );
      // Wait until the scan ends: node found (stopScan) or 15 s timeout.
      await FlutterBluePlus.isScanning.where((scanning) => !scanning).first;
    } catch (e) {
      debugPrint('BLE: scan failed: $e');
      await Future.delayed(const Duration(seconds: 3));
    }
    return found;
  }

  /// Connects, finds our characteristic and enables notifications.
  /// Must run again after every reconnection (services are lost on disconnect).
  Future<void> _connectAndSubscribe() async {
    final device = _device;
    if (device == null || _stopped) return;

    try {
      // 3. Connect (gives up after 10 s if the node does not answer).
      await device.connect(license: License.free, timeout: const Duration(seconds: 10));

      // 4. Find our service and our data characteristic.
      final services = await device.discoverServices();
      final service = services.firstWhere((s) => s.uuid == kServiceUuid);
      final dataChar =
          service.characteristics.firstWhere((c) => c.uuid == kDataCharUuid);

      // 6. Every notification -> decode -> UI.
      final sub = dataChar.onValueReceived.listen(_onFrame);
      device.cancelWhenDisconnected(sub); // auto-cancel when the link drops

      // 5. Enable notifications: writes 0x0001 to the CCCD (0x2902).
      await dataChar.setNotifyValue(true);
      debugPrint('BLE: notifications enabled');
    } catch (e) {
      debugPrint('BLE: connection failed: $e');
      _scheduleRetry();
    }
  }

  void _onConnectionState(BluetoothConnectionState state) {
    final isConnected = state == BluetoothConnectionState.connected;
    if (!_stopped) _connected.add(isConnected);

    if (isConnected) {
      _wasConnected = true;
    } else if (_wasConnected) {
      // The link dropped (board reset, out of range...): reconnect.
      _wasConnected = false;
      debugPrint('BLE: disconnected '
          '(${_device?.disconnectReason?.description}), reconnecting');
      _scheduleRetry();
    }
  }

  void _scheduleRetry() {
    if (_stopped) return;
    _retryTimer?.cancel();
    _retryTimer = Timer(const Duration(seconds: 3), _connectAndSubscribe);
  }

  void _onFrame(List<int> bytes) {
    if (_stopped) return;
    try {
      final reading = decodeFrame(bytes);
      debugPrint('BLE: $reading');
      _readings.add(reading);
    } on FormatException catch (e) {
      // A malformed frame is ignored, but logged so we can see it.
      debugPrint('BLE: bad frame ignored ($e): $bytes');
    }
  }
}
