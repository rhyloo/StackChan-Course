# Demo 14 · The Nezha Pro robotic arm, from a web page

The robotic arm from Elecfreaks' [**Nezha Pro AI Mechanical Power Kit** (case 10)](https://wiki.elecfreaks.com/en/microbit/building-blocks/nezha-pro-ai-mechanical-power-kit/nezha-pro-ai-mechanical-power-kit-case-10/) has a **micro:bit V2** on the **Nezha Pro** expansion board and three Elecfreaks smart motors. In this demo you move its **three motors from a web page**, on the laptop or on a phone: hold a button to turn a joint, go to an angle, open and close the gripper, and save poses and play them back. You'll learn how the Nezha board drives its motors over I2C, how to make a motor stop by itself when it gets stuck, how to give a microcontroller a text protocol over the serial port and how to build a MakeCode program without the browser. It is the first step towards Stack-chan telling the arm what to grab.

```
 browser ──HTTP──> laptop/arm_web.py ──USB serial, 115200──> micro:bit V2 ──I2C 0x10──> Nezha Pro ──> M1 M2 M3
 (laptop or phone)  web/index.html       "run 1 50", "goto 2 90"   microbit/main.ts               (M3: the gripper)
                    <── angles, states ──   "pos 0 90 -30"            buttons A / B
```

## What you need

- **The arm from case 10**, assembled, with the gripper motor on **M3** (as in Elecfreaks' wiring) and the Nezha Pro's battery charged. The gesture sensor can stay on the IIC port: this demo doesn't use it.
- **A USB data cable** for the micro:bit.
- **Node.js 18 or newer** on the laptop, only to build the program (`node --version`). The first time, `run.sh` installs the MakeCode compiler in `.makecode/` (about 600 MB).

## Run it

1. **Switch the Nezha Pro board on** (its button): the motors take their power from the board's battery, not from the USB.
2. **Put the arm in its starting position.** The angles count from where each motor is when the micro:bit starts.
3. **Connect the micro:bit over USB.** A drive called `MICROBIT` appears.
4. **Run:**
   ```bash
   demos/14-robot-arm/run.sh          # Windows: demos\14-robot-arm\run.bat
   ```
   It builds `microbit/main.ts`, copies the `.hex` to the `MICROBIT` drive (the yellow LED blinks while it saves it) and opens the web page at **http://localhost:8014**. The terminal also prints the address to open it from a phone on the same Wi-Fi.

`--flash-only` (`-FlashOnly`) only builds and copies it; `--no-flash` (`-NoFlash`) opens the page without flashing. `--http-port`, `--no-browser` and `--port` go to `arm_web.py`. `run.sh --help` lists every option.

**Without Node.js:** open [makecode.microbit.org](https://makecode.microbit.org), create a project, add the `nezha2` extension (search for `nezha pro`) and paste `microbit/main.ts` in the JavaScript view. Download it to the micro:bit and then run `run.sh --no-flash`.

## Try it

**On the web page**, one card per motor:

| Control | What it does |
|---|---|
| **◀ ▶** (hold) | Turns the motor while you hold it. It stops when you let go. |
| **−90° −10° +10° +90°** | Turns exactly that much |
| **Go** | Goes to the angle in the box |
| **Zero** | The current position becomes 0 (nothing moves) |
| **Open / Close (grip)** | The gripper (M3), until it grips something or reaches the end |
| **STOP** (or the space bar) | Stops everything |
| **Speed** | For the hold buttons, the steps and *Go* |
| **Poses** | *Save current* stores the three angles; *Go* returns to them, *Play all in order* goes through them one after another |

Try a pick-and-place: open the gripper, move the arm over the foam cube and save the pose "above". Lower it, close the gripper and save "grab". Lift it, move it somewhere else and save "drop". Open the gripper there and save "release". Then press *Play all in order*.

**On the micro:bit:** A (hold) closes the gripper, B opens it, A + B stops everything.

- **A motor stops by itself** when it gets stuck. The gripper has gripped something, or a joint reached its end (`stuck: stopped` on its card). That way it never keeps pushing against the gears.
- **If the page closes or the Wi-Fi drops** while you hold a button, the motor stops on its own after 0.6 s: the page repeats the order every 0.2 s and each one lasts 0.6 s.
- **The angles are of the motor shaft,** not of the joint: with gears in between, a joint turns less than its motor. They can go past 360°.

## Serial commands

The web page sends the same text you can type in its *Serial* box, or in `laptop/console.py`. `<m>` is 1, 2 or 3:

| Command | What it does |
|---|---|
| `run <m> <speed> [ms]` | Turns at a speed from -100 to 100 for ms (default 500). Send it again to keep going. |
| `move <m> <degrees> [speed]` | Turns that many degrees (negative: the other way) |
| `goto <m> <angle> [speed]` | Goes to that angle |
| `stop [m]` | Stops one motor, or all of them |
| `zero [m]` | The current position becomes 0 |
| `speed <1-100>` | Default speed |
| `pos` | Sends the angles now |
| `close [ms]` / `open [ms]` | The gripper, until it grips or reaches the end (at most 3 s) |

The micro:bit answers `pos <a1> <a2> <a3>` whenever an angle changes (and every 2 s), `moving <m> run|move`, `stopped <m> stall|timeout|arrived|command|button` and `error ...`.

## How it works

**The Nezha Pro board** is an I2C device at address `0x10`. Every order is 8 bytes, `FF F9 <motor> <direction> <command> <value high> <mode> <value low>`. For example, command `0x60` turns at a speed, `0x70` turns a number of degrees, `0x5F` stops and `0x46` reads the angle. The [pxt-nezha2](https://github.com/elecfreaks/pxt-nezha2) extension, the one the Elecfreaks cases use, wraps them: `nezhaV2.start`, `nezhaV2.stop`, `nezhaV2.readAngle`...

**The smart motor has an encoder:** the board knows its angle at every moment. Every 200 ms `main.ts` reads the three angles. With them it sends `pos`, knows when a `goto` has arrived and detects a stuck motor: one that turned less than 2° while it should be moving. A plain motor would keep pushing against the gears until something gives.

**`goto` is a relative move:** `main.ts` subtracts the current angle from the target and asks the board for that many degrees (command `0x70`). That way it works past 360°, which the board's own absolute mode (`0x5D`, 0-359°) can't do.

**Only the main loop talks to the board.** The serial port handler only queues the commands and `basic.forever` runs them. The I2C bus has a single owner, as on Stack-chan (*I2C only from the `loop()` task*).

**The web page** asks the server for the state (`GET /state`) five times per second and sends commands with `POST /cmd`. The server uses only Python's standard library (`http.server`), so it needs nothing beyond `pyserial`.

**Building without the browser:** `tools/microbit.py` runs the same compiler as makecode.microbit.org (`pxt`, the `pxt-microbit` package from npm). `pxt install` downloads the extensions in `pxt.json` from GitHub, and `pxt build` leaves `built/binary.hex`, which works on the micro:bit V1 and V2. Flashing a micro:bit is copying that file to its drive.

| File | What for |
|---|---|
| `microbit/main.ts` | The micro:bit program: serial commands, buttons, positions and stopping when stuck. `GRIPPER`, `CLOSE`, `STALL_DEG`, `START_MS` at the top. |
| `microbit/pxt.json` | The MakeCode project: dependencies (`pxt-nezha2` v1.2.1) and files. |
| `laptop/arm_web.py` | Web server: serves the page, passes its commands to the serial port and keeps the angles, states and last lines. |
| `laptop/web/index.html` | The page: motor cards, speed, poses (saved in the browser) and the serial log. |
| `laptop/console.py` | The same from the terminal: what you type goes to the serial port. |
| `laptop/microbit_link.py` | The serial port: finds the micro:bit, reconnects if it restarts and saves everything to `logs/microbit-*.log`. |
| `../../tools/microbit.py` | `build` and `flash` for any MakeCode project. |

## Troubleshooting

| Symptom | What to do |
|---|---|
| A motor stops straight away (`stuck: stopped`) | Is the Nezha Pro board switched on? The motors don't move on USB power alone. If it does move but stops too soon, use more speed or raise `START_MS` in `main.ts`. |
| A joint stops halfway with a load | `STALL_DEG` is too high for that speed: lower it, or use more speed. |
| `goto` goes the other way and never gets there | This motor counts the angle the other way round: swap `CW` and `CCW` in `goTo()` in `main.ts`. |
| `close` opens the gripper | Change `CLOSE` to `-1` in `main.ts`. |
| The poses don't land where they did | The angles count from where the arm was when the micro:bit started. Start it in the same position, or move it there and press *Zero all*. |
| The page says `micro:bit not connected` | USB data cable? Is another program using the port (`console.py`, a MakeCode tab connected with WebUSB)? Close it. |
| `I can't find the micro:bit` when flashing | The `MICROBIT` drive must be mounted: open it once in the file manager. |
| `Address already in use` | Another `arm_web.py` is open, or use `--http-port 8015`. |

## Ideas to extend

- **Stack-chan gives the orders:** "grab the cube" by voice with Gemini (demo 02), and the relay sends `goto` and `close` over the serial port (or to `POST /cmd`).
- **Sequences that wait for the gripper:** in *Play all*, close the gripper only when the arm has arrived, and go on only when it has gripped (`stopped 3 stall`).
- **The gesture sensor** from the kit, as in Elecfreaks' case: a hand moving up or down moves the arm.
