# StackChanLink

C++ library (Arduino, ESP32-S3) for the UDP link between the robot and the laptop relay. It pairs with the first relay that answers, streams the camera only while someone watches, sends telemetry and moves the head from POSE packets. The protocol is in [demos/01-vr-cardboard/PROTOCOL.md](../../demos/01-vr-cardboard/PROTOCOL.md). It depends on `StackChanHW`.

## Main API (`link.h`)

| Function | What it does |
|---|---|
| `linkStart()` | Call once Wi-Fi is up. Starts the RX, hello and video tasks (core 0). |
| `linkTakePending(&cmds)` | Commands that touch I2C or the display (brightness, LEDs, Wi-Fi sleep). Call it from `loop()` and apply them there. |
| `linkSetBattery(v, pct, mA)` | Battery values to send in the telemetry. |
| `linkGetStats(&stats)` | Relay online, IP, fps, frame size, last `text` command: handy for the screen. |
| `linkVideoWanted()`, `linkRelayOnline()`, `robotId()` | State and the robot name (`stackchan-XXXX`). |
| `linkSetCommandHandler(fn)` | Extension point: text commands the link doesn't know (e.g. `mic on`). |
| `linkSetMessageHandler(fn)` | Extension point: binary message types the link doesn't know (e.g. audio). |
| `linkSend(type, payload, len)` | Sends your own message type to the relay. |

Handlers run on the network task: keep them short and don't touch I2C or the display.

Timings and camera settings are in `link_config.h`. If you change a robot message, change `lib/python/stackchan/protocol.py` at the same time.

## Who uses it

The firmwares of demos 01, 02, 05 and 07 (demo 10 runs demo 02's firmware).
