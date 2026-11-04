import 'package:flutter_test/flutter_test.dart';
import 'package:sowguard/models/packet_stats.dart';

void main() {
  group('PacketStats', () {
    test('no loss when seq goes up by 1', () {
      final s = PacketStats();
      for (final seq in [0, 1, 2, 3]) {
        s.add(seq);
      }
      expect(s.received, 4);
      expect(s.lost, 0);
      expect(s.lossPercent, 0.0);
    });

    test('counts the missing seq numbers', () {
      final s = PacketStats()
        ..add(10)
        ..add(11)
        ..add(14); // 12 and 13 were lost
      expect(s.received, 3);
      expect(s.lost, 2);
      expect(s.lossPercent, closeTo(40.0, 0.01)); // 2 of 5
    });

    test('wrap-around 65535 -> 0 is not a loss', () {
      final s = PacketStats()
        ..add(65534)
        ..add(65535)
        ..add(0)
        ..add(1);
      expect(s.lost, 0);
    });

    test('loss across the wrap-around', () {
      final s = PacketStats()
        ..add(65534)
        ..add(1); // 65535 and 0 were lost
      expect(s.lost, 2);
    });

    test('a board reboot (seq back to 0) is not a loss', () {
      final s = PacketStats()
        ..add(500)
        ..add(501)
        ..add(0);
      expect(s.received, 3);
      expect(s.lost, 0);
    });
  });
}
