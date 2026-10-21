# UDP protocol of the demos

Everything is UDP on the local network, in **big-endian** byte order. Every datagram starts with the same 4-byte header and is at most **1200 bytes** long (so IP never fragments it).

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 2 | magic | `0x53 0x56` ("SV") |
| 2 | 1 | version | `1` |
| 3 | 1 | type | see the tables |

There are two links:

- **Phone ↔ laptop**, port **5601** on the laptop. The app defines it ([rhyloo/StackChan-VR](https://github.com/rhyloo/StackChan-VR), `net/Protocol.kt`): don't change it without changing the app.
- **Robot ↔ laptop**, port **5600** on both sides. It's ours: `lib/StackChanLink/src/link.cpp` (robot) and `lib/python/stackchan/protocol.py` (laptop) must change together.
- **Demo 03**: the receiver broadcasts to UDP **5602** (see the end).

## Robot ↔ laptop (5600)

| Type | Name | Direction | Content after the header |
|---|---|---|---|
| `0x20` | HELLO | robot → laptop | JSON `{"name":"stackchan-4F48","fw":"1.0.0","ip":"192.168.0.24","limits":{"pan":[-3.1416,3.1416],"tilt":[-0.12,1.2763]}}`. Every 1 s: to `255.255.255.255:5600` while it has no laptop, and straight to the laptop once it has one. |
| `0x21` | ACK | laptop → robot | 1 flags byte; bit 0 = someone is watching (phone connected, `--show` or `--record`). Reply to every HELLO from the paired robot, and as soon as the flags change. An empty ACK means "always send video". |
| `0x22` | VIDEO | robot → laptop | Same as the phone's VIDEO_CHUNK: `frameId` u32, `chunkIndex` u16, `chunkCount` u16, `captureMs` u32 and up to 1184 bytes of JPEG. |
| `0x23` | TELEMETRY | robot → laptop | JSON at 2 Hz: `batt_v`, `batt_pct`, `current_ma` (INA226; negative = charging), `video` (0/1), `pan`, `tilt` (rad), `fps`, `jpeg_bytes`, `rssi`, `heap`, `psram`, `send_errors`, `quality`. |
| `0x24` | POSE | laptop → robot | Binary: `seq` u32, `pan` f32, `tilt` f32 (radians, IEEE-754 big-endian). |
| `0x25` | CMD | laptop → robot | ASCII text: `home`, `torque_on`, `torque_off`, `quality <5-95>`, `tone <Hz> <ms>`, `leds <r> <g> <b>`, `text <message>`, `brightness <0-255>`, `wifi_sleep <0/1>`. Demo 02: `mic on`, `mic off`. Demo 05: `nfc write <UTF-8 text>`, `nfc read`, `nfc cancel`. Demo 07: `photo ack <id>`, `photo status <text>`, `photo error <text>`. |
| `0x26` | AUDIO_IN | robot → laptop | Demo 02. `seq` u32 + 16-bit little-endian PCM, 16 kHz mono, 20 ms (640 bytes). |
| `0x27` | AUDIO_OUT | laptop → robot | Demo 02. `seq` u32 + 16-bit little-endian PCM, 24 kHz mono, up to 1180 bytes. The robot buffers 200 ms before playing. |
| `0x28` | NFC | robot → laptop | Demo 05. JSON. Card read: `{"event":"tag","ok","uid","type","ndef","text"}`. Result of `nfc write`: `{"event":"write","ok","uid","text","error"}`. |
| `0x29` | PHOTO | robot → laptop | Demo 07. Same as VIDEO: `photoId` u32, `chunkIndex` u16, `chunkCount` u16, `captureMs` u32 and up to 1184 bytes of JPEG. The laptop confirms with `photo ack <id>`; if not, the robot sends it again (3 tries). |
| `0x2A` | PHOTO_EVENT | robot → laptop | Demo 07. JSON: `{"event":"filter","photo","name"}`, `{"event":"bitmap_ok","job"}`, `{"event":"printed","ok","error"}`. |
| `0x2B` | PRINT_BITMAP | laptop → robot | Demo 07. `job` u32, `chunkIndex` u16, `chunkCount` u16, `width` u16 (384), `height` u16 and up to 1000 bytes of 1-bit rows (48 bytes per row, least significant bit first, 1 = black). The laptop resends until `bitmap_ok` arrives. |

Rules:

- **Pairing.** The robot pairs with the first laptop that sends it ACK, POSE or CMD, and ignores the rest while that one keeps talking to it (3 s). The laptop pairs with the robot given in `--robot` or, if none is given, with the first one it hears. That way several teams share the classroom Wi-Fi.
- The robot only sends video while the laptop has talked to it in the last 3 s **and** the last ACK says someone is watching (it saves battery).
- A POSE with an old or repeated `seq` is dropped, except after 2 s without POSE (because the relay resets the counter when it starts).
- The robot clamps pan and tilt to its limits and applies a speed limiter (about 94°/s in pan and 47°/s in tilt), because sudden jumps cause current spikes that reset the base.
- The GC0308 has no hardware JPEG encoder: the robot captures RGB565 QVGA (320×240) and compresses it with `frame2jpg`.

## Phone ↔ laptop (5601)

Summary; the details are in the app's specification.

| Type | Name | Direction |
|---|---|---|
| `0x13` | DISCOVER | phone → broadcast, every 1 s while searching |
| `0x04` | ANNOUNCE | laptop → phone: `{"name","proto":1,"limits"}` |
| `0x10` | HELLO | phone → laptop, every 1 s; with no HELLO for 3 s the laptop stops sending to it |
| `0x01` | VIDEO_CHUNK | laptop → phone |
| `0x02` | TELEMETRY | laptop → phone, 2 Hz: `robot_online`, `batt_v`, `batt_pct`, `pan`, `tilt`, `robot_fps`, `relay_fps`, `limits` |
| `0x12` / `0x03` | PING / PONG | latency (RTT) |
| `0x11` | CONTROL | phone → laptop, up to 20 Hz: `{"seq","mode","pan","tilt"}` → turned into POSE |
| `0x14` | BUTTON | phone → laptop: gamepad buttons → `hooks.on_button` |
| `0x15` | COMMAND | phone → laptop: `home` → CMD `home` |
| `0x05` | SUBSCRIBE | laptop → phone: which sensors it wants and how often |
| `0x16`–`0x19` | SENSOR_DATA, DEVICE_INFO, EVENT, BLE_SCAN | phone → laptop → `hooks.on_sensors` / `hooks.on_event` |
| `0x06` | ACTUATE | laptop → phone: `hud_message`, `vibrate`, `torch`, `tone`, `say`, `device_info` |

## Demo 03: beacons (5602)

The receiver (robot) broadcasts to UDP 5602, 5 times per second, a datagram with the common header, type `0x30`, and JSON:

```json
{"robot":"DC:54:75:..","t":123456,"beacons":[{"id":1,"rssi":-61.4,"raw":-63,"n":812}]}
```
`rssi` is the exponential average; `raw`, the last reading; `n`, the packets received from that beacon. The ESP-NOW messages between beacons and robot are in `demos/03-espnow-beacons/espnow_msg.h`.
