# SowGuard — IoT Lab (UAB, 2026-27)

Proof of concept for monitoring sows in farrowing crates:
- **Water consumption drop** → early illness indicator.
- **Sow lying down + piglet scream** → possible crushing → alarm on the phone.

The lab prototype is a simplified version of our theoretical design:
one single node, **BLE instead of Thread**, a **phone as the edge** instead of a
fixed farm gateway, and simulated data wherever we have no real sensor
(allowed by the professor).

## Architecture
```
nRF52840-DK  --BLE notify-->  Flutter app (Android)  --Internet-->  Cloud
(firmware/)                   (app/)                                (cloud/)
```

## Repo layout
| Folder      | Content                                                   | Owner     |
|-------------|-----------------------------------------------------------|-----------|
| `firmware/` | nRF Connect SDK (Zephyr) code for the nRF52840-DK         | Lluís     |
| `app/`      | Flutter edge app: BLE layer + dashboard UI                | Member 2 (BLE), Member 3 (UI) |
| `cloud/`    | Backend, database, web dashboard, ML (Report 2)           | TODO      |
| `docs/`     | `protocol.md` (BLE frame + edge→cloud format), course guide, test evidence | All |

The BLE frame and UUIDs are frozen in [`docs/protocol.md`](docs/protocol.md).
Any change must be agreed by the three of us and written there first.

## How we work with Git
1. `git pull` on `main` before starting.
2. Create your own branch: `git checkout -b <your-name>/<what-you-do>`.
3. Commit small steps, then `git push -u origin <branch>`.
4. Open a pull request on GitHub; someone else reviews it before merging.

## Deliverables
- Report 1 (Device + BLE + Edge): 06/11/2026 (moved from 30/10/2026)
- Lab evaluation (live demo): 07/01/2027
- Report 2 (backend + frontend + code): 10/01/2027
