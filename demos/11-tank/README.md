# Demo 11 · The Keyestudio tank with the PS4 gamepad

The **Keyestudio Mini Tank Robot V2 (KS0428)** is a tracked tank with an Arduino UNO board, an L298P motor driver and an HM-10 Bluetooth module. In this demo you drive it with the **PS4 gamepad paired with the laptop**, and L2 / R2 make the Stack-chan honk or fire a Star Wars style blaster through its speaker. The gamepad only drives the tank: it doesn't move Stack-chan's head. You'll learn how a Bluetooth LE "serial port" works, how to make a robot stop safely when the link drops and how to play your own sounds on the robot. With `run.sh vr` Stack-chan rides on top: you see through its camera with the VR viewer, it turns its head with yours and you can talk to it (demo 02).

```
 PS4 gamepad ))) Bluetooth ))) laptop ))) Bluetooth LE ))) HM-10 ── Arduino UNO ── L298P ── tracks
                               drive.py                    (D0/D1, 9600 bd)   tank/src/main.cpp
                                  │
                                  └── Wi-Fi, AUDIO_OUT (horn, blaster) ──> Stack-chan's speaker (demo 02 firmware)
```

## What you need

- **The KS0428 tank**, assembled, with charged batteries and the **HM-10** module plugged in.
- **The PS4 gamepad paired with the laptop:** `tools/pair_gamepad.sh`, as in demo 10.
- **Bluetooth on the laptop.** The gamepad and the tank work at the same time: the gamepad over classic Bluetooth and the tank over Bluetooth LE.
- **A USB cable** to flash the tank's Arduino, only the first time.
- **For the sounds (optional): the Stack-chan** on the same Wi-Fi as the laptop, with demo 02's firmware (`run.sh` flashes it if the robot is on USB). Without it the tank drives the same, just silent.

## Run it

The tank's Arduino has its own firmware (`run.sh tank`). The Stack-chan, only needed for the sounds, gets demo 02's firmware, which has the audio. The laptop side is `laptop/drive.py`.

1. **Flash the tank.** Only once.
   ```bash
   demos/11-tank/run.sh tank          # Windows: demos\11-tank\run.bat tank
   ```
   It asks you to **remove the Bluetooth module** first: the HM-10 is wired to the same serial port (D0/D1) as the USB, and with it plugged in the upload fails. When it's done, **put it back**.
2. **Switch the tank on.** The HM-10 LED blinks while it waits for a connection.
3. **Drive:**
   ```bash
   demos/11-tank/run.sh               # Windows: demos\11-tank\run.bat
   ```
   If the Stack-chan is on USB, it first flashes demo 02's firmware on it; if not, it skips that. Then it looks for the tank, the gamepad and the robot. When they connect, the HM-10 LED stays on and the gamepad rumbles.

The usual modes apply to the Stack-chan: `--no-flash` (`-NoFlash`) if it already has demo 02's firmware, `--flash-only` (`-FlashOnly`) to flash it and stop. `--debug` does nothing here. `run.sh --help` lists every option.

## Try it

| Gamepad | What it does |
|---|---|
| **Left stick** | Drive: forward/back and turn. The more you tilt it, the faster. |
| **D-pad** | Forward, back and spin on the spot, at a fixed speed |
| **R1** (hold) | Full speed (without R1, 60 %: easier to control) |
| **✕** | Stop |
| **L2** | Horn ("meep-meep!") on the Stack-chan's speaker |
| **R2** | Blaster shot, Star Wars style, on the Stack-chan's speaker |

At the bottom of the terminal you see the speed of each track (`left` and `right`, from -255 to 255) and the robot playing the sounds.

## Stack-chan on top: VR viewer + voice

```
 phone (Stack-chan VR app) <── UDP 5601 ──> relay/main.py <── UDP 5600 ──> Stack-chan (demo 02 firmware)
   video in, head pose out                  │  demo 02 relay: video, head, Gemini Live
                                             └── Bluetooth LE ──> tank      (PS4 gamepad on the laptop)
```

Put the Stack-chan on the tank and run:

```bash
export GEMINI_API_KEY="your-key"     # as in demo 02
demos/11-tank/run.sh vr              # Windows: demos\11-tank\run.bat vr
```

It flashes demo 02's firmware to the Stack-chan (this time the robot is needed: connect it over USB, or use `--no-flash` if it already has it) and starts `relay/main.py`: demo 02's relay plus the tank. Then open the Stack-chan VR app on the phone, as in demo 01, and put it in the viewer.

- **The phone moves the head** (Head mode) and **the gamepad moves the tank**, with the same controls as above.
- **You talk to it** as in demo 02, hands-free: "what do you see?", "turn on night vision", "save a photo". **L1** mutes or unmutes the mic (with `--push-to-talk`, hold it to talk) and **Options** mutes it too.
- **L2 / R2** play the horn and the blaster through the same audio queue as Gemini's voice.
- **The gamepad is paired with the laptop,** not with the phone as in demo 01. A DualShock 4 only remembers one device: if you used it with the phone, pair it again with `tools/pair_gamepad.sh`.

`run.sh vr --debug` also saves `logs/robot-*.log` and `logs/relay-*.log`. The relay options (`--tank`, `--robot`, `--push-to-talk`, `--no-vision`...) are listed by `.venv/bin/python demos/11-tank/relay/main.py --help`.

## How it works

**Tank firmware** (`tank/src/main.cpp`, Arduino UNO, PlatformIO `atmelavr`):
- **One command per line:** `M<left>,<right>`, from -255 to 255. Example: `M200,-200` spins right on the spot.
- **It stops by itself:** if no other command arrives within 400 ms, the tank stops. If the program closes or the Bluetooth drops, it doesn't keep driving.
- **Soft starts:** the speed changes by 20 PWM steps every 10 ms at most. Sudden starts draw current spikes that can reset the board or the HM-10.
- **It still understands the "keyes BT car" app** (`F`, `B`, `L`, `R`, `S`).
- **Motors:** a direction pin (D13 left, D12 right; LOW = forward) and a speed PWM (D11 and D3).

**Laptop:**

| File | What for |
|---|---|
| `laptop/tank.py` | The `Tank` class: finds the HM-10 (service `FFE0`), connects with [bleak](https://github.com/hbldh/bleak), reconnects if the link drops and writes the command to the `FFE1` characteristic 12 times per second. It can be reused as it is to add the tank to demo 02. |
| `laptop/drive.py` | Reads the gamepad with pygame and turns the stick into each track's speed ("arcade": forward + turn). `FORWARD = -1` flips forward/back for this tank, which went backwards when asked forward. L2 / R2 play a sound when pressed. |
| `laptop/speaker.py` | `RobotSpeaker`: pairs with the Stack-chan like a relay does (it answers its HELLO), turns its mic off (`listen manual`) and sends a sound as AUDIO_OUT, 16-bit PCM at 24 kHz. |
| `relay/main.py` | `run.sh vr`: demo 02's relay (`voice.py`, `hooks.py`, `processing.py`) with `tank_pad.setup` added. |
| `relay/tank_pad.py` | The gamepad inside the relay: drives the tank with `drive.py`'s `tracks()` and plays the sounds through `voice.py`'s audio queue. Only one program can talk to the robot, so `RobotSpeaker` can't be used next to the relay. `TankVoice` tells Gemini it is riding a tank. |
| `../10-voice-gamepad/relay/sounds.py` | The sounds, synthesized with numpy: `horn()` and `blaster()` (a sweep from 2600 to 180 Hz with a metallic ring). |

**The HM-10 is a serial port over Bluetooth.** What the laptop writes to its `FFE1` characteristic comes out of its TX pin and reaches the Arduino through `Serial`, as if it came over a cable. That's why the firmware is a normal serial-port program.

## Options

```bash
demos/11-tank/run.sh --tank BT05     # if there are several Bluetooth modules nearby (in class, other tanks)
```

`--tank` takes the Bluetooth name or the MAC of your tank's HM-10. `--robot stackchan-4F48` (or its IP) picks the Stack-chan if there are several, and `--volume 0-255` sets its speaker (default 120; louder can brown out the robot).

## Troubleshooting

| Symptom | What to do |
|---|---|
| *Can't find it over Bluetooth* | Charged batteries? HM-10 module plugged in? If the phone's "keyes BT car" app is connected to the tank, disconnect it: the HM-10 only allows one connection. |
| Flashing the firmware fails | Remove the Bluetooth module. |
| One track runs backwards | The motor is wired the other way round on the terminal block: swap its two wires, or flip the sign of that track in `tank/src/main.cpp`. |
| Forward and back are swapped | Change `FORWARD` in `laptop/drive.py` (1 or -1). |
| It turns the wrong way | Swap the tracks: `ML_*` with `MR_*` in `main.cpp`. |
| `run.sh vr`: the phone doesn't find the relay | Phone and laptop on the same Wi-Fi as the robot. Close any other relay or `run.sh` (without `vr`): the robot only pairs with one. |
| L2 / R2 make no sound | The terminal says `no robot`: the Stack-chan isn't on the Wi-Fi, has another demo's firmware, or another relay is open (it only listens to one). |
| It stops in jerks | Low batteries, or the Bluetooth drops (more than 400 ms without commands): get closer. |

## Ideas to extend

- **Drive by voice:** a `drive` function in `TankVoice` ("go forward a bit") that calls `Tank.move` for a moment.
- **The kit's ultrasonic sensor:** don't let the tank move forward if there's an obstacle close, even if you tell it to.
- **The tank's LED matrix:** an arrow showing where it's going.
