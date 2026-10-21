# The development environment

`setup.sh` (Linux) or `setup.bat` (Windows) installs all of this. Here you'll find what each piece is and what to do when something fails.

## The pieces

| Piece | What for |
|---|---|
| **PlatformIO** | Builds the firmwares' C++, downloads the compiler and the libraries each `platformio.ini` asks for, flashes over USB and opens the serial monitor. It lives in two places: the VS Code extension (buttons) and `.venv/bin/pio` (command line). Both share `~/.platformio`. |
| **Arduino-ESP32 2.0.17** | The "framework": `setup()`, `loop()`, `WiFi`, `Serial`… on top of ESP-IDF 4.4. `platform = espressif32@7.1.2` in every `platformio.ini` pins it, so we all build the same thing. |
| **M5Unified** | M5Stack's library for the CoreS3's screen, touch, speaker, microphones, battery and IMU. |
| **`lib/`** | Our own code shared by the demos: `StackChanHW` (servos, LEDs, battery, camera) and `StackChanLink` (UDP with the laptop). The `platformio.ini` files include it with `lib_extra_dirs`. |
| **`.venv/`** | Python virtual environment with PlatformIO, OpenCV, NumPy and google-genai, so it doesn't mix with the system Python. The `server.sh` scripts use it on their own; by hand: `.venv/bin/python` (Windows: `.venv\Scripts\python`). |

## VS Code

1. Open `stackchan-course.code-workspace` (menu *File → Open Workspace from File*, or `code stackchan-course.code-workspace`).
2. The first time, PlatformIO takes a few minutes to get ready (bottom bar).
3. Each firmware shows up as a folder of the workspace. In the bottom bar, choose the project (the folder icon) and use:

| Button | Does |
|---|---|
| ✓ | Build |
| → | Build and flash |
| 🔌 | Serial monitor |
| 🗑 | Clean the build (if something odd happens after changing libraries) |

In the serial monitor, the robot accepts `info` (network and video status) and `home` (centre the head).

## AI assistants

Working with an assistant in the terminal helps a lot with a project like this: you can ask it to read `docs/ROBOT.md` before touching the servos. For example, Claude Code:

```bash
curl -fsSL https://claude.ai/install.sh | bash        # Linux
irm https://claude.ai/install.ps1 | iex               # Windows (PowerShell)
```

Then run `claude` inside the course folder. Any other assistant (Gemini CLI, Antigravity, Copilot…) works just as well.

## The USB port

| | Robot (CoreS3) | ESP32 DevKit |
|---|---|---|
| USB chip | The ESP32-S3 itself (native USB) | A CP2102 or CH340 bridge |
| Linux | `/dev/ttyACM0` | `/dev/ttyUSB0` |
| Windows | `COMx` ("USB Serial Device") | `COMx` ("Silicon Labs CP210x" or "USB-SERIAL CH340") |
| Windows driver | Installs itself (with Internet) | [CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) or [CH340](https://www.wch-ic.com/downloads/CH341SER_EXE.html) if the COM port doesn't appear |

If the port doesn't appear:
1. **Cable.** Many USB-C cables only charge. Try another one.
2. **Linux, permissions.** `groups` must include `dialout` (log out after adding yourself). If the port appears and disappears, `sudo apt remove brltty`.
3. **CoreS3, download mode.** The CoreS3's USB is created by the program itself: if the firmware hangs, it disappears. Hold **RST** ~3 s until the LED turns **green** and flash. Then press RST to start.
4. **DevKit.** If it gets stuck on `Connecting...`, hold **BOOT** while it starts flashing.
5. **Windows.** If Device Manager shows "USB JTAG/serial debug unit" with no COM port, the driver is missing: [Espressif's guide](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-usb-driver.html).

## Firewall

The demos use UDP 5600 (robot), 5601 (phone) and 5602 (beacons).
- **Linux:** `setup.sh` opens them if you use `ufw`.
- **Windows:** the first time you start a relay, allow access on *Private networks*. Your Wi-Fi must be marked as a private network.
