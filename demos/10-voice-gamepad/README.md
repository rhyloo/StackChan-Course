# Demo 10 · Voice with Gemini and a PS4 gamepad on the laptop

[Demo 02](../02-gemini-voice/) (you talk to the robot and Gemini answers and runs functions), but now the head moves with a **PS4 gamepad paired with the laptop over Bluetooth**, with no phone or VR viewer. You'll learn how to read a gamepad from Python and how to make sounds with numpy alone.

```
 PS4 gamepad ))) Bluetooth ))) laptop (relay) ──POSE──> robot: servos
                                │      ^
                      robot mic ┘      └── Gemini Live: voice and functions
```

**This demo has no firmware of its own:** the robot runs **demo 02's firmware**, and everything new happens on the laptop. The relay reads the gamepad with [pygame](https://www.pygame.org/) (SDL2), which works the same on Linux, Windows and macOS, and sends the head position to the robot over the usual UDP link.

## What you need

- **The robot** and **the laptop** on the same 2.4 GHz Wi-Fi.
- **A PS4 gamepad** (DualShock 4), charged.
- **Bluetooth on the laptop.**
- **A Gemini key**, as in demo 02:
  ```bash
  export GEMINI_API_KEY="your-key"        # Windows (PowerShell): $env:GEMINI_API_KEY="your-key"
  ```

## Run it

1. **Pair the gamepad with the laptop.** You only need to do it once.
   - Put the gamepad in pairing mode: hold **SHARE + PS** for about 3 s, until the light bar blinks fast.
   - **Linux:**
     ```bash
     tools/pair_gamepad.sh
     ```
     If it says the Bluetooth service is off: `sudo systemctl start bluetooth`.
   - **Windows / macOS:** Settings → Bluetooth → Add device → "Wireless Controller".

   If the gamepad was paired with a phone or a PS4, it forgets them: a DualShock 4 only remembers one device.
2. **Connect the robot over USB-C and run:**
   ```
   demos/10-voice-gamepad/run.sh               # flash the robot and start the demo
   demos/10-voice-gamepad/run.sh --flash-only  # only build and flash; the firmware stays on the robot
   demos/10-voice-gamepad/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
   demos/10-voice-gamepad/run.sh --no-flash    # start the demo with what the robot already has
   demos/10-voice-gamepad/run.sh --help        # every option of this demo
   ```
   On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

   It flashes demo 02's firmware and starts the relay. Wait for `Gemini: ready` and `Gamepad connected`. If the robot already has demo 02, use `--no-flash`.
3. **Press PS** if the gamepad didn't connect by itself.

## Try it

Move the stick and talk to the robot.

| Gamepad | What it does |
|---|---|
| **Left stick** | Moves the head. The more you tilt it, the faster. |
| **Right stick** | LED color: the direction picks the hue and the tilt, the brightness. |
| **D-pad** ↑ ↓ ← → | Look up, down, left or right. |
| **✕** | Head to the center |
| **○** | **Let's dance!** A song plays on the robot's speaker and the head and LEDs follow the beat. |
| **△** / **□** | Nod ("yes") / shake the head ("no") |
| **R2** | **"Pew!"**: laser shot, red flash and rumble |
| **L2** | Horn, "meep-meep!" |
| **R1** (hold) | Head at double speed |
| **L1** | Hands-free: mutes or unmutes the mic. With `--push-to-talk`: hold it to talk. |
| **Options** | Mutes or unmutes the mic |
| **PS** | Everything to rest: stops the dance, head to the center and LEDs off |

- **When the gamepad rumbles:** when it connects, when the head reaches its limit, with every shot and on every bar of the dance.
- **The stick and the voice don't get in each other's way:** the head is only sent while you move the stick. If you ask by voice "look to the left", the gamepad doesn't undo it.
- **To stop the dance:** stick, ✕, D-pad or PS.

## How it works

| File | What for |
|---|---|
| `relay/gamepad.py` | Reads the gamepad 25 times per second. The stick moves a pan/tilt target (rad/s depending on the tilt), clipped to the limits the robot sends (the pan keeps turning past the back on a robot that turns all the way round), and it is sent with `robot.pose()`. Buttons detect the moment they are pressed (edge), not whether they are held. |
| `relay/sounds.py` | The "pew", the horn and the song, synthesized with numpy. |
| `relay/main.py` | The demo 02 relay with `gamepad.setup()`: the voice (`voice.py`) is the one from there, with other instructions for Gemini. |
| `tools/pair_gamepad.sh` | Looks for a "Wireless Controller" with `bluetoothctl`, pairs it, marks it as trusted and connects it. |

**The sounds are not files.** `relay/sounds.py` makes them with numpy at startup: the "pew" is a tone that falls from 1800 to 250 Hz, and the song is an original tune at 124 BPM, with kick, claps, hi-hat, bass and an arpeggio over Am – F – C – G. They travel to the robot's speaker the same way as Gemini's voice: 16-bit PCM at 24 kHz. While something plays, the robot doesn't listen, because the mic and the speaker share the audio bus.

**Why SDL's "game controller" API.** Every gamepad numbers its buttons and axes its own way, and it also changes with the operating system. SDL ships a database of gamepads and gives them all the same names. That's why this code also works with an Xbox or PS5 gamepad without changing anything.

## Options

The same as [demo 02](../02-gemini-voice/#options): `--voice`, `--model`, `--no-vision` and `--push-to-talk`.

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| *Waiting for the PS4 gamepad…* | Press PS. If the light blinks slowly and goes off, it isn't paired with this laptop: repeat step 1. |
| The head moves the wrong way | Change the sign in `self.pan -= …` or `self.tilt -= …` in `relay/gamepad.py`. |
| The head moves a little by itself | The stick doesn't fully return to the center: raise `DEAD_ZONE` in `gamepad.py` (0.15 → 0.25). |
| The audio cuts out or stutters | The laptop's Bluetooth and Wi-Fi share the 2.4 GHz radio. Bring the gamepad closer or connect the laptop by cable. |

## Ideas to extend

- **Your own song:** change `CHORDS`, `BPM` or the instruments in `sounds.song()`, or the dance steps in `STEPS` in `gamepad.py`.
- **The gamepad's light bar** in the color of the robot's LEDs: on Linux you can write to `/sys/class/leds/`.
- **A button that asks Gemini** "what do you see?" without talking.
