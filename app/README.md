# Edge app (Flutter, Android)

Scans for the SowGuard service UUID, connects, enables notifications (CCCD),
decodes the frame from [`../docs/protocol.md`](../docs/protocol.md) and shows it.

- **BLE layer** (Member 2): permissions, scan, connect, reconnect, decoder + unit tests,
  `ReadingSource` with `MockSource` and `BleSource`.
- **UI** (Member 3): dashboard that only uses `ReadingSource`, so it works with
  `MockSource` and no board.

## Run
TODO
