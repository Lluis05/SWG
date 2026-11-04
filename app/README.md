# Edge app (Flutter, Android)

Scans for the SowGuard service UUID, connects, enables notifications (CCCD),
decodes the frame from [`../docs/protocol.md`](../docs/protocol.md) and shows it.

## Structure
```
lib/
  main.dart                    app entry point + theme
  models/sensor_reading.dart   one decoded reading (Dart interface in protocol.md)
  models/packet_stats.dart     packet loss counter based on `seq`
  sources/reading_source.dart  interface used by the UI
  sources/mock_source.dart     fake readings, no board needed
  sources/ble_source.dart      real board over BLE (flutter_blue_plus)
  sources/frame_decoder.dart   15 bytes -> SensorReading
  ui/app_colors.dart           colours
  ui/dashboard_screen.dart     the only screen
test/
  frame_decoder_test.dart      decoder unit tests on fixed byte arrays
  packet_stats_test.dart       packet loss counter tests
```

Data path: `BLE notification (15 B) -> BleSource -> decodeFrame() -> SensorReading
-> Stream<SensorReading> -> DashboardScreen`. `MockSource` produces the same
`SensorReading` objects, so the UI cannot tell the difference.

## Setup (once)
1. Generate the Android project around our code (from the repo root):
   ```
   cd app
   flutter create --project-name sowguard --org cat.uab --platforms android .
   ```
   Then copy our `lib/` and `test/` over the generated ones (overwrite `main.dart`)
   and delete `test/widget_test.dart` (it refers to the template's counter app).
2. Add the BLE library:
   ```
   flutter pub add flutter_blue_plus
   ```
   License: FlutterBluePlus License, free for educational use.
3. Add the permissions to `android/app/src/main/AndroidManifest.xml`,
   inside `<manifest>` and before `<application>`:
   ```xml
   <uses-feature android:name="android.hardware.bluetooth_le" android:required="false" />
   <!-- Android 12+ -->
   <uses-permission android:name="android.permission.BLUETOOTH_SCAN" android:usesPermissionFlags="neverForLocation" />
   <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
   <!-- Android 11 or lower -->
   <uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30" />
   <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />
   <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="30" />
   <uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" android:maxSdkVersion="28" />
   ```
4. flutter_blue_plus needs Android `minSdk` 21 or higher
   (`android/app/build.gradle.kts`). The Flutter default already meets it.

## Run
```
flutter test        # unit tests (no phone needed)
flutter run         # phone connected by USB with USB debugging enabled
```
The app starts with simulated data. The "Real board" switch moves to `BleSource`.
BLE does not work in the Android emulator: use a real phone.

## Requirements on the firmware side
For `BleSource` to find and read the board, the firmware must:
- advertise the service UUID `5781a4a8-0001-41a3-b91f-baa566bff75f` (the scan filters by it),
- expose the data characteristic `5781a4a8-0002-41a3-b91f-baa566bff75f` with Notify,
- have security disabled (no pairing), as decided on 28 Oct,
- send exactly 15 bytes per notification.
