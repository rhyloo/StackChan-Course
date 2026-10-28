# Stack-chan course: guide for AI assistants

Repository of a course for programming Stack-chan, an M5Stack CoreS3 (ESP32-S3) on the K151 base with two SCS0009 servos. Read `README.md` and the README of the demo you are going to work on before touching code. For anything about the hardware (pins, I2C, servos, limits) the reference is `docs/ROBOT.md`; the datasheets are in `docs/hardware/`.

## Language

- Talk to the user in Spanish (Spain).
- Everything in the repo is in English: READMEs, docs, user-facing text, identifiers and comments.

## Structure

| Path | Contents |
|---|---|
| `demos/01-vr-cardboard/` | Robot camera → laptop → phone (app [rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR), separate repo) and head pose back. |
| `demos/02-gemini-voice/` | Demo 01 plus voice: robot mic → relay → Gemini Live (function calls) → robot speaker. The head follows the VR viewer, not the voice. |
| `demos/02.1-voice-moves/` | Voice without VR: Gemini moves the head (`look`, `turn_head`, 0 = straight ahead, 360° pan), shows moods (`set_mood`: face command `face <mood>` + LEDs + posture) and dances. Reuses `audio.cpp` and `voice.py` from demo 02. |
| `demos/03-espnow-beacons/` | ESP-NOW beacons on ESP32 DevKit boards, receiver on the robot and trilateration on the laptop. |
| `demos/04-nfc/` | NFC reader: the base's ST25R3916 reads NFC-A tags and the robot shows their NDEF content on screen. |
| `demos/05-nfc-voice/` | Demo 02 + 04: Gemini writes an NDEF text on the card by voice (`write_card`, `read_card`). Reuses `audio.cpp` and `voice.py` from demo 02. |
| `demos/06-infrared/` | Infrared (receiver G10, emitter G5, 38 kHz) with IRremoteESP8266: decodes remote controls and repeats the last code when the screen is tapped. |
| `demos/07-photo-booth/` | Photo booth: countdown, photo → relay (Gemini/OpenCV filters + dithering to 384 px) → the robot prints over BLE (NimBLE) on an MX10. MX10 protocol in `printer_mx10.cpp`. |
| `demos/08-servo-sg90/` | SG90 servo on PORT.B (pulse on G9 and G8 at once) with LEDC at 50 Hz, moved from the touch screen. |
| `demos/09-sensor-kit/` | KY-0xx modules on PORT.B (G9/G8): digital, ADC, DHT11, DS18B20, encoder, joystick and outputs. Inputs go through a 10k/20k divider because the Grove port gives 5 V. |
| `demos/10-voice-gamepad/` | Relay only: demo 02's voice plus a PS4 gamepad paired with the laptop, read with pygame (`relay/gamepad.py`). The robot runs demo 02's firmware. Pairing: `tools/pair_gamepad.sh`. |
| `demos/11-tank/` | Keyestudio KS0428 tank: Arduino UNO firmware (`tank/`, `M<left>,<right>` commands with a 400 ms watchdog) and `laptop/tank.py` (BLE to the HM-10 with bleak). To flash it you must remove the HM-10. `run.sh vr` starts `relay/main.py` instead: demo 02's relay (VR + voice) with the gamepad driving the tank (`relay/tank_pad.py`). |
| `demos/12-mini-tv/` | Mini TV: `laptop/tv.py` converts videos with ffmpeg to `.tv` (320x240 JPEG frames + PCM audio, format in `main.cpp`) and uploads them over Wi-Fi (HTTP, `stackchan-tv.local`) to `/tv` on the microSD. The robot plays them as channels; the audio sets the pace. |
| `demos/13-web-server/` | The robot as a web server, no relay: `esp_http_server` page + WebSocket (port 80, page in `src/web/index.html` embedded with `board_build.embed_txtfiles`), MJPEG on port 81, optional MQTT (esp-mqtt) with Home Assistant discovery. JSON commands shared by WebSocket, MQTT and serial. |
| `demos/14-robot-arm/` | Not the robot: Elecfreaks Nezha Pro arm (micro:bit V2 + Nezha Pro board, smart motors M1-M3 over I2C 0x10, gripper on M3). MakeCode TypeScript in `microbit/`: text commands over USB serial (`run`/`move`/`goto`/`stop`/`zero`, replies `pos a1 a2 a3`), A/B move the gripper, a motor stops when its encoder stalls. Built offline by `tools/microbit.py` (pxt in `.makecode/`, needs Node.js) and flashed by copying the `.hex` to the `MICROBIT` drive. `laptop/arm_web.py` serves the web page (stdlib `http.server`, port 8014, `web/index.html`); `laptop/console.py` is the terminal console. |
| `demos/15-ota/` | OTA: a base (`firmware/src/ota.cpp`: Wi-Fi, ArduinoOTA 3232, web upload on port 80, UDP 3233 `who`/`log`) that runs tests (`src/tests/<name>/`, one PlatformIO env each, `testSetup`/`testLoop`/`testStop`, `Log`). New firmware stays pending until the test runs 15 s with Wi-Fi; a crash before that rolls back (bootloader), 3 crashes later = safe mode. First flash over USB, then `run.sh send <test>` (`laptop/ota.py`, espota). |
| `demos/16-math-game/` | Math game on the touch screen, one player: 26 skills (`game.cpp`: add/sub kinds, each times table, divisions), 4 options with typical-mistake distractors, pictures to count for the easy ones (`art.cpp`, shapes only). Stats per skill in NVS (`Preferences` "mathgame"); weighted choice towards what fails, a wrong question returns 3 later, the next skill unlocks at 8 of the last 10. Stats page from the robot (`web.cpp`, Arduino WebServer in `loop()`). No voice: ESP-SR is English/Chinese only and not in Arduino 2.0.17. |
| `demos/17-arcade/` | Arcade: KY-023 joystick on PORT.B (VRx G9, VRy G8, 10k/20k dividers), buttons A G17 / B G18 to GND with pull-ups, SW G2 optional. `main.cpp` = menu, controls screen, pause, game over, high scores (NVS "arcade"), 30 fps loop with a full-screen PSRAM canvas. Each game is `games/<name>.cpp` exporting a `Game` (`start`, `frame(input, dt)` → false at game over, `score`, `endTitle` or nullptr), listed in `GAMES`. `platformer.cpp`: original-art homage to 8-bit platformers (no Nintendo assets/names/look-alikes: slime, star blocks): tile level built in code, P meter, raccoon-tail flight/glide/swing, squash & stretch; art in `platformer_art.h` (char-art + palette, `makeSprite()`). `dino.cpp`: endless runner with original dino art. Servos off (`boardInit(false)`). |
| `lib/StackChanHW/` | Shared C++: servos, LEDs, INA226, camera. Limits and calibration in `stackchan_limits.h`. |
| `lib/StackChanLink/` | Shared C++: UDP link robot ↔ relay and its extension points for the demos. |
| `lib/python/stackchan/` | Shared Python: protocol and base relay. Each demo provides `hooks.py` and `processing.py`. |
| `config/wifi_secrets.h` | Wi-Fi. Generated by `setup`, listed in `.gitignore` and never committed. |

The protocol of all the demos is in `demos/01-vr-cardboard/PROTOCOL.md`. If you change a robot message, change `lib/StackChanLink/src/link.cpp` and `lib/python/stackchan/protocol.py` together. The phone ↔ laptop part (UDP 5601) is set by the app: don't change it without changing the app.

## Commands

```bash
demos/05-nfc-voice/run.sh [--no-flash]                       # a demo in one step: build, flash and start the laptop side
./setup.sh --no-system                                       # .venv environment + toolchains + build everything
.venv/bin/pio run -d demos/02-gemini-voice/firmware          # build a firmware
./tools/flash.sh demos/02-gemini-voice/firmware --no-monitor # flash (with no args it lists the firmwares)
./tools/flash.sh demos/03-espnow-beacons/beacon --env beacon2 /dev/ttyUSB0
.venv/bin/python -m unittest discover -s lib/python/tests    # protocol tests
demos/02-gemini-voice/server.sh --debug                   # relay; --debug saves logs/relay-*.log
.venv/bin/python tools/serial_log.py demos/02-gemini-voice/firmware   # robot serial → logs/robot-*.log
.venv/bin/python tools/microbit.py flash demos/14-robot-arm/microbit   # MakeCode project → MICROBIT drive
```

`run.sh` modes: `--flash-only`, `--debug`, `--no-flash`, plus `--usb PORT` and `--help` (Windows `run.bat`: `-FlashOnly`, `-DebugLogs`, `-NoFlash`, `-Usb`). Sub-commands: `demos/03-espnow-beacons/run.sh beacons`, `demos/11-tank/run.sh tank | vr`, `demos/12-mini-tv/run.sh channels | add VIDEO | list | remove NAME` (channels from `channels.txt`), `demos/15-ota/run.sh send TEST|FOLDER|FILE.bin | find | tests | log [--host NAME]`.

Pinned platform: `espressif32@7.1.2` (Arduino-ESP32 2.0.17, ESP-IDF 4.4.7), M5Unified 0.2.21.

## Rules you can't see in the code

- **The GC0308 has no hardware JPEG.** Capture in RGB565 and compress with `frame2jpg`; `PIXFORMAT_JPEG` fails.
- **Respect the head limits** in `stackchan_limits.h` and the servos' speed limiter. Below the minimum tilt the chin hits the base.
- **Don't raise the base LEDs above `LED_MAX_LEVEL`.** At full power, together with the speaker and Wi-Fi, the PMIC cut the robot's power.
- **I2C and screen only from the `loop()` task.** Network tasks leave orders in `PendingCommands`.
- **Core split:** core 0 runs the network and video compression; core 1, servos, audio and screen.
- **The CoreS3's mic and speaker share I2S:** audio is half-duplex.
- **In *Head* mode, the app resends the pose every 500 ms.** To move the head from code, call `robot.pause_phone_control()` first.
- **OpenCV is pinned to 4.x.** Version 5 removed `CascadeClassifier`.
- **The robot pairs with the first relay that talks to it.** Two open relays fight over it.
- **Secrets:** never print or commit the Wi-Fi password or `GEMINI_API_KEY`.
- **To close processes, use their PID.** `pkill -f <pattern>` also kills the very shell running it.

## How to work

There is real hardware and a person who tests it. Build, flash if the robot is connected, do at most one quick check and give them the steps to test it. Don't set up long simulations. Afterwards, read the `logs/` before changing anything.
