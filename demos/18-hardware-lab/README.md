# Demo 18 · Hardware lab

Every part of the robot on one web page: camera, head, LEDs, screen, speaker and microphone, battery, IMU, light and proximity, the touch panel on the head, NFC, infrared and the microSD card. You see the sensors live and you can drive the outputs from a phone or a laptop. It is a good first check of a robot: what answers and what doesn't.

It is the same as demo 15's `hardware-lab` test, without the OTA: it is flashed over USB like the other demos.

## What you need

- **The robot.**
- **A phone or laptop** on the Wi-Fi in `config/wifi_secrets.h` (`./setup.sh --wifi`).
- Optional, to try everything: an NFC card or sticker, a TV remote control and a microSD card (FAT32).

## Run it

```bash
demos/18-hardware-lab/run.sh               # build, flash and show the robot's serial output
demos/18-hardware-lab/run.sh --flash-only  # only flash it
```

On Windows use `run.bat` the same way.

## Use it

1. **The robot's screen** shows its address (`http://192.168.x.x` and `http://stackchan-XXXX.local`), a green or red box for each part that answered at start (base, camera, light, touch, NFC, SD) and a summary of the sensors. **Tap the screen** for a QR code with the address; tap again to go back.
2. **Open the address** in a browser on the same Wi-Fi. The page asks the robot for its state 4 times a second.

| On the page | What it does |
|---|---|
| Robot | IP, Wi-Fi signal, uptime, free memory, chip temperature, RTC clock, microSD size. |
| Camera | One photo, or the live stream (MJPEG on port 81, about 10 fps). |
| Head | Pan and tilt sliders, center, torque on and off. The limits of `stackchan_limits.h` apply. |
| LEDs and screen | The base LEDs (limited to `LED_MAX_LEVEL`), a color on the whole screen for 3 s, brightness. |
| Sound | A tone, the volume, the microphone level, and a 3 s recording you can play on the robot or listen to on the page (`/rec.wav`). |
| Battery | Level, voltage and current (INA226; positive = discharging). |
| Touch | The 3 zones of the head panel (Si12T) and the last tap on the screen. |
| Movement, light | Accelerometer and gyroscope; light in lux and proximity. |
| NFC | UID, type and the first text or URL of each tag held near the base. It beeps when it reads one. |
| Infrared | The last code from a remote control, and a button to send it back. |

## Commands

The page sends text commands, and the same work typed on the serial monitor:

| Command | What it does |
|---|---|
| `head PAN TILT` | Move the head (degrees). |
| `center` | Head to 0, 0. |
| `torque on\|off` | Servos holding or free (move the head by hand). |
| `leds R G B` | Base LEDs, 0-255 each (scaled down to `LED_MAX_LEVEL`). |
| `screen R G B` | The whole screen in a color for 3 s. |
| `brightness N` | Screen brightness, 0-255. |
| `tone HZ MS` | A beep. |
| `volume N` | Speaker volume, 0-255. |
| `mic on\|off` | The microphone level meter. |
| `record`, `play` | Record 3 s, play the recording. |
| `ir` | Send the last infrared code back. |
| `nfc on\|off` | Stop or start reading NFC tags. |

## How it works

| File | What for |
|---|---|
| `main.cpp` | `setup()` and `loop()`: the sensors, the commands and the screen. Everything on I2C and the screen happens in `loop()`. |
| `web.cpp` | `esp_http_server` on core 0: the page, `/state` (JSON), `/cmd`, `/photo.jpg`, `/rec.wav`, and the camera stream on port 81. |
| `audio.cpp` | Its own task owns the microphone and the speaker: they share one I2S bus, so only one works at a time. |
| `index.html` | The page, built into the firmware (`board_build.embed_txtfiles`). |

The web tasks never touch the hardware: they leave commands in a queue that `loop()` runs, and read the last state, which `loop()` rebuilds as JSON every 150 ms.

## If something fails

- **The screen says `Wi-Fi: ...` and never shows an address:** the robot doesn't reach the network in `config/wifi_secrets.h`. Change it with `./setup.sh --wifi` and flash again. Only 2.4 GHz networks work.
- **`stackchan-XXXX.local` doesn't open:** some phones and Windows don't resolve `.local` names. Use the IP, or the QR code.
- **A red box on the screen:** that part didn't answer at start. The SD box is red without a card.
