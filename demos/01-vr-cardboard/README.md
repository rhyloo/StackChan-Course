# Demo 01 · Cardboard VR

See through Stack-chan's eyes with a Google Cardboard VR viewer and move its head by turning yours, or with a PS4 gamepad. You'll learn how video and head pose travel between the robot, the laptop and the phone, and where to plug in your own computer vision.

```
Robot (camera) --UDP 5600, JPEG--> Laptop (relay) --UDP 5601, JPEG--> Phone (Stack-chan VR app)
Robot (servos) <-- pose ---------- Laptop (relay) <-- control/sensors -- Phone
```

**Why there is a laptop in the middle.** The ESP32-S3 struggles to send video to several places and process it at the same time. The laptop forwards the video and, if you want, processes it first with OpenCV: more contrast, face detection, recording. This is where you code during the course.

## What you need

- **The robot.**
- **The laptop**, with the course installed (`setup.sh`).
- **An Android phone** with the [Stack-chan VR](https://github.com/rhyloo/StackChan-VR) app, and a Cardboard VR viewer.
- **Optional, a PS4 gamepad** paired with the **phone** over Bluetooth (not with the robot: the ESP32-S3 only has Bluetooth LE and the gamepad needs Bluetooth Classic).

## Run it

Connect the robot over USB-C and run, from the repo root:

```bash
demos/01-vr-cardboard/run.sh               # flash the robot and start the demo
demos/01-vr-cardboard/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/01-vr-cardboard/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/01-vr-cardboard/run.sh --no-flash    # start the demo with what the robot already has
demos/01-vr-cardboard/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

The default builds the firmware, flashes it and starts the laptop relay. On the robot's screen you'll see its IP, its name (`stackchan-XXXX`) and, soon after, "Laptop 192.168.x.x". While nobody is watching, the robot sends no video, to save battery.

Next time, if you haven't changed the firmware, `--no-flash` is enough (or `server.sh`, which only starts the relay). The relay options below work with both: `run.sh --show`.

- **No phone?** `server.sh --show` shows the video in a window on the laptop.
- **No robot?** `.venv/bin/python demos/01-vr-cardboard/fake_robot.py` simulates one.

## Try it

1. **Open the app** on the phone. The laptop shows up in the list by itself.
2. Tap *Enter VR*, put the phone in the VR viewer and look around. The robot's head follows yours.

### Controls in the VR viewer

| Action | How |
|---|---|
| Recenter | Tap the screen (or the Cardboard button), or ✕ on the gamepad |
| Show/hide the HUD | Double tap, or △ |
| Change the control mode | □. Modes: view only, head, gamepad, head + gamepad. |
| Exit | Long press, or ○ |
| Move the head with the gamepad | Left stick; R2 to go slower |

Tapping the robot's screen centers its head.

## How it works

The relay receives the robot's JPEG frames on UDP 5600 and forwards them to the phone on UDP 5601. The phone sends back its control state and, if you ask for them, its sensors; the relay turns that into a head pose for the servos. All messages are in [PROTOCOL.md](PROTOCOL.md).

| File | What for |
|---|---|
| `relay/processing.py` | Every frame goes through `process()` before reaching the phone: filters, face detection, overlaying data… |
| `relay/hooks.py` | React to gamepad buttons (L1, R1, d-pad…), events (shaking the phone, touching the screen) and sensors, and send commands to the robot (`tone`, `leds`, `text`, `home`) or to the phone (`hud`, `vibrate`, `say`) |
| `firmware/src/main.cpp` | The robot's screen and main loop |
| `fake_robot.py` | A simulated robot, to test the relay without hardware |
| `../../lib/StackChanLink/` | The UDP link with the laptop (shared with demo 02) |
| `../../lib/StackChanHW/` | Servos, LEDs, battery and camera |

### Battery

The base has a battery of only 550 mAh. What drains it most is the Wi-Fi radio (no power saving, so the video doesn't stutter), the camera and the JPEG compression. That's why:
- **Video pauses** when no phone is connected and neither `--show` nor `--record` is on.
- **The relay's status line shows the real current draw** (`mA`), measured by the base's INA226. A negative value means it is charging.
- **There are commands to measure how much each thing saves:** `brightness 0-255` (screen) and `wifi_sleep 1` (Wi-Fi power saving; adds latency). Send them from `hooks.py` with `robot.cmd("brightness 20")`.

For long classes, keep the robot plugged in over USB-C.

## Options

Relay options (`run.sh` or `server.sh`; `--help` lists them all):

| Option | What it does |
|---|---|
| `--show` | Shows the video in a window (`q` closes it) |
| `--enhance` | More contrast: the GC0308 camera is dark indoors |
| `--upscale 2` | Doubles the resolution before sending it |
| `--record videos` | Records what you see into `videos/` |
| `--robot stackchan-XXXX` | Pairs only with that robot, if there are several on the network |
| `--sensors accelerometer=20,light=2 --events shake,touch` | Asks the phone for sensors (in the app: *Sensors → Share with laptop*) |
| `--always-video` | The robot sends video even if nobody is watching (uses more battery) |

## Troubleshooting

| Symptom | What to do |
|---|---|
| The robot stays on "WiFi: connecting to..." | The network must be 2.4 GHz. Check the name and password with `./setup.sh --wifi` and flash again. |
| The robot says "Looking for the laptop (relay)..." | The relay isn't running, the laptop is on another network, the firewall blocks UDP 5600-5601 ([docs/ENVIRONMENT.md](../../docs/ENVIRONMENT.md)), or the network isolates clients. |
| The app doesn't find the laptop | Same causes. You can also type the laptop's IP by hand in the app. |
| Two relays fight over the robot | The robot sticks with the first one that talks to it. Close the other one (Ctrl+C) or use `--robot`. |
| The image is very dark | `--enhance`, and more light. |

## Ideas to extend

- Uncomment the face detection example in `relay/processing.py` and draw boxes around faces.
- Use a free gamepad button in `relay/hooks.py` to beep, change the LEDs or write on the robot's screen.
- Subscribe to `--events shake` and make the robot react when you shake the phone.
