# Demo 13 · Stack-chan as a web server

The robot serves its own web page. Open `http://stackchan-XXXX.local` on any phone or laptop on the same Wi-Fi and you get its camera, its head (all the way round), its face, LEDs and speaker, and every sensor live: battery, current, motion, light, proximity, touch… No relay, no app, no Internet. If you have an MQTT broker, it also publishes everything and takes orders through it, and it shows up in Home Assistant on its own.

You'll learn how an ESP32 serves HTTP, how a **WebSocket** keeps a page and a device talking both ways in real time, how a camera becomes a video in a plain `<img>` (MJPEG), and what **MQTT** is for.

```
 browser ── HTTP GET / ──────────────> robot :80   the page (src/web/index.html, built into the firmware)
         <══ WebSocket /ws ══════════> robot :80   state 5 times a second  ⇄  commands {"cmd":"head","pan":90}
         ── GET :81/stream ──────────> robot :81   camera, MJPEG (~10 fps)
                                        │
 MQTT broker <── stackchan/XXXX/state ──┤  every 2 s      (optional)
 (Mosquitto)  ── stackchan/XXXX/cmd ───>┘  same commands
     └── Home Assistant (MQTT discovery: the robot appears as a device)
```

## What you need

- **The robot** and a **phone or laptop** on the same 2.4 GHz Wi-Fi.
- **For MQTT (optional):** a broker on the network. The easiest is Mosquitto on the laptop (see *MQTT* below). Home Assistant is optional too.

## Run it

```
demos/13-web-server/run.sh               # flash the robot and show what it says (its address)
demos/13-web-server/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/13-web-server/run.sh --debug       # the same here: the serial output is shown and saved to logs/robot-*.log
demos/13-web-server/run.sh --no-flash    # show the serial output of what the robot already has
demos/13-web-server/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

When it connects, the robot prints `Open http://stackchan-XXXX.local or http://192.168.x.x` and shows the address at the bottom of its screen. **Tap the screen** for a QR code with it: point the phone's camera at it. Once flashed, the robot doesn't need the laptop: power it from its battery and it keeps serving the page.

If `stackchan-XXXX.local` doesn't open (some Android phones and Windows don't resolve `.local`), use the IP.

## Try it

- **Camera:** *Start video*. *Photo* opens a picture (`/photo.jpg`).
  - *Enhance*: your browser averages the last 3 frames (less noise in dim light) and stretches the levels. Nothing extra for the robot.
  - *Night*: the camera aims brighter and amplifies more. Brighter, noisier.
  - *HDR photo* (`/hdr.jpg`): three shots at 0.35×, 1× and 2.8× the exposure, fused on the robot, each pixel taken mostly from the shot where it is best exposed. It shines with bright and dark areas at once (a lamp, a window); in a dark room all three are dark.
  - None of this replaces light: in the dark the sensor has nothing to work with.
- **Head:** drag the blue dot around the dial. Top is the front; the head turns all the way round. The green needle is where the head looks now; the red slice behind is the zone the servo can't see, crossed fast (see `docs/ROBOT.md`). The slider is the tilt, from −7° to 73°.
- **Face:** the six moods of demo 02.1.
- **LEDs:** any color (the firmware caps the brightness). **Screen:** a short text over the face.
- **Sound:** a beep, a scale, R2-D2 chirps, an alarm. The page plays them note by note with `beep` commands.
- **Sensors:** wave a hand over the camera (proximity), cover it (light), tilt or turn the robot (the bubble and the rotation speed), tap its screen, unplug the USB (the current goes from negative, charging, to positive).
- **Two browsers at once:** open the page on the phone and the laptop. Both see the same state and both can command it.

## How it works

| File | What for |
|---|---|
| `firmware/src/main.cpp` | Wi-Fi, mDNS (`stackchan-XXXX.local`), the sensors (read 10 times a second), the commands and the screen. `loop()` owns I2C and the screen: the web and MQTT tasks only leave commands in a queue. |
| `firmware/src/web.cpp` | ESP-IDF's `esp_http_server`: the page, the WebSocket and the photo on port 80; the MJPEG stream on port 81, a server of its own so a video that never ends doesn't block the rest. |
| `firmware/src/web/index.html` | The page: plain HTML and JavaScript, no libraries. `board_build.embed_txtfiles` in `platformio.ini` builds it into the firmware as it is: edit it and flash. |
| `firmware/src/mqtt.cpp` | ESP-IDF's MQTT client: broker saved on the robot (NVS), state, commands, last will and Home Assistant discovery. |
| `firmware/src/camera_extra.cpp` | The GC0308's exposure registers (datasheet in `docs/hardware/datasheets`): night mode and the HDR photo. The exposure of the long shot never goes past one frame (window + vertical blanking), or the frame breaks; the rest is made up with gain. |
| `firmware/src/face.cpp` | The faces. |
| `laptop/mosquitto.conf` | A Mosquitto config that accepts the robot from the network. |

**WebSocket vs plain HTTP.** With HTTP the browser asks and the robot answers, once. A WebSocket stays open: the robot pushes its state 5 times a second without being asked, and the page sends each command as it happens. That's what makes the dial and the sensors feel live.

**Commands** (WebSocket, MQTT `…/cmd` and the serial monitor, all the same JSON):

| JSON | What it does |
|---|---|
| `{"cmd":"head","pan":90,"tilt":10}` | Head to that direction, in degrees. 0, 0 = straight ahead; pan + = the robot's left, ±180 = behind. A missing angle stays as it is. |
| `{"cmd":"center"}` | Straight ahead |
| `{"cmd":"mood","mood":"happy"}` | `neutral`, `happy`, `sad`, `surprised`, `angry`, `sleepy` |
| `{"cmd":"leds","r":0,"g":255,"b":128}` | Base LEDs |
| `{"cmd":"text","text":"Hello!"}` | Text on the screen (empty = remove it) |
| `{"cmd":"beep","hz":1000,"ms":150}` | A tone on the speaker |
| `{"cmd":"camera","night":true}` | Camera night mode on / off |
| `{"cmd":"mqtt","host":"192.168.0.22","port":1883}` | Sets the broker and saves it on the robot (empty host = MQTT off) |

**The state** the robot sends: `batt`, `pct`, `ma` (battery), `pan`, `tilt` (head), `acc`, `pitch`, `roll`, `gyro` (IMU), `lux`, `prox`, `touch`, `rssi`, `up`, `heap`, `temp` (chip), `mood`, `night`, `text`, `ws` (WebSocket clients), `mqtt`, `broker`.

## MQTT

MQTT is a tiny publish/subscribe protocol for devices: they don't talk to each other but to a **broker**, publishing messages on **topics** and subscribing to the topics they care about. It is how Home Assistant, Node-RED and most home-automation gear talk.

1. **A broker on the laptop** (Linux; on Windows, the installer from mosquitto.org):
   ```bash
   sudo apt install mosquitto mosquitto-clients
   sudo systemctl stop mosquitto                              # the system one only listens on localhost
   mosquitto -c demos/13-web-server/laptop/mosquitto.conf -v
   ```
2. **Tell the robot where it is:** on the page, *MQTT*, the laptop's IP and *Save*. The status turns *connected*. It's saved on the robot.
3. **Watch it and command it** from another terminal:
   ```bash
   mosquitto_sub -h localhost -t 'stackchan/#' -v                 # everything it publishes
   mosquitto_pub -h localhost -t stackchan/XXXX/cmd -m '{"cmd":"mood","mood":"happy"}'
   mosquitto_pub -h localhost -t stackchan/XXXX/cmd -m '{"cmd":"head","pan":180}'
   ```
4. **Home Assistant** (if you have it, with its MQTT integration pointing to the same broker): the robot appears under *Devices* as *Stack-chan XXXX*, with battery, current, light, proximity, chip temperature, Wi-Fi and head pitch, a mood selector, the head angles, a screen text and a *Center head* button. If the robot disappears, it shows as unavailable (MQTT last will).

**Why not Zigbee?** Zigbee (and Thread / Matter over Thread) use an 802.15.4 radio, and the ESP32-S3 doesn't have one. It would need an ESP32-C6 or ESP32-H2 as a coprocessor, or a Zigbee USB stick on the Home Assistant side. MQTT over Wi-Fi does the same job here with what the robot already has.

## Troubleshooting

| Symptom | What to do |
|---|---|
| `stackchan-XXXX.local` doesn't open | Use the IP (bottom of the robot's screen, or the QR code). |
| The page loads but nothing updates | The WebSocket didn't connect: the dot at the top is red. Some guest/enterprise networks block it; use your own router or the phone as a hotspot. |
| The video doesn't start or freezes | Only one viewer at a time: close it in other tabs. Weak Wi-Fi lowers the fps. |
| MQTT stays in *connecting* | Mosquitto only listens on localhost by default: start it with `laptop/mosquitto.conf`. Check the IP and that the laptop's firewall allows port 1883. |
| The robot resets when moving fast with LEDs on | Same power limits as always (`docs/ROBOT.md`): keep the LEDs moderate on USB power. |

## Ideas to extend

- **A joystick for the tank** of demo 11 on this page, through MQTT.
- **Charts:** keep the last minutes of sensors in the page and draw them on a `<canvas>`.
- **Speak from the page:** record with the browser's microphone and send the audio to the robot's speaker (as demo 02 does over UDP).
- **Automations:** in Home Assistant, "when the robot's light goes below 10 lx, make it sleepy".
