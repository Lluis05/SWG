# Firmware (Device layer)

nRF Connect SDK v3.4.1 / Zephyr project for the Nordic nRF52840-DK, based on the
`peripheral_lbs` sample. All the code is in `src/main.c`.

- Real sensor: DFR0067 (DHT11) temperature/humidity on P0.03 (A0), read every 2 s.
- Simulated with the DK buttons:
  - Button 1: sow lying / standing
  - Button 2: piglet scream (crush alarm if the sow lay down ≤ 30 s ago)
  - Button 3: water flow starts dropping (low water < 200)
  - Button 4: simulate a DHT11 failure
- LEDs: LED1 = connected, LED2 = alarm, LED3 = sow lying, LED4 = running (blinks).
- Sends the 15-byte frame described in [`../docs/protocol.md`](../docs/protocol.md)
  as a BLE notification every 2 s.

## Just run it (no PC needed)

The firmware stays in the board's flash. If the board is already flashed:

1. Power the board: USB cable on the short-side connector (J-Link) to any PC,
   phone charger or power bank. Power switch ON.
2. Open the SowGuard app on the phone (Bluetooth on).
3. LED1 turns on when the phone connects, and data appears in the app.

## Install the tools (once per PC)

| Tool | Why | How |
|---|---|---|
| SEGGER J-Link | Talks to the chip that flashes the board | Free download from segger.com (Arch: `paru -S jlink-software-and-documentation`, also installs the udev rules) |
| nrfutil | Nordic's command-line tool | Download from nordicsemi.com, then `nrfutil install device sdk-manager` |
| nRF Connect SDK v3.4.1 | Zephyr + ARM compiler + west | `nrfutil sdk-manager install v3.4.1` (several GB, installs to `~/ncs`) |

Check that the board is detected:

```bash
nrfutil device list
```

## Build and flash

From this folder (`firmware/`):

```bash
# Build (-p always = from scratch; use it the first time or after changing prj.conf/overlay)
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 -- west build -p always -b nrf52840dk/nrf52840 -d build .

# Flash (--erase wipes the board's memory first)
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 -- west flash -d build --erase
```

After small changes to `src/main.c` you can drop `-p always` (faster build).

## See the board's log (optional)

The board prints messages over the USB serial port at 115200 baud:

```bash
stty -F /dev/ttyACM0 115200 raw -echo
cat /dev/ttyACM0
```

You will see lines like `Connected`, `Connection params: interval 480 ms ...`,
`DHT11: 23 C, 45 %RH`. On Windows use PuTTY (or similar) on the board's COM port
at 115200.

## Files

| File | What it is |
|---|---|
| `src/main.c` | All the firmware: BLE (GAP + GATT), buttons, LEDs, DHT11, frame |
| `prj.conf` | Zephyr options: BLE peripheral, name, connection parameters, sensor |
| `boards/nrf52840dk_nrf52840.overlay` | Devicetree: tells Zephyr the DHT11 is on P0.03 |
| `CMakeLists.txt` | Build file (from the Nordic sample) |
