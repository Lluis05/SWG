import 'package:flutter_test/flutter_test.dart';
import 'package:sowguard/sources/frame_decoder.dart';

// Byte arrays built by hand from the frame in docs/protocol.md
// (and cross-checked with Python's struct.pack('<BHIhHHBB', ...)).
void main() {
  group('decodeFrame', () {
    test('normal frame: standing, no flags', () {
      final r = decodeFrame([
        0x01, //                   version = 1
        0x02, 0x01, //             seq = 258
        0xE8, 0x03, 0x00, 0x00, // timestamp = 1000 ms
        0x29, 0x09, //             temp = 2345 -> 23.45 °C
        0x7C, 0x15, //             hum = 5500 -> 55.00 %
        0x5E, 0x01, //             water = 350 mL/min
        0x00, //                   posture = standing
        0x00, //                   flags = none
      ]);
      expect(r.seq, 258);
      expect(r.timestampMs, 1000);
      expect(r.tempC, closeTo(23.45, 0.001));
      expect(r.humidity, closeTo(55.0, 0.001));
      expect(r.waterMlMin, 350);
      expect(r.lying, isFalse);
      expect(r.crushAlarm, isFalse);
      expect(r.lowWater, isFalse);
      expect(r.sensorError, isFalse);
    });

    test('negative temperature (int16 is signed)', () {
      final r = decodeFrame([
        0x01, 0x07, 0x00, 0xD0, 0x07, 0x00, 0x00, // v1, seq 7, t 2000 ms
        0xF3, 0xFD, //                             temp = -525 -> -5.25 °C
        0xB8, 0x0B, //                             hum = 3000 -> 30.00 %
        0x00, 0x00, 0x00, 0x00, //                 water 0, standing, no flags
      ]);
      expect(r.tempC, closeTo(-5.25, 0.001));
      expect(r.humidity, closeTo(30.0, 0.001));
    });

    test('crush alarm while lying', () {
      final r = decodeFrame([
        0x01, 0x2A, 0x00, 0x20, 0x48, 0x01, 0x00, // v1, seq 42, t 84000 ms
        0x28, 0x0A, 0x96, 0x19, //                 26.00 °C, 65.50 %
        0x7C, 0x01, //                             water = 380 mL/min
        0x01, //                                   posture = lying
        0x01, //                                   flags = bit0 (crush)
      ]);
      expect(r.seq, 42);
      expect(r.timestampMs, 84000);
      expect(r.waterMlMin, 380);
      expect(r.lying, isTrue);
      expect(r.crushAlarm, isTrue);
      expect(r.lowWater, isFalse);
      expect(r.sensorError, isFalse);
    });

    test('low water + sensor error flags', () {
      final r = decodeFrame([
        0x01, 0x64, 0x00, 0x40, 0x0D, 0x03, 0x00, // v1, seq 100, t 200000 ms
        0x00, 0x00, 0x00, 0x00, //                 temp 0, hum 0
        0x5A, 0x00, //                             water = 90 mL/min
        0x00, //                                   standing
        0x06, //                                   flags = bit1 + bit2
      ]);
      expect(r.waterMlMin, 90);
      expect(r.crushAlarm, isFalse);
      expect(r.lowWater, isTrue);
      expect(r.sensorError, isTrue);
    });

    test('maximum seq and timestamp', () {
      final r = decodeFrame([
        0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // seq 65535, t 2^32 - 1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      ]);
      expect(r.seq, 65535);
      expect(r.timestampMs, 4294967295);
    });

    test('rejects a frame that is too short', () {
      expect(() => decodeFrame([0x01, 0x02, 0x03]), throwsFormatException);
    });

    test('rejects a frame that is too long', () {
      expect(() => decodeFrame(List.filled(16, 0x01)), throwsFormatException);
    });

    test('rejects an unknown version', () {
      final bytes = List.filled(15, 0x00);
      bytes[0] = 0x02; // version 2 does not exist
      expect(() => decodeFrame(bytes), throwsFormatException);
    });
  });
}
