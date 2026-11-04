import 'dart:typed_data';

import '../models/sensor_reading.dart';

/// Size and version of the BLE data frame (see docs/protocol.md).
const int kFrameLength = 15;
const int kFrameVersion = 1;

/// Turns the 15 bytes of one BLE notification into a SensorReading.
///
/// Byte layout (little-endian):
///   0      version   uint8
///   1..2   seq       uint16
///   3..6   timestamp uint32  (ms since boot)
///   7..8   temp      int16   (°C x 100)
///   9..10  hum       uint16  (%RH x 100)
///   11..12 water     uint16  (mL/min)
///   13     posture   uint8   (0 standing, 1 lying)
///   14     flags     uint8   (bit0 crush, bit1 low water, bit2 sensor error)
///
/// Throws FormatException if the size or the version is wrong.
SensorReading decodeFrame(List<int> bytes) {
  if (bytes.length != kFrameLength) {
    throw FormatException('Expected $kFrameLength bytes, got ${bytes.length}');
  }

  // ByteData lets us read multi-byte numbers with the right byte order.
  final data = ByteData.sublistView(Uint8List.fromList(bytes));

  final version = data.getUint8(0);
  if (version != kFrameVersion) {
    throw FormatException('Unknown frame version $version');
  }

  final flags = data.getUint8(14);

  return SensorReading(
    seq: data.getUint16(1, Endian.little),
    timestampMs: data.getUint32(3, Endian.little),
    tempC: data.getInt16(7, Endian.little) / 100, // signed: can be negative
    humidity: data.getUint16(9, Endian.little) / 100,
    waterMlMin: data.getUint16(11, Endian.little),
    lying: data.getUint8(13) == 1,
    crushAlarm: (flags & 0x01) != 0, // bit0
    lowWater: (flags & 0x02) != 0, // bit1
    sensorError: (flags & 0x04) != 0, // bit2
  );
}
