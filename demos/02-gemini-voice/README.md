# Demo 02 · Voice with Gemini

Demo 01 plus voice. You talk to the robot and [Gemini Live](https://ai.google.dev/gemini-api/docs/live) understands you, answers through the robot's speaker and runs functions: change the image filter, move the head, set the LEDs, save photos… When you start talking it sees what the camera sees, so you can ask "what do you see?". You'll learn how a live voice model calls your own Python functions, and how audio travels over the same UDP link as the video.

```
robot mic --AUDIO_IN 16 kHz--> relay --> Gemini Live --> voice --AUDIO_OUT 24 kHz--> robot speaker
                                 ^                   └-> function call: set_filter("night")
robot camera --JPEG--------------┘ (one frame at the start of each turn)
```

**Why Gemini runs on the laptop and not on the robot**, as the Mika project did:
- **The robot is already busy** compressing and sending video. Opening the encrypted connection to Gemini as well would take CPU and Wi-Fi away from it.
- **Audio is light**: about 32 KB/s travel over the same UDP link as the video.
- **The Gemini key stays on the laptop**, not stored on the robot.
- **Writing the functions in Python is much easier** than in C++.

## What you need

- Everything from demo 01. The phone and the VR viewer are **optional**: without them it works as a robot you talk to.
- **A Gemini API key**: create one for free at https://aistudio.google.com/apikey and set it in your terminal:
  ```bash
  export GEMINI_API_KEY="your-key"        # Windows (PowerShell): $env:GEMINI_API_KEY="your-key"
  ```
  To avoid typing it every time, add it to `~/.bashrc`. **Never commit it to git.**

## Run it

Connect the robot over USB-C and run:

```
demos/02-gemini-voice/run.sh               # flash the robot and start the demo
demos/02-gemini-voice/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/02-gemini-voice/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/02-gemini-voice/run.sh --no-flash    # start the demo with what the robot already has
demos/02-gemini-voice/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

It builds the firmware, flashes the robot and starts the relay. Wait until you see `Gemini: ready`. `server.sh` (or `server.bat`) only starts the relay, like `--no-flash`.

## Try it

**Just talk**, as you would to a person. The robot listens whenever it isn't speaking (a green circle for a mouth), and Gemini detects by itself when you have finished (half a second of silence).

- **To make it stop listening**, tap its screen, press L1 on the gamepad or Enter in the terminal. Same again to turn it back on.
- **While the robot speaks it can't hear you.** The microphone and the speaker share the CoreS3's audio bus, so you can't interrupt it.
- **Walkie-talkie mode:** `run.sh --push-to-talk`. You talk only while you hold L1, or between two screen taps or two presses of Enter. It works better in noisy places.

Try:
- "Turn on night vision"
- "Use the cartoon filter"
- "Remove the filter"
- "What do you see?"
- "Look to the left"
- "Set the LEDs to blue"
- "Take a photo"

Photos are saved in `photos/`.

## How it works

- **Turns.** In hands-free mode the robot sends its microphone whenever it isn't speaking, and Gemini's voice detection decides when you start and stop. It has low sensitivity at the start, so noise doesn't trigger it, and it ends the turn after 500 ms of silence (`SILENCE_MS` in `voice.py`). With `--push-to-talk`, the relay marks the turn: it starts when audio arrives and ends after 300 ms without audio (`activity_start` / `activity_end`).
- **Images.** In hands-free mode Gemini gets one image every 3 s; with `--push-to-talk`, one at the start of each turn. Each image stays in the session context and slows down the following answers: measured, about 100-200 ms with one per second.
- **API usage.** In hands-free mode the audio goes to Gemini all the time (silences included), so it uses more quota than push to talk.
- **Microphone or speaker, not both.** On the CoreS3 they share the same I2S bus. With `--push-to-talk`, if you talk while the robot answers, the robot goes quiet and listens to you.
- **Function calls.** Gemini runs nothing by itself: it asks "call `set_filter` with `filter=night`". The relay runs it and sends back the result, and Gemini uses it to answer ("Night vision on").
- **Sessions.** With video, a Gemini Live session is short. The context is compressed to make it last longer, and if it drops the relay reconnects by itself.

Where to program:

| File | What for |
|---|---|
| `relay/voice.py` | **The functions Gemini can use** (`functions()`) and its personality (`INSTRUCTIONS`). To give it a new skill, add an entry with its description: Gemini decides by itself when to use it. |
| `relay/processing.py` | The filters. Add a function to `FILTERS` and Gemini can turn it on by its name. |
| `relay/hooks.py` | Which gamepad button is used to talk |
| `firmware/src/audio.cpp` | The robot's microphone and speaker |

## Options

All the options of demo 01, plus:

| Option | What it does |
|---|---|
| `--voice Kore` | Another voice: Puck (default), Charon, Kore, Fenrir, Aoede… |
| `--model …` | Another Gemini Live model |
| `--no-vision` | Don't send images to Gemini |
| `--push-to-talk` | Push to talk instead of hands-free |

## Troubleshooting

| Symptom | What to do |
|---|---|
| `Missing Gemini key` | `export GEMINI_API_KEY=...` in the same terminal |
| It answers noises or background conversations | Mute the mic (tap, L1 or Enter) or use `--push-to-talk` |
| The mouth is listening (green circle) but Gemini doesn't answer | Look at the terminal: if `You: …` doesn't show up, the audio isn't arriving. Check that the robot has **this** demo's firmware. |
| You can't hear the answer | The volume is in `audio.cpp` (`setVolume`). The answer only plays while the microphone is closed. |
| The head goes back a few seconds after "look to the left" | Expected if the VR viewer controls the head (*Head* mode): the voice is in charge for 10 s (`VOICE_HOLD_S` in `voice.py`) and then the head follows you again. To control it by voice only, put the app in *View only* with □. |
