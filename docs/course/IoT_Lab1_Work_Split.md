# SowGuard — Lab Report 1: Work Split

Markdown conversion of `IoT_Lab1_Work_Split.pdf`. The frozen BLE contract is
kept up to date in [`../protocol.md`](../protocol.md).

IoT Laboratory (UAB) — Device + Edge over BLE — Deadline: 30 Oct 2026 (Virtual Campus)

> **Update:** the Report 1 deadline was moved to **6 Nov 2026**. The timeline below is the original plan.

## 1. Scope of this delivery
Report 1 covers only the Device (nRF52840-DK sending data over BLE) and the
Edge (Flutter app on Android receiving and showing that data). Cloud, database
and ML are not part of this delivery. The lab prototype is a simplified
version of the design: one single node, BLE instead of Thread, and simulated
data wherever we have no real sensor (allowed by the professor).

## 2. Prototype overview
```
DHT11 + DK buttons --GPIO--> nRF52840-DK firmware --BLE notify (15 B)--> Flutter BLE layer --Stream<SensorReading>--> Flutter UI
(real temp/hum,              (Zephyr / nRF Connect SDK)                  (scan, connect,                               (dashboard,
 simulated events)                                                        notify, decode)                               alarm banner)
```

| Signal | Source in the lab | How |
|--------|-------------------|-----|
| Temperature / humidity | Real: DFR0067 (DHT11) | Zephyr sensor driver, read every 2 s |
| Water flow | Simulated in firmware | Base value + noise; a slow drop can be triggered for the demo |
| Sow lying down (accelerometer in design) | Simulated: DK Button 1 | GPIO interrupt toggles posture |
| Piglet scream (microphone in design) | Simulated: DK Button 2 | Lying + scream within N s sets the crush alarm flag. No audio is sent |
| Status | DK LEDs | LED1 = connected, LED2 = alarm |

## 3. Shared contract (frozen by 23 Oct)
See [`../protocol.md`](../protocol.md) (UUIDs, 15-byte frame, Dart interface).

## 4. Who does what
| Member | Responsibilities | Report section |
|--------|------------------|----------------|
| **Lluís** — Firmware (Device), has the board | nRF Connect SDK project based on `peripheral_lbs`. DHT11 devicetree overlay and reading. GATT service + notify characteristic. Frame packing. Simulation module (water, buttons with interrupts, alarm logic, flags). LEDs. Integration of the final report. | Device layer, BLE frame spec, report integration |
| **Member 2** — Edge BLE layer | Flutter project and repo setup. Android 12+ BLE permissions. Scan filtered by service UUID, connect, enable notifications (CCCD), reconnect. Decoder (bytes → `SensorReading`) with unit tests on fixed byte arrays. `ReadingSource` interface, `MockSource` and `BleSource`. | Edge layer (BLE), per-layer tests |
| **Member 3** — Edge UI | Dashboard screen that only consumes `ReadingSource`, so it can be built from day 1 with `MockSource` and no board. Shows connection status, live values (temp, humidity, water, posture), red alarm banner when the crush flag is set, sensor-error indicator, and a Simulated/Live switch. Stretch goal: line chart of the last readings. | UI description + screenshots for test evidence |

Onboarding for Member 3 (day 1): GitHub account, clone the repo, basic Git
workflow (pull, commit, push, branch, pull request), Flutter + Android Studio
installed and the skeleton app running on a phone. Member 2 does a short
pairing session to explain the project structure and `MockSource`.

## 5. Timeline
| Date | Milestone | Who |
|------|-----------|-----|
| 22–23 Oct | Freeze contract (UUIDs, frame, Dart interface). Repo + tool setup. Member 3 onboarding. | All |
| 24, 25, 27 Oct | Development of each part (26 Oct: architecture defense, no lab work) | Each owner |
| 28 Oct | Integration session with the board: firmware + app end to end | All, together |
| 29 Oct | Tests (packet loss via seq, reconnection, sensor read failures), screenshots, report sections | All |
| 29 Oct (evening) | Code walk-through: each member explains their part to the other two | All |
| 30 Oct | Submission on Virtual Campus | Lluís |

## 6. Working rules
One GitHub repository with `firmware/`, `app/` and `docs/`. Each member works on
their own branch and merges through pull requests. Everyone must be able to
explain the whole data path (button press or sensor reading on the board up to
the value on the screen), not only their own part.
