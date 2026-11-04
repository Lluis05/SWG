/// Counts received and lost packets using the `seq` field of the frame.
///
/// The firmware adds 1 to `seq` on every frame, so a jump in `seq`
/// means frames were lost on the way. Used for the BLE stability tests.
class PacketStats {
  int received = 0;
  int lost = 0;
  int? _lastSeq;

  void add(int seq) {
    received++;
    final last = _lastSeq;
    if (last != null) {
      // Frames missing between the previous seq and this one.
      // "& 0xFFFF" handles the wrap-around of the uint16 counter (65535 -> 0).
      final gap = (seq - last - 1) & 0xFFFF;
      // A huge gap means the board rebooted (seq restarted from 0):
      // that is not packet loss, so we do not count it.
      if (gap < 1000) {
        lost += gap;
      }
    }
    _lastSeq = seq;
  }

  /// Lost frames as a percentage of all the frames the board sent.
  double get lossPercent {
    final total = received + lost;
    return total == 0 ? 0.0 : 100 * lost / total;
  }
}
