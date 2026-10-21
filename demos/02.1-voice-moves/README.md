# Demo 02.1 · Head and moods by voice

You talk to the robot and it moves: it looks where you tell it, shows how it feels on its face and dances. There is no VR viewer here, so only your voice moves the head. You'll learn how Gemini turns "a bit more to the left" into a function call with numbers, and how the relay drives the robot's head, face and LEDs from Python.

> "Look behind you." *The head turns all the way round.*
> "A bit more to the left." *It turns 20° more.*
> "Are you sad?" *Sad face, blue LEDs, head down.* "A little…"
> "Dance!" *Head to the beat, LEDs changing color.*

```
mic --AUDIO_IN--> relay --> Gemini Live --> look(pan=180, tilt=0)
                    │                       set_mood("sad") / dance(8)
                    ├── POSE (pan, tilt) ──────────> robot: servos
                    └── CMD "face sad", "leds 0 0 40" ──> robot: face on the screen, base LEDs
                                Gemini --> "A little…" --AUDIO_OUT--> speaker
```

## What you need

- **The robot** and **the laptop** on the same 2.4 GHz Wi-Fi. No phone.
- **A Gemini key**, as in demo 02: create it at https://aistudio.google.com/apikey and
  ```bash
  export GEMINI_API_KEY="your-key"        # Windows (PowerShell): $env:GEMINI_API_KEY="your-key"
  ```

## Run it

Connect the robot to the laptop over USB and run:

```
demos/02.1-voice-moves/run.sh               # flash the robot and start the demo
demos/02.1-voice-moves/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/02.1-voice-moves/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/02.1-voice-moves/run.sh --no-flash    # start the demo with what the robot already has
demos/02.1-voice-moves/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

When it says `Gemini: ready`, you can talk. Close any other relay first (demo 01, 02…): the robot only listens to the first one that talks to it.

## Try it

- **Head:** "look to the left", "look up", "look behind you", "look at me", "a bit more to the right", "turn around".
- **Moods:** "be happy", "are you sad?", "you look surprised", "get angry", "you must be sleepy". Each mood changes the face, the LEDs and how the head is held.
- **Dance:** "dance!", "dance for 20 seconds", "stop dancing".
- **Other:** "set the LEDs to purple", "speak louder", "how much battery do you have?".

Tapping the robot's screen mutes or unmutes the mic.

**Directions.** Angles are degrees from looking straight ahead (0, 0). Pan: + is the robot's left, − its right, and the head turns all the way round (180 = behind). Tilt: from −7 (a bit down) to 73 (up). "Left" means the robot's left unless you say otherwise.

## How it works

| File | What for |
|---|---|
| `relay/voice_moves.py` | The Gemini functions: `look` (absolute), `turn_head` (relative to where it looks now), `center_head`, `set_mood`, `dance`, `stop_dance`, plus `set_leds`, `set_volume` and `robot_status` from demo 02. The moods and the dance steps are tables at the top: edit them. |
| `relay/main.py` | Starts the demo 02 relay with these functions and without video. |
| `firmware/src/main.cpp` | Demo 02's firmware without the VR part, plus the faces: `face neutral`, `happy`, `sad`, `surprised`, `angry`, `sleepy`. The mouth opens while it listens or speaks. The audio is demo 02's `audio.cpp`, compiled in as is. |

**Why relative turns.** "A bit more to the left" has no absolute angle. The relay remembers where it last sent the head and `turn_head` adds to it, so Gemini doesn't need to know where the head is.

**The dance** runs in its own thread on the laptop: one pose and one LED color per beat (0.5 s), for the seconds Gemini asks for. Any head or mood command stops it. At the end it goes back to the front and to the mood it had.

**The faces** are drawn by the robot; the relay only sends `face <mood>`. Try them without Gemini on the serial monitor: `face happy`.

## Options

| Option | What it does |
|---|---|
| `--voice Kore` | Another voice: Puck (default), Charon, Kore, Fenrir, Aoede… |
| `--model …` | Another Gemini Live model |
| `--push-to-talk` | Push to talk (screen tap or Enter) instead of hands-free |

## Troubleshooting

| Symptom | What to do |
|---|---|
| Gemini answers but the head doesn't move | Another relay is open and the robot is paired with it. Close it and wait 3 s. |
| The face doesn't change | The robot has another demo's firmware: run `run.sh` without `--no-flash`. |
| It turns the other way | "Left" is the robot's left. Say "your left" or "my left" if it's ambiguous. |
| The head stops at the back | Behind the robot there's a ~58° zone the servo can't see: a target there goes to the nearest edge. See `docs/ROBOT.md`. |

## Ideas to extend

- **New moods:** add a row to `MOODS` in `voice_moves.py` and draw its face in `drawEyes()` / `drawMouth()` in `main.cpp`.
- **Gestures:** a `nod` ("yes") and a `shake_head` ("no") function, like the dance but with two or three poses.
- **Follow a face:** turn vision on and ask Gemini where the person is in the image, then call `look`.
