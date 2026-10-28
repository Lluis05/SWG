# SowGuard protocol

Frozen on 23 Oct 2026. Any change must be agreed by the three members and
written here first. This file goes directly into the reports.

## 1. BLE (Device → Edge)

The nRF52840-DK is a BLE peripheral (GATT server). The phone is the central.

> Lab uses BLE with one node; Design uses a Thread mesh with several nodes per pen.

| Item                   | Value |
|------------------------|-------|
| GATT service UUID      | `5781a4a8-0001-41a3-b91f-baa566bff75f` |
| Data characteristic    | `5781a4a8-0002-41a3-b91f-baa566bff75f` (Notify only) |
| CCCD                   | `00002902-0000-1000-8000-00805f9b34fb` (write `0x0001` to enable notifications) |
| Notification period    | 2 s (DHT11 max. is about 1 reading/s) |
| Security               | None: no pairing/bonding, the link is not encrypted (decided 28 Oct). Simpler for the Flutter app and the demo; a known limitation of the prototype |
| Byte order             | Little-endian |
| Frame size             | 15 bytes, fits in one notification with the default MTU (20 B of payload) |

### Data frame (version 1)

| Offset | Size | Field       | Type     | Meaning |
|--------|------|-------------|----------|---------|
| 0      | 1    | `version`   | uint8    | Frame format version (= 1) |
| 1      | 2    | `seq`       | uint16   | Sequence number, used to measure packet loss |
| 3      | 4    | `timestamp` | uint32   | ms since boot |
| 7      | 2    | `temp`      | int16    | °C × 100 (real, DHT11) |
| 9      | 2    | `hum`       | uint16   | %RH × 100 (real, DHT11) |
| 11     | 2    | `water`     | uint16   | mL/min (simulated) |
| 13     | 1    | `posture`   | uint8    | 0 = standing, 1 = lying (simulated, Button 1) |
| 14     | 1    | `flags`     | uint8    | bit0 crush alarm, bit1 low water, bit2 sensor read failed |

Total: 15 bytes.

No application CRC: the BLE link layer already adds a CRC-24 to every packet.

### Dart interface (BLE layer ↔ UI)

```dart
class SensorReading {
  final int seq, timestampMs, waterMlMin;
  final double tempC, humidity;
  final bool lying, crushAlarm, lowWater, sensorError;
}

abstract class ReadingSource {      // two implementations:
  Stream<SensorReading> get readings; // - MockSource: fake readings in the app
  Stream<bool> get connected;         // - BleSource: real data from the node
}
```

## 2. Edge → Cloud

TODO (Report 2): protocol (MQTT / HTTP REST / WebSockets) and JSON schema.
