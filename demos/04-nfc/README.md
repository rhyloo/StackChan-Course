# Demo 04 · NFC reader

Stack-chan's base has an ST **ST25R3916** NFC chip ([datasheet](../../docs/hardware/datasheets/STMicroelectronics_ST25R3916_NFC_Datasheet.pdf)) on the internal I2C bus (address `0x50`). It is a full reader **and writer**: it reads and writes tags, and can even pretend to be one (emulation). In this demo the robot only **reads**. Hold a card or an NFC sticker near it: the robot beeps, turns the base LEDs green and shows on screen what the tag holds. You'll learn how NFC tags work and how their data is stored.

```
 NFC tag  )))  13.56 MHz  (((  ST25R3916 (base) ──I2C──> CoreS3 ──> screen
                                                                  └─> serial monitor
```

**NFC** (*Near Field Communication*) is radio at 13.56 MHz that only reaches a few centimetres. The reader creates a magnetic field that powers the tag, so the tag needs no battery. Tags store their data as **NDEF**, a standard format with one or more records: a text, a URL, a contact…

## What you need

- **The robot.** No Wi-Fi or laptop needed, except to flash it.
- **NFC-A tags** (ISO 14443A): NTAG213/215/216 stickers or cards, MIFARE Ultralight, Classic or DESFire. Many transport, hotel and access cards are one of these types.
- Optional: a phone with NFC and the **NFC Tools** app, to write your own texts or URLs on the tags.

## Run it

Connect the robot over USB-C and run, from the repo root:

```bash
demos/04-nfc/run.sh               # flash the robot and start the demo
demos/04-nfc/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/04-nfc/run.sh --debug       # like the default, and save logs/robot-*.log
demos/04-nfc/run.sh --no-flash    # start the demo with what the robot already has
demos/04-nfc/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

This demo has no relay: the default builds the firmware, flashes it and leaves the serial monitor open, which is also saved to `logs/`. `--debug` does the same.

## Try it

1. The screen shows *"NFC: bring a tag close"*.
2. **Hold the tag near the base**, 1 or 2 cm away, and keep it still for a moment. The screen shows:
   - the **UID** (the tag's serial number);
   - its **type** and how much **memory** it has;
   - the **NDEF records** one by one: their type (`T` = text, `U` = URL…) and what they hold.
3. If the tag has no NDEF, the screen says so and the **serial monitor** shows a dump of its memory, page by page.
4. To read the same tag again, move it away and bring it back. A tap on the screen goes back to the waiting screen.

### What you may see

| On screen | What it means |
|---|---|
| `T` / `U` records | A tag with NDEF: text or URL, already decoded. |
| *No NDEF data* | An empty tag or one with a custom format (access cards). Look at the serial monitor. |
| *MIFARE Classic: its sectors need a key* | Without the sector key its data can't be read. The dump tries with the default key. |
| *Could not identify it* | The tag moved or was at the edge of the field. Bring it closer and keep it still. |
| *NFC: reader not found* | The ST25R3916 doesn't answer over I2C. Check that the CoreS3 is properly seated on the base. |

## How it works

`firmware/src/main.cpp` uses the official [M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) library (`m5stack/M5Unit-NFC@0.1.2`). It includes the ST25R3916 driver and the NFC-A and NDEF layers.

- `boardInit()` (from `lib/StackChanHW`) powers the base, and `UnitNFC` registers on `M5.In_I2C`, the internal bus.
- Every 200 ms, `NFCLayerA::detect()` looks for tags.
- `identify()` finds out the exact model and `ndefRead()` reads the NDEF message.
- `deactivate()` puts the tag to sleep, so it isn't read again while it is still on the reader.

| File | What for |
|---|---|
| `firmware/src/main.cpp` | Tag detection, NDEF reading and what the screen shows |
| `firmware/platformio.ini` | Board and libraries (M5Unit-NFC) |
| `../../lib/StackChanHW/` | Base power and LEDs |

## Troubleshooting

| Symptom | What to do |
|---|---|
| The screen says *"NFC: reader not found"* | The ST25R3916 doesn't answer over I2C. Check that the CoreS3 is properly seated on the base. |
| *Could not identify it* or *Read error: the tag moved* | Bring the tag closer (1-2 cm) and keep it still. |
| Nothing happens when you hold a tag | It may not be NFC-A. This demo only reads ISO 14443A tags (NTAG, MIFARE). |

## Ideas to extend

- **Write:** `NFCLayerA::ndefWrite()` writes an NDEF message. The library example `examples/UnitUnified/NFCA/NDEF` writes a text and a URL. Careful: turning a MIFARE Ultralight into NDEF can't be undone, and on DESFire cards it erases what was there.
- **Other standards:** the chip also reads NFC-B, NFC-F (FeliCa) and NFC-V (ISO 15693), with the `NFCLayerB`, `NFCLayerF` and `NFCLayerV` classes.
- **Make the robot react:** depending on the tag's text, move the head, change the LEDs (without going over `LED_MAX_LEVEL`) or say something.
