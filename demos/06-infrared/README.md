# Demo 06 · Infrared: the robot as a remote control

Stack-chan's base has an **infrared receiver** (IRM-56384, pin G10) and an **emitter LED** (pin G5), both at 38 kHz: the frequency of almost every TV, air conditioner or stereo remote. In this demo the robot:

1. **Reads remote controls.** Point a remote at the robot and press a button. The screen shows the protocol (NEC, Sony, Samsung, RC5...), the address, the command and the code.
2. **Gives them colors.** Each button paints the screen and the base LEDs a color. The numbers on the Samsung remote have their own and show in big letters (1 red, 2 orange, 3 yellow...); every other button on any remote gets a color made from its code, always the same one.
3. **Repeats them.** The robot keeps the last button. Tap its screen and it sends it again through its LED: now the robot is the remote.

You'll learn how IR remotes encode their buttons and how to receive and send those codes.

## What you need

- **The robot.** No Wi-Fi needed.
- **An infrared remote control**: the TV's, the air conditioner's, a fan's... Bluetooth or radio remotes, like many modern TV remotes, won't work. If you can see the remote's LED blink through your phone's camera when you press a button, it is infrared.

## Run it

Plug the robot in over USB-C and run:

```bash
demos/06-infrared/run.sh               # flash the robot and start the demo
demos/06-infrared/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/06-infrared/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/06-infrared/run.sh --no-flash    # start the demo with what the robot already has
demos/06-infrared/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

This demo has no relay: the default run already opens the robot's serial monitor and saves it in `logs/`, so `--debug` does the same.

## Try it

1. **Point the remote at the robot's base**, from less than 2 or 3 m, and press a button. The robot beeps, the screen and the base LEDs turn that button's color, and the protocol and code show at the bottom. Press the numbers 0 to 9.
2. **Tap the robot's screen** with the base pointing at the device: the robot repeats the last button and the LEDs flash purple. Try the TV's power button.
3. **Serial monitor:** each button also comes out as source code, ready to paste into your program:
   ```
   Protocol  : NEC
   Code      : 0x20DF10EF (32 Bits)
   uint16_t rawData[71] = {9024, 4462,  598, 520, ...};
   uint32_t address = 0x4;
   uint32_t command = 0x8;
   uint64_t data = 0x20DF10EF;
   ```

What you may see:

| On the screen | What it means |
|---|---|
| `NEC`, `SONY`, `SAMSUNG`, `RC5`... | A known protocol: you see the address and the command, and it is sent back as is. |
| *(repeat)* | You are holding the button: the remote sends a short "still pressed" frame. The robot doesn't keep it. |
| *Air conditioner: N bits* | AC remotes send their whole state at once (temperature, mode, fan). The serial monitor decodes it, for example `Power: On, Mode: Cool, Temp: 24C`. |
| `UNKNOWN` (N pulses) | A protocol the library doesn't know. The raw pulses are kept and replayed pulse by pulse, which almost always works too. |

## How it works

```
remote ))) 38 kHz ))) receiver G10 ──> CoreS3: decodes ──> screen and serial monitor
                                          │
TV     ((( 38 kHz ((( emitter G5   <──────┘ on a screen tap, repeats the last code
```

**How a remote works.** The remote's LED blinks 38,000 times per second, in bursts. The length of the bursts and of the gaps encodes ones and zeros. Each maker has its own protocol: in NEC, for example, there are 32 bits with an address (which device) and a command (which button). The receiver filters out everything that isn't 38 kHz, so sunlight or lamps barely bother it.

`firmware/src/main.cpp` uses the [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) library (`crankyoldgit/IRremoteESP8266@2.9.0`), which knows more than 100 protocols.

| Part | What for |
|---|---|
| `IRrecv s_rx(...)`, `s_rx.decode()` | Receiving. `FRAME_GAP_MS` is the silence that ends a frame: 15 ms is enough for TVs, air conditioners need 50. |
| `BUTTONS[]` | Buttons with a name and a color of their own. The Samsung remote's numbers are there; add yours by copying the protocol and code from the serial monitor: `{ NEC, 0x20DF10EF, "ON", 255, 0, 0 },`. |
| `colorFromCode()` | The color of buttons that are not in the table. |
| `learn()` | Keeps the last code: value and bits, the whole state for an air conditioner, or the raw pulses if it is unknown. |
| `sendLearned()` | Sends it. Turns the receiver off while sending so the robot doesn't hear itself. |

## Troubleshooting

| Symptom | What to do |
|---|---|
| Nothing shows up | Get closer, point at the base and not at the CoreS3, and check the remote is infrared. |

## Ideas to extend

- **A remote for the robot:** map buttons on the TV remote to robot actions (in `loop()`, compare `s_res.command`) and move the head with `servoInit()` and `servoSetTarget()` from `StackChanHW`.
- **Voice universal remote:** combine this demo with demo 02, so "turn off the TV" sends the stored code.
- **Store several buttons:** a list of named codes instead of just the last one, and the base's three touch zones to choose which to send.
- **Control the air conditioner:** with `IRac` you build the message from scratch (`ac.next.degrees = 24; ac.sendAc();`) without learning it first.
