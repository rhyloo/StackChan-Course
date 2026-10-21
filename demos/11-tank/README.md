# Demo 11 · The Keyestudio tank with the PS4 gamepad

The **Keyestudio Mini Tank Robot V2 (KS0428)** is a tracked tank with an Arduino UNO board, an L298P motor driver and an HM-10 Bluetooth module. In this demo you drive it with the **PS4 gamepad paired with the laptop**. You'll learn how a Bluetooth LE "serial port" works and how to make a robot stop safely when the link drops. It is the first step towards carrying Stack-chan on top, with demo 02 and the VR viewer.

```
 PS4 gamepad ))) Bluetooth ))) laptop ))) Bluetooth LE ))) HM-10 ── Arduino UNO ── L298P ── tracks
                               drive.py                    (D0/D1, 9600 bd)   tank/src/main.cpp
```

## What you need

- **The KS0428 tank**, assembled, with charged batteries and the **HM-10** module plugged in.
- **The PS4 gamepad paired with the laptop:** `tools/pair_gamepad.sh`, as in demo 10.
- **Bluetooth on the laptop.** The gamepad and the tank work at the same time: the gamepad over classic Bluetooth and the tank over Bluetooth LE.
- **A USB cable** to flash the tank's Arduino, only the first time.

## Run it

This demo has no robot firmware: the firmware goes to the tank's Arduino, and the laptop side is `laptop/drive.py`.

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
   It looks for the tank and the gamepad. When they connect, the HM-10 LED stays on and the gamepad rumbles.

About the usual modes: `--flash-only` (`-FlashOnly` on Windows) does the same as `run.sh tank`, it flashes the tank's Arduino. `--debug` and `--no-flash` don't apply here, since there is no robot. `run.sh --help` lists every option.

## Try it

| Gamepad | What it does |
|---|---|
| **Left stick** | Drive: forward/back and turn. The more you tilt it, the faster. |
| **D-pad** | Forward, back and spin on the spot, at a fixed speed |
| **R1** (hold) | Full speed (without R1, 60 %: easier to control) |
| **✕** | Stop |

At the bottom of the terminal you see the speed of each track (`left` and `right`, from -255 to 255).

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
| `laptop/drive.py` | Reads the gamepad with pygame and turns the stick into each track's speed ("arcade": forward + turn). |

**The HM-10 is a serial port over Bluetooth.** What the laptop writes to its `FFE1` characteristic comes out of its TX pin and reaches the Arduino through `Serial`, as if it came over a cable. That's why the firmware is a normal serial-port program.

## Options

```bash
demos/11-tank/run.sh --tank BT05     # if there are several Bluetooth modules nearby (in class, other tanks)
```

`--tank` takes the Bluetooth name or the MAC of your tank's HM-10.

## Troubleshooting

| Symptom | What to do |
|---|---|
| *Can't find it over Bluetooth* | Charged batteries? HM-10 module plugged in? If the phone's "keyes BT car" app is connected to the tank, disconnect it: the HM-10 only allows one connection. |
| Flashing the firmware fails | Remove the Bluetooth module. |
| One track runs backwards | The motor is wired the other way round on the terminal block: swap its two wires, or flip the sign of that track in `tank/src/main.cpp`. |
| It turns the wrong way | Swap the tracks: `ML_*` with `MR_*` in `main.cpp`. |
| It stops in jerks | Low batteries, or the Bluetooth drops (more than 400 ms without commands): get closer. |

## Ideas to extend

- **Stack-chan on top,** with demo 02 and the VR viewer: the phone moves the head and the gamepad moves the tank. That's the next step.
- **The kit's ultrasonic sensor:** don't let the tank move forward if there's an obstacle close, even if you tell it to.
- **The tank's LED matrix:** an arrow showing where it's going.
