# Demo 05 · Write NFC cards with your voice

You tell the robot what you want to save, bring an NFC card close to its base and the robot writes it on the card. It joins [demo 02](../02-gemini-voice/) (voice with Gemini Live) and [demo 04](../04-nfc/) (NFC reader). You'll learn how a Gemini function can wait for something real to happen (a card arriving) before it answers.

> "Write on the card: the storage room key is in the blue drawer."
> *The screen turns yellow: "Bring the card close to write".* You bring the card close, it beeps…
> "Done, it's written!"

It also reads: bring a card close and ask "what does the card say?".

```
mic --AUDIO_IN--> relay --> Gemini Live --> write_card("the storage room key...")
                    │                                 │
                    └── CMD "nfc write ..." ──> robot: waits for the card, writes it and reads it back
                    <── NFC 0x28 {"event":"write","ok":true} ──┘
                                  Gemini <-- {"ok": true} --> "Done!" --AUDIO_OUT--> speaker
```

## What you need

- **The robot** and **the laptop** on the same 2.4 GHz Wi-Fi.
- **A Gemini key**, as in demo 02: create it at https://aistudio.google.com/apikey and
  ```bash
  export GEMINI_API_KEY="your-key"        # Windows (PowerShell): $env:GEMINI_API_KEY="your-key"
  ```
- **NTAG213/215/216 cards or stickers**. They are the cheapest and work best; new ones already come in NDEF format.

## Run it

Connect the robot to the laptop over USB and run:

```
demos/05-nfc-voice/run.sh               # flash the robot and start the demo
demos/05-nfc-voice/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/05-nfc-voice/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/05-nfc-voice/run.sh --no-flash    # start the demo with what the robot already has
demos/05-nfc-voice/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

It builds the firmware, flashes the robot and starts the relay. When it says `Gemini: ready`, you can talk.

## Try it

1. Talk to the robot. For example:
   - "Write on the card: hello, I'm Stack-chan". Bring the card close within 20 s and keep it still until it beeps.
   - "What does the card say?", with the card on the robot.
2. Check it with your phone: with NFC on, bringing the card close shows the text. In the NFC Tools app, under *Read*, you can see the text record.

Tapping the robot's screen mutes or unmutes the mic.

## How it works

| File | What for |
|---|---|
| `firmware/src/main.cpp` | The robot: reads every new card and tells the relay; with `nfc write <text>` it waits for a card, writes an NDEF text record (language `es`: the cards are written in Spanish) and reads it back. The audio is demo 02's `audio.cpp`, compiled in as is. |
| `relay/nfc_voice.py` | The Gemini functions `write_card` and `read_card`. It inherits demo 02's voice (`Voice`) and changes its instructions and functions. |
| `relay/main.py` | Starts the demo 02 relay with these functions. |

**The function really waits.** `write_card` returns nothing to Gemini until the robot has written the card (or failed), so what it tells you is the real result. While it waits, the conversation keeps going: `voice.py` runs the functions in a separate thread.

**A card that is already on the robot.** After reading a card, the robot puts it to sleep (HALT) so it doesn't read it in a loop. When you ask it to write or read, it turns the radio field off and on: the card resets and answers again without you having to move it away.

**New protocol messages** (see [PROTOCOL.md](../01-vr-cardboard/PROTOCOL.md)): the commands `nfc write <text>`, `nfc read` and `nfc cancel`, and the type `0x28 NFC` (robot → laptop, JSON).

## Options

| Option | What it does |
|---|---|
| `--voice Kore` | Another voice: Puck (default), Charon, Kore, Fenrir, Aoede… |
| `--model …` | Another Gemini Live model |
| `--push-to-talk` | Push to talk instead of hands-free |

This robot sends no video, so there is no `--no-vision`: Gemini never gets images in this demo.

## Troubleshooting

| Message | What it means |
|---|---|
| *no card arrived* | 20 s went by without a card. Ask it again. |
| *the text does not fit on this card* | An NTAG213 holds about 130 characters; the relay trims to 140 bytes. Use an NTAG215/216 for long texts. |
| *it is a MIFARE Classic* / *it is a DESFire* | They aren't written: the Classic needs keys and the DESFire would have to be formatted. Use an NTAG. |
| *what was written does not match when read back* | The card moved while it was being written. Try again without moving it. |

## Ideas to extend

- **A URL** instead of text: `Record::setURIPayload("example.com", URIProtocol::HTTPS)`. The phone opens it when you bring the card close.
- **Cards that give orders:** a "party mode" card that changes the LEDs when it is read. Look at `on_nfc` in `nfc_voice.py`.
- **A diary:** save every read with the date in a file on the laptop.
