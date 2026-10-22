# Stack-chan course

Demos to learn to program [Stack-chan](docs/ROBOT.md), an M5Stack desktop robot with an ESP32-S3, camera, screen and two servos to move its head. Each demo lives in its own folder, with its own README, and you can do it without the others.

| Demo | What it does | You need |
|---|---|---|
| [01 · VR cardboard](demos/01-vr-cardboard/) | You see through the robot's eyes with a VR viewer and move its head by turning yours or with a PS4 gamepad. The laptop can process the image (OpenCV). | Robot, laptop, Android phone with the [Stack-chan VR](https://github.com/rhyloo/StackChan-VR) app, VR viewer (Cardboard). Optional: PS4 gamepad. |
| [02 · Gemini voice](demos/02-gemini-voice/) | Demo 01 plus voice: you talk to the robot ("turn on night vision", "what do you see?", "set the LEDs to blue") and Gemini Live understands, answers through the robot's speaker and runs functions. | What demo 01 needs (the phone is optional) and a [Gemini key](https://aistudio.google.com/apikey). |
| [02.1 · Head and moods by voice](demos/02.1-voice-moves/) | No VR: you tell the robot where to look ("look behind you", "a bit more to the left"), how to feel ("are you sad?") or to dance, and it moves its head, changes its face and its LEDs. | Robot, laptop and a [Gemini key](https://aistudio.google.com/apikey). |
| [03 · ESP-NOW beacons](demos/03-espnow-beacons/) | Three ESP32 boards act as ESP-NOW beacons. The robot measures their signal, knows which one is closest, and the laptop estimates its position on a map. | Robot, 3 ESP32 DevKit boards, laptop. |
| [04 · NFC reader](demos/04-nfc/) | You hold an NFC card or sticker near the base and the robot shows its serial number, its type and what is written on it (text, URL…). | Robot and NFC tags (NTAG, MIFARE). Optional: a phone with NFC to write them. |
| [05 · NFC by voice](demos/05-nfc-voice/) | You say what you want to store ("write on the card: …"), hold the card near the robot and it writes it. It can also read back what the card says. | Robot, laptop, NTAG cards and a [Gemini key](https://aistudio.google.com/apikey). |
| [06 · Infrared](demos/06-infrared/) | You point a remote control at the robot and see the code of each button; tap the screen and the robot repeats it, like a universal remote. | Robot and an infrared remote control (TV, air conditioner…). |
| [07 · Photo booth](demos/07-photo-booth/) | Tap the screen, a 3 or 5 s countdown and a photo. You choose a filter (Ghibli, Gaming, Comic and Picasso with AI; Sketch, Pixel or Original) and it prints on an MX10 thermal printer over Bluetooth. | Robot, laptop, MX10 printer and, for the AI filters, a [Gemini key](https://aistudio.google.com/apikey). |
| [08 · SG90 servo](demos/08-servo-sg90/) | You connect an SG90 micro servo to PORT.B and move it from the screen: slider, 0°/90°/180° or sweep. | Robot, an SG90 servo and a Grove cable. |
| [09 · Sensor kit](demos/09-sensor-kit/) | The KY-0xx modules of the 37-sensor kit on PORT.B: digital, analog with a graph, DHT11, DS18B20, encoder, joystick and outputs (LEDs, relay, buzzer). | Robot, the kit and 10 and 20 kΩ resistors for the dividers. |
| [10 · Voice + gamepad](demos/10-voice-gamepad/) | Demo 02 (Gemini voice) with the head moved by a PS4 gamepad paired with the laptop over Bluetooth, no phone needed. | Robot, laptop with Bluetooth, PS4 gamepad and a [Gemini key](https://aistudio.google.com/apikey). |
| [11 · Tank](demos/11-tank/) | You drive the Keyestudio KS0428 tank (Arduino UNO + HM-10) with the PS4 gamepad from the laptop, over Bluetooth LE. | KS0428 tank, laptop with Bluetooth and PS4 gamepad. |
| [12 · Mini TV](demos/12-mini-tv/) | The robot becomes a tiny old TV: it plays videos from its microSD card, and you tap the screen to change the channel. The laptop converts any video and sends it over Wi-Fi. | Robot with a microSD card (FAT32), laptop with ffmpeg. |
| [13 · Web server](demos/13-web-server/) | The robot serves its own web page: camera, head (360°), face, LEDs, speaker and every sensor live over a WebSocket, from any phone or laptop. Optional MQTT, and it shows up in Home Assistant. | Robot and a phone or laptop on the same Wi-Fi. For MQTT, a broker (Mosquitto). |

```
demos/                 one folder per demo: firmware, laptop code and README
lib/StackChanHW/       shared C++: the robot's servos, LEDs, battery and camera
lib/StackChanLink/     shared C++: UDP link robot <-> laptop
lib/python/stackchan/  shared Python: protocol and base relay
config/                your Wi-Fi (wifi_secrets.h: setup creates it and it is never committed)
docs/                  the robot inside, the development environment and the datasheets
tools/                 flash any firmware, run demos, serial log
```

## Setup (once)

The script gets the computer ready even if you have never programmed a microcontroller:
- Git, Python and Visual Studio Code with PlatformIO;
- the ESP32 and ESP32-S3 compilers (about 1 GB, better at home before the course);
- the USB port permissions and the Python libraries.

At the end it asks for the Wi-Fi and checks that every firmware builds.

**Linux (Ubuntu/Debian):**
```bash
sudo apt install -y git
git clone https://github.com/rhyloo/StachChan-Course.git stackchan-course
cd stackchan-course
./setup.sh
```
If it adds you to the `dialout` group, log out and back in.

**Windows 10/11:**
1. Install [Git](https://git-scm.com/download/win).
2. Clone the repo, or download it as a ZIP from GitHub.
3. **Double-click `setup.bat`**.

Then open the course in VS Code: `code stackchan-course.code-workspace`. What you need to know about VS Code, PlatformIO and the USB port is in [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md).

**Wi-Fi.** The robot, the laptop and the phone must be on the **same 2.4 GHz Wi-Fi network**.
- Networks that isolate clients from each other, like eduroam or guest networks, don't work. Use your own router or the phone as a hotspot.
- To change network: `./setup.sh --wifi` (or `setup.bat -Wifi`) and flash again.

## Run a demo

Connect the robot over USB-C and run the demo's `run.sh` (on Windows, `run.bat`). It checks that everything is ready, builds, flashes the firmware to the robot and starts what the demo needs on the laptop:

```bash
./demo.sh                                    # menu: pick the demo by its number (Windows: demo.bat)
demos/02-gemini-voice/run.sh               # flash the robot and start the demo
demos/02-gemini-voice/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/02-gemini-voice/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/02-gemini-voice/run.sh --no-flash    # start the demo with what the robot already has
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`. If the robot isn't found on its own, add `--usb /dev/ttyACM0` (Windows: `-Usb COM5`). `run.sh --help` lists every option of the demo. With `--debug`, the robot's serial goes to `logs/robot-*.log` and the relay, with full detail, to `logs/relay-*.log`: those are the files to look at (or attach) when something fails.

## Flash by hand

```bash
./tools/flash.sh                                    # list the firmwares
./tools/flash.sh demos/01-vr-cardboard/firmware     # robot
./tools/flash.sh demos/03-espnow-beacons/beacon --env beacon2 /dev/ttyUSB0
```

On Windows it is `tools\flash.bat` with the same arguments, except that you write `-Env beacon2` instead of `--env beacon2`. You can also use the PlatformIO buttons in VS Code.

## Documentation

- [docs/ROBOT.md](docs/ROBOT.md): the robot inside (components, pins, servos, limits, recovery). Datasheets, schematics and manuals in [docs/hardware/](docs/hardware/).
- [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md): VS Code, PlatformIO, serial monitor, AI assistants and USB problems.
- [demos/01-vr-cardboard/PROTOCOL.md](demos/01-vr-cardboard/PROTOCOL.md): the UDP messages between robot, laptop and phone.
