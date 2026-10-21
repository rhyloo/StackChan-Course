# Demo 03 · ESP-NOW beacons

Three ESP32 boards act as beacons and broadcast an [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/network/esp_now.html) message ten times per second. Stack-chan listens to them and measures how strong each one arrives (RSSI). With that:
- **it knows which beacon is closest:** it shows it on screen, sets the base LEDs to its colour and asks it over ESP-NOW to turn its LED on;
- **the laptop estimates its position** on a map (trilateration).

You'll learn how ESP-NOW works, how to turn signal strength into distance and how to locate something from three distances.

```
 beacon 1 ))) ESP-NOW (((                          ┌───────────────┐
 beacon 2 ))) ESP-NOW ((( ──>  Stack-chan  ──UDP──>│ laptop:       │
 beacon 3 ))) ESP-NOW (((      (receiver)  5602    │ map with the  │
            <── "turn your LED on" ┘               │ position      │
                                                   └───────────────┘
```

**ESP-NOW** is an Espressif protocol that lets two ESP32 send each other short messages (up to 250 bytes) directly, with no router and no prior connection, and very low latency. It is used for remote controls, sensors and robot swarms.

## What you need

- **The robot**, which acts as the receiver.
- **3 classic ESP32 DevKit boards** (ESP32-WROOM), one per beacon, powered over USB or by a USB power bank.
- **The laptop.**

## Run it

1. **Flash the three beacons.** The script asks you to connect each board, one after another:
   ```bash
   demos/03-espnow-beacons/run.sh beacons      # Windows: demos\03-espnow-beacons\run.bat beacons
   ```
   By hand it would be `./tools/flash.sh demos/03-espnow-beacons/beacon --env beacon1` (and `beacon2`, `beacon3`).
   In the serial monitor you'll see `Beacon 1 ready: channel 1`. The blue LED blinks once per second.
2. **Connect the robot and run the demo.** It flashes the receiver to the robot and opens the map (see "Try it"):
   ```bash
   demos/03-espnow-beacons/run.sh               # flash the robot and start the demo
   demos/03-espnow-beacons/run.sh --flash-only  # only build and flash; the firmware stays on the robot
   demos/03-espnow-beacons/run.sh --debug       # like the default, and save logs/robot-*.log
   demos/03-espnow-beacons/run.sh --no-flash    # start the demo with what the robot already has
   demos/03-espnow-beacons/run.sh --help        # every option of this demo
   ```
   On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

   The first time, close the map with Ctrl+C and do steps 3 and 4 first.
3. **Place the beacons** in a big triangle, 3 to 5 m per side, up high and with no metal objects around. Measure their positions in metres and write them in `laptop/beacons.json`.
4. **Calibrate.** Put the robot 1 m from a beacon, with nothing in between, and run:
   ```bash
   .venv/bin/python demos/03-espnow-beacons/laptop/position.py --calibrate 1
   ```

## Try it

Open the map (or `run.sh --no-flash`):

```bash
.venv/bin/python demos/03-espnow-beacons/laptop/position.py          # --text to see it without a window
```

Move the robot around the room. Each beacon's circle is the estimated distance; the white dot is the position.

On the robot's screen there is one bar per beacon. The closest one is framed and marked `<- closest`, the base LEDs take its colour (1 red, 2 green, 3 blue) and that beacon's LED stays on.

## How it works

**Same channel.** ESP-NOW needs no router, but sender and receiver must be on the **same** radio **channel**. The robot is connected to the Wi-Fi, which sets its channel. That's why each beacon looks for that network at boot (with the name in `config/wifi_secrets.h`) and switches to its channel, although it never connects to it.

**From RSSI to distance**, with the log-distance model:

```
rssi = rssi_1m − 10·n·log10(d)      →      d = 10^((rssi_1m − rssi) / (10·n))
```
- `rssi_1m` is what you measure at 1 m (it is calibrated).
- `n` is the path-loss exponent: 2 in free space, 2.5 to 4 indoors.

**Trilateration.** With three distances, we look for the point whose distances to the three beacons best match the measured ones (least squares, Gauss-Newton method).

**The RSSI in the callback.** In Arduino-ESP32 2.x, the ESP-NOW callback has no RSSI. The receiver also turns on promiscuous mode, sees the whole 802.11 frame and takes the RSSI from its header.

**Realistic expectations.** Indoors the signal bounces off walls, furniture and people, and your own body weakens it. Expect position errors of 1 to 3 m. What you can trust is "which beacon am I closest to".

The receiver sends its readings to the laptop by UDP broadcast on port 5602; the format is in [PROTOCOL.md](../01-vr-cardboard/PROTOCOL.md).

| File | What for |
|---|---|
| `espnow_msg.h` | The ESP-NOW messages (beacon → robot and robot → beacon). Add new types here. |
| `beacon/src/main.cpp` | The beacon: what it broadcasts and what it does when it gets commands |
| `receiver/src/main.cpp` | The robot: RSSI smoothing, screen, LEDs, commands to the beacons |
| `laptop/position.py` | Distance model, trilateration and map |
| `laptop/beacons.json` | Beacon positions and calibration |

## Options

`position.py` (you can also pass them to `run.sh`):

| Option | What it does |
|---|---|
| `--calibrate ID` | Measures `rssi_1m` with the robot 1 m from beacon `ID` and saves it in `beacons.json` |
| `--text` | No window: text only in the terminal |
| `--config FILE` | Uses another configuration file instead of `laptop/beacons.json` |

## Troubleshooting

| Symptom | What to do |
|---|---|
| The beacon keeps saying "Network not found" | The Wi-Fi in `config/wifi_secrets.h` must be in range and 2.4 GHz. If you change it, flash the beacons again. |
| The robot says "waiting..." for all of them | The beacon and the robot are on different channels: restart the beacon after the robot connects to the Wi-Fi. |
| The map receives nothing | The laptop and the robot must be on the same network, with UDP 5602 open in the firewall. |
| The position jumps a lot | That's the RSSI. Increase the smoothing (`RSSI_ALPHA` in the receiver, `0.7/0.3` in `position.py`) or calibrate again. |

## Ideas to extend

- Turn the head towards the closest beacon with `servoSetTarget()` from `StackChanHW`.
- Improve the accuracy:
  - more beacons;
  - a Kalman filter;
  - calibrate `n`;
  - a fingerprint map: measure the RSSI at known points and compare.
