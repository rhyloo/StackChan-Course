# Demo 08 · An SG90 servo on the Grove port

You plug an **SG90** micro servo into one of the robot's Grove ports and move it from the touch screen: by dragging a slider, with 0°, 90° and 180° buttons, or in sweep mode from one end to the other. It is a way to give Stack-chan an arm, a flag, a lid that opens... You'll learn how a hobby servo is driven with a PWM pulse.

## What you need

- **The robot.** No Wi-Fi or laptop needed, except to flash it.
- **An SG90 servo** (or an MG90S, the same but with metal gears).
- **A Grove cable** (HY2.0-4P) with one free end, or a Grove to Dupont pins cable.

### Wiring

The robot has three Grove ports (HY2.0-4P). The servo goes on **PORT.B**: its pins G9 and G8 are free, while PORT.A carries an I2C bus and PORT.C a UART.

| Port | Black | Red | Yellow | White |
|---|---|---|---|---|
| PORT.A | GND | 5 V | G2 | G1 |
| **PORT.B** | **GND (−)** | **5 V (+)** | **G8** | **G9** |
| PORT.C | GND | 5 V | G17 | G18 |

| SG90 wire | What it is | PORT.B |
|---|---|---|
| **Black** (or brown) | Ground, GND | Black (−) |
| **Red** | Power, 5 V | Red (+) |
| **Blue** (or orange or yellow) | Signal | White (G9) or yellow (G8) |

The firmware puts the same pulse on G9 and G8, so the signal works on either one. Tested with a real SG90.

> ⚠️ SG90 wire colors change between makers. Red is always 5 V and the darkest one (black or brown) is ground: **the one left is the signal**. **Never put the signal on +5 V**.

## Run it

1. **With the robot off**, connect the servo to PORT.B as in the table.
2. **Plug the robot in over USB-C and run:**

```bash
demos/08-servo-sg90/run.sh               # flash the robot and start the demo
demos/08-servo-sg90/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/08-servo-sg90/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/08-servo-sg90/run.sh --no-flash    # start the demo with what the robot already has
demos/08-servo-sg90/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

This demo has no relay: the default run already opens the robot's serial monitor and saves it in `logs/`, so `--debug` does the same. On start-up the servo goes to 90°.

## Try it

1. **Move the servo** from the screen:
   - **drag the slider** to choose the angle;
   - **0°, 90°, 180°:** go straight to that position;
   - **Sweep:** goes back and forth from one end to the other until you tap "Stop".

   The dial shows in orange where the arm is, in blue where it is going, and the pulse width in microseconds.
2. **From the computer**, with `.venv/bin/pio device monitor -d demos/08-servo-sg90/firmware`, type `angle 45` and Enter.

## How it works

```
 touch screen ──> ESP32-S3: 0.5 to 2.4 ms pulse every 20 ms ──G9 and G8 (PORT.B)──> SG90
```

**How a servo is driven.** The servo gets one pulse every 20 ms (50 Hz). What matters is **how long the pulse lasts**: about 0.5 ms takes it to 0°, 1.45 ms to 90° and 2.4 ms to 180°. Inside it has a motor, some gears and a potentiometer that measures where it is: it corrects itself until it reaches the requested angle. The ESP32 makes the pulse in hardware with its **LEDC** peripheral, the one also used to dim LEDs (PWM).

`firmware/src/main.cpp`:

| Part | What for |
|---|---|
| `ledcSetup(0, 50, 14)` + `ledcAttachPin(9, 0)` and `(8, 0)` | PWM at 50 Hz with 14 bits: the 20 ms period is split into 16,384 steps of 1.2 µs. One channel can drive several pins: the pulse comes out of G9 and G8. |
| `writeServo()` | Angle → microseconds (`MIN_US`...`MAX_US`) → LEDC value. |
| `SPEED_DEG_S` | Speed limiter: the servo moves at most 180° per second, in steps every 20 ms. Sudden jumps pull current peaks, just like with the head servos. |
| `drawDial()` | The dial, drawn in a sprite (memory) and sent to the screen in one go so it doesn't flicker. |

## Troubleshooting

| Symptom | What to do |
|---|---|
| It doesn't move | Check the signal goes to the yellow or white wire of **PORT.B**. Check the 5 V: if the servo doesn't make a sound at all, + and − are not right. |
| It buzzes or shakes at the ends | That SG90 can't reach 0° or 180° and pushes against its end stop. Narrow the pulse: `MIN_US = 600` and `MAX_US = 2300` in `main.cpp`. Some only accept 1000 to 2000 µs. |
| It turns less than 180° | The opposite of the above: widen the range (`MIN_US = 400`, `MAX_US = 2500`) carefully, without making it buzz. |
| The robot reboots when it moves | A stalled servo can draw 0.6 A and pull the 5 V down. Don't block it with your hand, and better without LEDs at full power or a loud speaker. |

## Ideas to extend

- **More servos:** remove G8 from `SERVO_PINS` and use it with another LEDC channel for a second servo.
- **Make it react:** raise a flag when you touch its head, or, together with demo 06, move it with the TV remote's buttons.
- **By voice:** together with demo 02, a Gemini function `move_arm(degrees)` that sends `angle N`.
