# Demo 07 · Photo booth with a thermal printer

The robot becomes a photo booth: you tap its screen, it counts down 3 or 5 seconds, takes the photo, you choose a filter and it prints it on an **MX10 thermal printer** (a "cat printer" with 57 mm thermal paper) over Bluetooth. You'll learn how to send a photo to the laptop, filter it with Gemini or OpenCV, dither it to black and white and drive a Bluetooth LE printer from the ESP32-S3.

| Filter | Who makes it | What you get |
|---|---|---|
| **Ghibli** | Gemini (AI) | The photo redrawn as a still from a Ghibli animated film. |
| **Gaming** | Gemini (AI) | The photo turned into a retro 16-bit video game, with a health bar and a score. |
| **Comic** | Gemini (AI) | A black-and-white comic book panel, with ink and halftones. |
| **Picasso** | Gemini (AI) | A Cubist portrait in the style of Picasso: the face broken into geometric planes. |
| **Original** | Laptop | The photo as is. |
| **Sketch** | Laptop (OpenCV) | A pencil drawing. |
| **Pixel** | Laptop | Pixel art 64 dots wide. |

## What you need

- **The robot** and **the laptop** on the same 2.4 GHz Wi-Fi.
- **An MX10 printer** with paper and battery. Other "cat printers" (GB01, GB02, GT01, MX05...) use the same protocol and should work; the MXW01 doesn't.
- **For the AI filters, a Gemini key**: create it at https://aistudio.google.com/apikey and
  ```bash
  export GEMINI_API_KEY="your-key"        # Windows (PowerShell): $env:GEMINI_API_KEY="your-key"
  ```
  Without a key, Original, Sketch and Pixel still work.

## Run it

Turn the printer on and put it near the robot. Plug the robot in over USB-C and run:

```bash
demos/07-photo-booth/run.sh               # flash the robot and start the demo
demos/07-photo-booth/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/07-photo-booth/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/07-photo-booth/run.sh --no-flash    # start the demo with what the robot already has
demos/07-photo-booth/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

It builds the firmware, flashes the robot and starts the relay. On the robot's screen you'll see the camera and "Laptop ready".

## Try it

1. **Tap the screen:** the left half for a **3 s** countdown, the right half for **5 s**. It beeps every second and, at zero, the screen and the LEDs flash.
2. **Choose a filter.** The AI ones take about 10 s. You can tap **New photo** if you don't like it.
3. **Look at the result** and tap:
   - **Print:** looks for the printer over Bluetooth (about 5 s the first time), prints and disconnects;
   - **New filter**, on the same photo;
   - **New photo**.

Every photo is saved on the laptop, in `photos/`: the original (`.jpg`), the filtered one (`-ghibli.png`...) and the print version (`-thermal.png`).

## How it works

```
 1. preview ──tap──> 2. countdown 3/5 s ──> 3. photo!
                                                │ JPEG over Wi-Fi
                                                v
 5. "Print" ──Bluetooth LE──> MX10      laptop: 4. filter (Gemini or OpenCV)
        ^                                       + black and white
        └──── image ready to print (384 dots) ──┘
```

**Why black and white.** Thermal paper darkens with heat: the printer can only put a black dot or not. Greys are faked with more or fewer dots (Floyd-Steinberg "dithering"). The robot's screen shows the image already dithered: **what you see is exactly what comes out on the paper**.

| File | What for |
|---|---|
| `firmware/src/main.cpp` | The screens (preview, countdown, menu, result), the photo (JPEG over UDP) and receiving the image. The filter buttons are in `FILTERS`. |
| `firmware/src/printer_mx10.cpp` | The printer: Bluetooth LE (NimBLE), service `AE30`, writes to `AE01`. Each command goes in a `51 78 <command> 00 <length> <data> <crc8> FF` frame; each line is 48 bytes (384 dots). |
| `relay/filters.py` | The filters: Gemini prompts in `PROMPTS`, OpenCV for the local ones, and `to_thermal()`, which scales to 384 dots wide and dithers. |
| `relay/photo_booth.py` | Receives the photo, calls the filter and sends the image back to the robot, repeating if a chunk gets lost. |

**The robot drives the printer.** The ESP32-S3 has Bluetooth LE, so it prints without needing the laptop's Bluetooth. The MX10 doesn't say when it can take more data during a print: the robot sends 200 bytes every 20 ms, the rate it prints at. When it finishes it waits 3 s before disconnecting, because the printer is still burning paper with what it has in memory.

The MX10 protocol and its tricks (blank paper before and after the image, don't use the feed command, a new connection for each print) come from [s8n/cataclysm](https://github.com/s8n/cataclysm) (MIT license), which tested them on a real MX10.

**New protocol messages** ([PROTOCOL.md](../01-vr-cardboard/PROTOCOL.md)): `0x29 PHOTO`, `0x2A PHOTO_EVENT`, `0x2B PRINT_BITMAP` and the commands `photo ack|status|error`.

## Options

| Flag | What it does |
|---|---|
| `--image-model MODEL` | Gemini model for the AI filters (default `gemini-3.1-flash-image`). |

## Troubleshooting

| Message | What to do |
|---|---|
| *Looking for the laptop (run.sh)...* | The relay is not running or you are not on the same Wi-Fi. |
| *Printer not found. Is it on and nearby?* | Turn it on and bring it closer. It must advertise as `MX...` over Bluetooth LE. If your phone is connected to it, disconnect it: it only takes one connection. |
| *Lost the printer connection. Is the battery low?* | It is almost always a low battery: with little charge the MX10 turns off halfway through a print. Charge it. |
| *Gemini returned no image* | Gemini refused or failed. Try another filter or another photo. |
| Print too light or too dark | Change `ENERGY` in `firmware/src/printer_mx10.cpp` (higher = darker) or the `0.8` in `_contrast()` in `relay/filters.py`. |

## Ideas to extend

- **A new filter:** add its prompt to `PROMPTS` in `filters.py` and its button to `FILTERS` in `main.cpp`.
- **A frame or text** ("Stack-chan · 2026") at the bottom of the print, drawn with OpenCV before dithering.
- **Photo booth strip:** four photos in a row, one under the other, on the same paper.
- **By voice:** combine this demo with demo 02, so "take a Ghibli-style photo of me" does it all.
