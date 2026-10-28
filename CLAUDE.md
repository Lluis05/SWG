# SowGuard — IoT Lab (UAB, 2026-27)

## Context
Group project (3 people) for the IoT course at UAB. The project has two separate parts:
- **Design**: the ideal/theoretical system (Thread mesh, fixed farm gateway as edge, several nodes per pen). Documented in an IEEE-format report. NOT implemented here.
- **Lab** (this repo): proof of concept that approximates the Design with the hardware we actually have.

Whenever a Lab decision differs from the Design, say so explicitly (e.g. "Lab uses BLE; Design uses Thread").

Use case: monitoring sows in farrowing crates.
- Water consumption drop → early illness indicator.
- Accelerometer detects the sow lying down → activates microphone → detects piglet screams (possible crushing) → push alarm.

## Lab architecture
Device → BLE → Edge (phone) → Internet → Cloud (backend + DB + web dashboard + ML)

- **Device**: Nordic nRF52840-DK, BLE peripheral (GATT server, notifications). Single node.
  - Real sensor: temperature/humidity only, DFR0067 (DHT11).
  - Simulated in firmware (allowed by the professor): water flow, accelerometer, microphone/scream detection.
  - Firmware framework: nRF Connect SDK (Zephyr), based on the `peripheral_lbs` sample. Do not use Arduino BLE (the course guide says its BLE library is broken since 2024).
- **Edge**: Flutter mobile app. Scans by our Service UUID, connects via GATT, enables notifications (CCCD), shows data, raises a push alarm on crushing events, forwards data to the cloud.
- **Cloud**: TODO. Free tier / free tools only. Needs storage, a web dashboard and at least one ML model (Edge or Cloud level).

## Repo layout
```
firmware/   nRF52840 code
app/        Flutter edge app
cloud/      backend, DB config, web dashboard, ML
docs/       protocol.md, test evidence (docs/evidence/), course PDFs + markdown versions (docs/course/)
```

## Rules for Claude
- Keep code simple and readable. We will be questioned on every part of it during the lab evaluation. Prefer clarity over cleverness; avoid unnecessary abstractions, patterns or dependencies.
- Before choosing a framework/library or making a structural change, propose 2 options with trade-offs and wait for confirmation.
- Any change to the BLE data frame or the edge→cloud payload MUST update `docs/protocol.md` (byte-by-byte breakdown of the BLE frame; protocol + JSON schema for edge→cloud). This goes directly into the report.
- The edge UI is owned by a teammate with no programming experience: keep widgets simple, comment them, and do not refactor his UI without asking.
- Never invent datasheet values, UUIDs or measurements. Mark unknowns as TODO.
- Only free tools and services.

## Deliverables
- Report 1 (PCB + BLE + edge communication): 06/11/2026 (moved from 30/10/2026)
- Lab evaluation (live demo): 07/01/2027
- Report 2 (backend + frontend + code zip/GitHub): 10/01/2027
- Required report structure: see `docs/course/IoT-lab-guide.md` (includes per-layer testing, end-to-end testing and test evidence — keep logs/screenshots in `docs/evidence/`).

## Commands
TODO: build / flash / run commands for firmware, app and cloud once chosen.
