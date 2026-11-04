# Edge app — technical overview

Written on 4 Nov 2026 for the whole team (and for any Claude agent working on the
repo). It explains what the Flutter edge app does, how every part fits together,
what the firmware must do so the two connect, and what is still pending.

The BLE contract itself (UUIDs, 15-byte frame, Dart interface) lives in
[`protocol.md`](protocol.md). This file never redefines it; it only explains how
the app uses it.

---

## 1. Purpose and scope

The app is the **Edge layer** of SowGuard. It runs on an Android phone and:

1. connects to the nRF52840-DK over BLE,
2. receives one 15-byte frame every 2 s (notifications),
3. decodes it into readable values,
4. shows them on screen and raises visible alarms (crushing, low water).

Forwarding data to the cloud is **not** implemented yet (Report 2).

> Lab uses a phone as the edge; Design uses a fixed farm gateway.
> Lab uses BLE with one node; Design uses a Thread mesh with several nodes per pen.

## 2. Status (4 Nov 2026)

| Item | Status |
|------|--------|
| Decoder (15 bytes → `SensorReading`) | Done, 8 unit tests passing |
| Packet loss counter (`seq`) | Done, 5 unit tests passing |
| `MockSource` (fake data) | Done, runs on a real phone (Samsung S25 FE, Android 16) |
| Dashboard UI | Done, works with `MockSource` |
| `BleSource` (real board) | Written, compiles, **not tested yet**: the board still runs the unmodified Nordic `peripheral_lbs` sample, which does not advertise our UUID |
| Edge → Cloud | Not started (Report 2) |

`flutter test` → `All tests passed!` (13 tests).

## 3. Tech stack

| Component | Choice | Notes |
|-----------|--------|-------|
| Framework | Flutter (Dart), Android only | Developed with Flutter stable 3.47.9 on Linux Mint (Lenovo T480) |
| BLE library | `flutter_blue_plus` (2.x, exact version in `app/pubspec.lock`) | Central role only. FlutterBluePlus License: free for educational use. Since 2.x, `connect()` requires `license: License.free` |
| Android | `minSdk` ≥ 21 (Flutter default), target SDK 36 | BLE does not work in the Android emulator: always use a real phone |
| Package | `sowguard`, applicationId `cat.uab.sowguard` | Created with `flutter create --project-name sowguard --org cat.uab --platforms android .` |

No other third-party dependencies (on purpose: we must be able to explain all the code).

## 4. Architecture and data path

```
nRF52840-DK                       Android phone (Flutter app)
-----------                       -----------------------------------------------------------
firmware --BLE notify (15 B)-->   BleSource ──> decodeFrame() ──> SensorReading ─┐
                                                                                 ├─> Stream<SensorReading> ──> DashboardScreen
                                  MockSource ──────────────────> SensorReading ─┘      Stream<bool> connected
```

The key idea is the **`ReadingSource` interface**: the UI only depends on it, never
on BLE. `MockSource` and `BleSource` both implement it and produce the same
`SensorReading` objects, so the UI works identically with fake and real data. The
"Real board" switch in the app bar swaps one for the other at runtime.

## 5. Files and responsibilities

```
app/
  lib/
    main.dart                    App entry point, theme, opens DashboardScreen
    models/sensor_reading.dart   One decoded reading (fields = Dart interface in protocol.md)
    models/packet_stats.dart     Counts received/lost frames from gaps in `seq`
    sources/reading_source.dart  Interface: readings, connected, start(), stop()
    sources/mock_source.dart     Fake readings every 2 s, 80 s demo cycle
    sources/ble_source.dart      Real board: scan, connect, notify, reconnect
    sources/frame_decoder.dart   decodeFrame(): 15 bytes -> SensorReading
    ui/app_colors.dart           All colours
    ui/dashboard_screen.dart     The only screen
  test/
    frame_decoder_test.dart      Decoder tests on fixed byte arrays
    packet_stats_test.dart       Packet loss counter tests
  android/app/src/main/AndroidManifest.xml   BLE permissions added (see §8)
```

`test/widget_test.dart` from the Flutter template was deleted (it tested the
template's counter app).

## 6. How each part works

### 6.1 `SensorReading` (models/sensor_reading.dart)
Plain immutable class with the fields frozen in `protocol.md`:
`seq`, `timestampMs`, `waterMlMin` (int); `tempC`, `humidity` (double);
`lying`, `crushAlarm`, `lowWater`, `sensorError` (bool). `toString()` prints
everything on one line, used in the logs.

### 6.2 `decodeFrame()` (sources/frame_decoder.dart)
- Rejects any frame that is not exactly 15 bytes or whose version byte is not 1
  (`FormatException`).
- Uses `ByteData` with `Endian.little` to read each field at its offset.
- `temp` is read as **signed** int16 (negative temperatures work); `hum` as uint16.
  Both are divided by 100.
- `posture == 1` → `lying = true`. Flags: bit0 crush, bit1 low water, bit2 sensor error.
- Test vectors were cross-checked with Python `struct.pack('<BHIhHHBB', ...)`.

### 6.3 `ReadingSource` (sources/reading_source.dart)
```dart
abstract class ReadingSource {
  Stream<SensorReading> get readings;
  Stream<bool> get connected;
  Future<void> start();
  Future<void> stop();   // a stopped source is not reused: create a new one
}
```
`start()` and `stop()` were **added on 4 Nov** (recorded in `protocol.md`,
pending agreement of the three members) so the UI can switch sources.
Both streams are broadcast streams. The UI subscribes **before** calling
`start()` so it does not miss the first events.

### 6.4 `MockSource` (sources/mock_source.dart)
Emits one reading every 2 s and always reports `connected = true`. It repeats a
40-reading (80 s) cycle so every state can be shown in a demo:

| Readings in cycle | State |
|-------------------|-------|
| 0–14 | normal |
| 15 | DHT11 failure (`sensorError`) |
| 20–29 | sow lying down |
| 24–26 | crush alarm (lying + scream) |
| 32–39 | low water |

Values (≈22 °C, ≈60 %RH, ≈400 mL/min normal, ≈100 mL/min low) are made up for
the UI. They are **not** real measurements and not the firmware's thresholds.
`seq` wraps at 65535 like the firmware's uint16.

### 6.5 `BleSource` (sources/ble_source.dart)
Sequence in `start()`:
1. Asks Android to turn Bluetooth on (`FlutterBluePlus.turnOn()`), then waits
   until the adapter state is `on`.
2. **Scan** for up to 15 s with `withServices: [5781a4a8-0001-…]`. Only devices
   advertising our service UUID are reported. The first result is taken and the
   scan is stopped. If nothing is found, it scans again (loop).
3. Subscribes to `device.connectionState` (kept for the whole session).
4. **Connect** (`timeout` 10 s, `license: License.free`). Android requests MTU 512
   by default; our 15 B frame fits in any MTU anyway.
5. `discoverServices()`, finds our service and the data characteristic
   `5781a4a8-0002-…`.
6. Listens to `onValueReceived` (auto-cancelled on disconnect with
   `cancelWhenDisconnected`), then `setNotifyValue(true)`, which writes `0x0001`
   to the CCCD.
7. Every notification → `decodeFrame()` → added to `readings`. Malformed frames
   are logged and ignored.

**Reconnection:** if the link drops after having been connected, or a connection
attempt fails, it retries `connect + discover + subscribe` every 3 s. Services are
always re-discovered after a reconnection (required by BLE/flutter_blue_plus).
The same device is reused; it does not scan again.

**Logs:** every step prints a `BLE: ...` line (`debugPrint`), including each
decoded reading. They appear in the `flutter run` terminal and are meant as test
evidence. To see only those: `adb logcat -s flutter | grep "BLE:"`.

### 6.6 `PacketStats` (models/packet_stats.dart)
For each new `seq`: `gap = (seq - lastSeq - 1) & 0xFFFF` (the mask handles the
65535 → 0 wrap). If `gap < 1000` it is added to `lost`; a bigger gap is treated
as a **board reboot** (seq restarted at 0) and not counted.
`lossPercent = lost / (received + lost) × 100`. Reset when the source changes.

### 6.7 `DashboardScreen` (ui/dashboard_screen.dart)
- App bar: title plus the "Real board" switch (off = `MockSource`, on = `BleSource`).
- Connection line: "Simulated data (no board)" / "Searching for the SowGuard node…" /
  "Connected to the SowGuard node".
- **Red banner** when `crushAlarm`; **amber banner** when `lowWater`.
- 2 × 2 tiles: temperature, humidity (show "Sensor error" when `sensorError`),
  water flow (amber when `lowWater`), posture.
- "Link quality": packets received, lost, loss %, last `seq`.
- State is kept with plain `setState` + stream subscriptions (no state
  management library), so it is easy to explain.

## 7. Integration contract with the firmware

For `BleSource` to find and read the board, the firmware **must**:

- [ ] Advertise the service UUID `5781a4a8-0001-41a3-b91f-baa566bff75f`
      (advertising data or scan response). The current sample advertises the LBS UUID,
      so the app will stay in "Searching…" until this changes.
- [ ] Expose the data characteristic `5781a4a8-0002-41a3-b91f-baa566bff75f` with
      **Notify** (and its CCCD).
- [ ] Have **no security**: no pairing/bonding (decided 28 Oct). The sample has
      `BT_LBS_SECURITY_ENABLED` on by default (passkey); it must be off/removed.
- [ ] Send **exactly 15 bytes** per notification, layout of `protocol.md`
      version 1, little-endian, every 2 s.
- [ ] Increment `seq` by 1 per frame (uint16, wraps at 65535).
- [ ] `timestamp` = ms since boot (uint32).
- [ ] Set the flags bits as defined (bit0 crush, bit1 low water, bit2 sensor read failed).
- [ ] Optionally rename the device (currently `Nordic_LBS`). The app does not
      filter by name, only by UUID.

**Open points to agree (TODO, not decided):**
- Does `seq` keep incrementing while no phone is connected? This changes how
  "lost" packets after a reconnection should be read in the report.
- What `temp`/`hum` values are sent when the DHT11 read fails (the app ignores them
  when bit2 is set, so any value works)?
- The crush window "lying + scream within N s": N is still TODO in the firmware.
- Low-water threshold used by the firmware for bit1.

## 8. Setup, run, test

One-time setup (already done on Pol's T480; repeat on any new machine):
1. Flutter SDK + Android Studio (SDK, Platform-Tools, **Command-line Tools**),
   `flutter doctor --android-licenses`. "Linux toolchain" errors in `flutter doctor`
   can be ignored (desktop target, not used).
2. From `app/`: `flutter pub get`.
3. Permissions in `android/app/src/main/AndroidManifest.xml` (already committed):
   `BLUETOOTH_SCAN` (neverForLocation), `BLUETOOTH_CONNECT`, plus legacy
   `BLUETOOTH`, `BLUETOOTH_ADMIN`, `ACCESS_FINE_LOCATION` (≤ API 30) and
   `ACCESS_COARSE_LOCATION` (≤ API 28). flutter_blue_plus asks for the runtime
   permission ("Nearby devices") itself when scanning starts.

Commands (from `app/`):
```
flutter test     # unit tests, no phone needed
flutter run      # phone by USB with USB debugging on
```
In `flutter run`: `r` hot reload, `R` hot restart, `q` quit.

## 9. Testing for the report

Per-layer (Edge) tests already in place:
- `frame_decoder_test.dart`: normal frame, negative temperature, crush while lying,
  low water + sensor error, max seq/timestamp, too short, too long, unknown version.
- `packet_stats_test.dart`: no loss, gap, wrap-around, loss across wrap-around, reboot.

Integration tests to run once the firmware is ready (evidence goes to `docs/evidence/`):

| Test | How | Evidence |
|------|-----|----------|
| End-to-end data | Switch "Real board" on, compare values on screen with the firmware's UART log | Screenshot + `BLE:` log lines |
| Packet loss | Leave it running N minutes, read "Link quality" | Screenshot of the counters |
| Reconnection | Reset the board / move out of range and back; app must reconnect alone (retry every 3 s) | `BLE: disconnected … reconnecting` + `notifications enabled` in the log |
| Sensor failure | Unplug the DHT11; tiles must show "Sensor error" | Screenshot |
| Alarms | Button 1 (lying) + Button 2 (scream) → red banner | Screenshot |

## 10. Known limitations / TODO

- No Edge → Cloud forwarding yet (protocol + JSON schema still TODO in `protocol.md`).
  Note for Report 2: `timestamp` is ms since boot, so the edge must add wall-clock
  time when forwarding.
- Alarms are on-screen banners only; no system push notification.
- No history/chart of past readings (stretch goal).
- If the user denies the Bluetooth permission or keeps Bluetooth off, the app
  waits in "Searching…" without an explicit message.
- Thermal stress is not computed: temp/hum are shown but there is no heat-stress
  flag in the frame. It could be derived at the edge or in the cloud without
  changing the frame.
- `start()`/`stop()` in `ReadingSource` pending team agreement.

## 11. Troubleshooting

- **Samsung phones:** turn off *Settings → Security and privacy → Auto Blocker*,
  otherwise USB debugging commands are blocked.
- **`flutter devices` shows the phone as "unauthorized":** accept the "Allow USB
  debugging?" prompt on the phone. **"no permissions" (Linux):**
  `sudo apt-get install -y android-sdk-platform-tools-common` and reconnect.
- **Compile error "Required named parameter 'license'":** flutter_blue_plus 2.x
  needs `device.connect(license: License.free, ...)` (already fixed in the code).
- **Red `WARNING: A restricted method in java.lang.System…` during `flutter run`:**
  Gradle/Java warning, harmless.
- **"Real board" stays in "Searching…":** the board is not advertising our service
  UUID (see §7), or Bluetooth/permissions are off on the phone.
