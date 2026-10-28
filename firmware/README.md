# Firmware (Device layer)

nRF Connect SDK / Zephyr project for the Nordic nRF52840-DK, based on the
`peripheral_lbs` sample.

- Real sensor: DFR0067 (DHT11) temperature/humidity, read every 2 s.
- Simulated: water flow (base value + noise), posture (Button 1), piglet scream (Button 2).
- LEDs: LED1 = connected, LED2 = alarm.
- Sends the 15-byte frame described in [`../docs/protocol.md`](../docs/protocol.md).

## Build / flash
TODO
