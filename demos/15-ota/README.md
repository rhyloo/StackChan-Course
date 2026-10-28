# Demo 15 · Updates over Wi-Fi (OTA)

You flash the robot over USB **once**, and from then on you send it new programs over the Wi-Fi, with no cable: OTA (*over the air*). The robot keeps a small base that connects to the Wi-Fi and waits for updates, and on top of it runs a **test**: a short program like an Arduino sketch. You change a test, send it, and a few seconds later it is running. If the new program fails, the robot goes back to the one it had before by itself.

## What you need

- **The robot** and a laptop **on the same Wi-Fi**. The robot uses the network in `config/wifi_secrets.h` (`./setup.sh --wifi`).
- A USB-C cable, only for the first time.

## Run it

1. **The first time, over USB:** this flashes the OTA base with the `hello` test and opens the serial monitor.

```bash
demos/15-ota/run.sh               # flash the OTA base over USB and open the serial monitor
demos/15-ota/run.sh --flash-only  # only flash it
```

2. **From then on, over the Wi-Fi** (unplug the cable if you like):

```bash
demos/15-ota/run.sh send leds     # build the "leds" test, send it and show what it says
demos/15-ota/run.sh find          # the robots with the OTA base on this Wi-Fi
demos/15-ota/run.sh tests         # the tests there are
demos/15-ota/run.sh log           # only show what the robot says (its Log lines)
demos/15-ota/run.sh send head --host stackchan-4F48   # if there are several robots
demos/15-ota/run.sh --help        # every option
```

On Windows use `run.bat` the same way (`run.bat send leds`).

The bar at the top of the screen always says the robot's name, its IP, the test and its state: **green** (confirmed), **orange** (new, being checked), **red** (safe mode), **grey** (no Wi-Fi).

## Try it

1. **Send a test:** `run.sh send leds`. The screen shows the progress, the robot restarts and the base LEDs go round the color wheel. On the laptop you'll see:

   ```
   ==> Running 'leds' (build 3fa1c2d0). It stays if it works for 15 s...
   [leds] color wheel on the base
   [OTA] test 'leds' confirmed: this firmware stays
   ==> Confirmed: 'leds' stays on the robot.
   ```

   Then come the `Log` lines of the test, like a serial monitor over the Wi-Fi. Ctrl+C to quit (the robot goes on).
2. **Change a test:** in `firmware/src/tests/hello/hello.cpp` change `MESSAGE`, then `run.sh send hello`.
3. **Break it on purpose:** `run.sh send crash`. It counts 5, 4, 3... and crashes. As it hadn't been confirmed yet, the robot goes back to the previous test:

   ```
   ==> ROLLED BACK: the new firmware failed before it was confirmed (a crash?).
       The robot is back on 'leds' (build 3fa1c2d0).
   ```

4. **From the browser:** open `http://<IP>` (it's on the bar) or `http://stackchan-XXXX.local`. It shows the robot's state, and you can upload any `firmware.bin`.
5. **Safe mode:** switch it off and on **holding the screen**. The test doesn't run; the robot only waits for an update.

| Test | What it does |
|---|---|
| `hello` | A message and a color; tap the screen to change the color. The one to copy for your own tests. |
| `leds` | The base LEDs go round the color wheel. Tap to pause. |
| `head` | The head looks left and right and nods, slowly. Tap to stop. |
| `crash` | Crashes 5 s after starting, to see the rollback. |

### Your own test

1. Copy `firmware/src/tests/hello` to `firmware/src/tests/mytest` (any name without spaces).
2. Add at the end of `firmware/platformio.ini`:

   ```ini
   [env:mytest]
   build_src_filter = ${env.build_src_filter} +<tests/mytest/>
   build_flags = ${env.build_flags} -DTEST_NAME=\"mytest\"
   ```

3. `run.sh send mytest`.

A test is like an Arduino sketch, with three functions:

```cpp
#include "ota.h"
void testSetup() { ... }   // once, like setup()
void testLoop()  { ... }   // over and over, like loop(); M5.update() is already called
void testStop()  { ... }   // optional: an update is arriving, switch off motors, LEDs, sound
```

- **Draw below `OTA_BAR_H`** (20 px): the top bar is the base's.
- **`Log.printf(...)`** instead of `Serial.printf(...)`: it goes to the USB serial *and* over the Wi-Fi to `run.sh send` / `run.sh log`.
- Don't use `WiFi.disconnect()` or change the Wi-Fi mode: without Wi-Fi there's no next update.

### Other projects

`run.sh send` also takes a PlatformIO folder or a `.bin`:

```bash
demos/15-ota/run.sh send demos/08-servo-sg90/firmware
demos/15-ota/run.sh send path/to/firmware.bin
```

It works, but **those programs don't have the OTA base**: after that the robot doesn't listen on the Wi-Fi, and to come back you flash demo 15 over USB (`run.sh`). That's why the tests are the way to try things over OTA.

## How it works

```
 laptop: run.sh send leds
   1. pio run -e leds  ──>  .pio/build/leds/firmware.bin
   2. "who"  ──UDP 3233 (broadcast)──>  robot answers: name, test, state, build
   3. espota.py  ──UDP 3232 "invitation"──>  robot
                 <──TCP: the robot fetches the firmware──
   4. "log"  ──UDP 3233──>  robot sends back its state and the Log lines
```

**Two slots.** The flash (16 MB) has two places for a program, `app0` and `app1` (partition table `default_16MB.csv`, 6.25 MB each). The update is written to the slot that is **not** running, so if the transfer is cut halfway the robot still has the old one intact. At the end, a small area (`otadata`) says "boot from the other slot" and the robot restarts. The bar and `run.sh find` show which slot is running.

**Rollback.** The new program starts **pending**. The base marks it **valid** (`esp_ota_mark_app_valid_cancel_rollback()`) when the test has run for 15 s, `loop()` is still turning and the Wi-Fi is up, that is, when a next update could get in. If it crashes first, on the restart the **bootloader** sees it still pending and boots the previous one. If it neither crashes nor gets there (stuck in `testSetup()`, or without Wi-Fi), at 90 s the base itself goes back. Arduino marks every program valid when it starts; the base stops that with `verifyRollbackLater()`.

**Safe mode.** A test that crashes *after* being confirmed would restart again and again. The base counts the crashes in RTC memory (it survives a restart): after 3 in a row it starts without the test. Holding the screen while it starts does the same.

**The network, on its own.** The test runs in the `loop()` task (core 1), like a sketch. The Wi-Fi, ArduinoOTA, the web page and the UDP port run in another task on core 0: even if a test gets stuck in an endless loop, an update still gets in.

`firmware/src/`:

| File | What for |
|---|---|
| `ota.cpp` | The base: Wi-Fi, ArduinoOTA (port 3232), web page (port 80), UDP 3233, confirmation, rollback and safe mode, the top bar. |
| `ota.h` | What a test sees: `testSetup/testLoop/testStop`, `OTA_BAR_H`, `Log`. |
| `tests/<name>/` | One test. `platformio.ini` has one environment per test, and each one builds the base plus only its folder. |

`laptop/ota.py` finds the robot, builds with PlatformIO, sends with `espota.py` (it comes with the ESP32 Arduino framework) and follows the robot's state until the update is confirmed or rolled back.

**A password (optional).** Anyone on the same Wi-Fi can send firmware to the robot. In a class, add this line to `config/wifi_secrets.h` and flash the base over USB again: `run.sh send` uses it by itself.

```c
#define OTA_PASSWORD "something-of-yours"
```

## Troubleshooting

| Symptom | What to do |
|---|---|
| `I can't find any robot` | Is the bar green or orange, with an IP? If it's grey, the robot doesn't reach the Wi-Fi in `config/wifi_secrets.h`. Is the laptop on that same network? Some networks (guests, campus) block traffic between devices: use a phone hotspot. With the IP of the bar: `--host 192.168.1.134`. |
| `The update didn't get through` | The robot connects back to the laptop: a firewall can stop it (Windows: allow Python on private networks). With `OTA_PASSWORD`, the base on the robot must have the same one. |
| It always rolls back | The test crashes in its first 15 s, or `loop()` doesn't come back for more than 3 s (`delay()` too long, a `while` without end). Look at the Log lines or at the USB serial: `run.sh --no-flash`. |
| Red bar, "Safe mode" | The test crashed 3 times in a row. Send another one: `run.sh send hello`. |
| After `send <folder>` it doesn't answer any more | Normal: that program has no OTA base. `run.sh` over USB. |
| The USB serial shows a crash | `run.sh --no-flash` opens the monitor and translates the addresses with the last firmware built: build the test that crashed. |

## Ideas to extend

- **A test that uses the camera** (`cameraInit()` from `StackChanHW`) and shows it on the screen.
- **Updates from the internet:** with `HTTPUpdate`, the robot downloads a `.bin` from a URL by itself (for example, when a GitHub release comes out).
- **A version number:** add `-DTEST_VERSION=3` to the environment and only accept updates with a higher version.
