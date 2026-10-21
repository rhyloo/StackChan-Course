# Inside Stack-chan

Reference guide to the robot for someone who has never seen it. Every fact cites its source in `docs/hardware/`. Anything we could not check is marked **unverified**.

Stack-chan is an **M5Stack CoreS3** (the "brain", with screen and camera) mounted on M5Stack's **StackChan K151 base**, which adds two servos to move the head, a battery, LEDs, infrared, NFC and a touch panel.

| | |
|---|---|
| Size | 54.0 × 70.5 × 61.5 mm |
| Weight | 187.2 g |
| Official manual | [M5Stack_StackChan_K151_User_Manual.pdf](hardware/manuals/M5Stack_StackChan_K151_User_Manual.pdf) |
| Drawings | [StackChan_Mechanical_Model_Size.pdf](hardware/manuals/StackChan_Mechanical_Model_Size.pdf) |

```
            ┌────────────── CoreS3 (head) ────────────────┐
            │ ESP32-S3 · 2" touch screen · GC0308 camera   │
            │ 9-axis IMU · 2 mics · speaker · light/prox.  │
            └──────────────┬───────────────────────────────┘
                           │ M5-Bus (30 pins): I2C, servo UART, IR, battery, 5 V
            ┌──────────────┴──────── K151 base ────────────┐
            │ PY32L020 (I2C 0x6F): servo power, 12 LEDs     │
            │ pan servo (ID 1) · tilt servo (ID 2) · 550 mAh │
            │ IR · NFC · 3-zone touch · INA226 (battery)     │
            └───────────────────────────────────────────────┘
```

## 1. CoreS3

Source: [M5Stack_CoreS3_User_Manual.pdf](hardware/manuals/M5Stack_CoreS3_User_Manual.pdf) and [schematic](hardware/schematics/M5Stack_CoreS3_Schematic_v1.0.pdf).

| Component | Detail |
|---|---|
| Processor | ESP32-S3: dual-core Xtensa LX7 at 240 MHz, 512 KB of SRAM ([datasheet](hardware/datasheets/ESP32-S3_Datasheet_Espressif.pdf)) |
| Memory | 16 MB of flash and 8 MB of PSRAM (Quad SPI) |
| Radio | 2.4 GHz Wi-Fi b/g/n (**it can't see 5 GHz networks**) and Bluetooth LE 5. No Bluetooth Classic: that's why the PS4 gamepad is paired with the phone. |
| Screen | 2.0" IPS, 320×240, ILI9342C controller over SPI. FT6336U capacitive touch. |
| Camera | GC0308, 0.3 MP (VGA 640×480 at most). **No hardware JPEG**: it outputs RGB565/YUV and the processor does the JPEG in software ([datasheet](hardware/datasheets/GalaxyCore_GC0308_Camera_Datasheet.pdf)). We use 320×240 at about 12 fps. |
| Motion | BMI270 IMU (accelerometer and gyroscope) + BMM150 magnetometer. The robot's magnets disturb the magnetometer. |
| Other sensors | LTR-553 light and proximity, above the camera |
| Audio | 2 microphones (ES7210 codec) and a 1 W speaker (AW88298 amplifier) |
| Clock | BM8563 RTC |
| Storage | microSD slot, up to 16 GB |
| Power | AXP2101 PMIC (rails, charging and battery level); AW9523B I/O expander (resets and enables) |
| USB | **Native** USB-C of the ESP32-S3 (VID:PID `303A:1001`). There is no USB-serial chip, so no CP210x or CH34x drivers are needed. |
| Size | 54 × 54 × 15.5 mm |

### Buttons

They are on the sides of the CoreS3.

| Button | Action |
|---|---|
| **POWER** (left, next to the USB-C) | Short press: switch on. 6 s: switch off. |
| **RST** (bottom, next to the microSD) | Short press: restart. 3 s: download mode (see section 6). |

There are no user buttons. Our firmware uses the touch screen: a tap centres the head.

### Internal I2C bus (SDA G12, SCL G11)

Shared by the CoreS3 and the base:

| Address | Chip | | Address | Chip |
|---|---|---|---|---|
| 0x10 | BMM150 magnetometer (auxiliary bus) | | 0x41 | INA226 battery monitor (base) |
| 0x21 | GC0308 camera (SCCB) | | 0x50 | ST25R3916 NFC (base) |
| 0x23 | LTR-553 light/proximity | | 0x51 | BM8563 RTC |
| 0x34 | AXP2101 PMIC | | 0x58 | AW9523B expander |
| 0x36 | AW88298 amplifier | | 0x68 | Si12T 3-zone touch (base) |
| 0x38 | FT6336U touch | | 0x69 | BMI270 IMU |
| 0x40 | ES7210 microphones | | 0x6F | PY32L020 coprocessor (base) |

### Useful pins

| Use | Pins |
|---|---|
| Servos (UART1, 1 Mbps) | TX G6, RX G7 |
| Infrared | Emitter G5, receiver G10 (38 kHz) |
| Camera | D0–D7 = G39, G40, G41, G42, G15, G16, G48, G47; PCLK G45; VSYNC G46; HREF G38. XCLK comes from a crystal on the flex. RESET is on AW9523 P1_0. |
| Screen (SPI) | MOSI G37, SCK G36, CS G3, DC G35 |
| microSD | Same SPI, MISO G35, CS G4 |
| Audio (I2S) | MCLK G0, BCK G34, WS G33, DOUT G13, DIN G14 |
| Grove Port A (I2C) | SDA G2, SCL G1 |
| Grove Port B | G9 (white wire), G8 (yellow), checked on the robot 2026-10-22 |
| Grove Port C (UART) | TX G17, RX G18 |

Grove connector: black GND, red 5 V, yellow and white signals. The full ESP32-S3 pin map is in [ESP32-S3_Pinout_Reference.pdf](hardware/pinouts/ESP32-S3_Pinout_Reference.pdf).

## 2. Feetech SCS0009 servos

One moves the horizontal turn (**pan**, ID 1) and the other the nod (**tilt**, ID 2). Source: [Feetech_SCS0009_Servo_Datasheet.pdf](hardware/datasheets/Feetech_SCS0009_Servo_Datasheet.pdf).

| | at 4.8 V | at 6 V |
|---|---|---|
| **Stall torque** | 1.89 kg·cm (0.185 N·m) | 2.3 kg·cm (0.226 N·m) |
| Rated torque | 0.65 kg·cm (0.064 N·m) | 0.75 kg·cm (0.074 N·m) |
| No-load speed | 0.125 s/60° (80 rpm) | 0.10 s/60° (100 rpm) |
| No-load / rated / stall current | 120 / 300 / 800 mA | 150 / 400 / 1000 mA |

On the base the servos run at **about 5 V**, made by a TPS61088 boost converter from the battery. Their real values will be between the two columns (**unverified**).

**Features**
- Supply: 4–7.4 V.
- Copper and steel gears, 1:416 reduction, backlash ≤ 0.5°.
- Weight: 13.2 g.
- It measures position with a 330° **potentiometer**, not an encoder.
- Protections:
  - overload: above 80 % of stall torque for 2 s, it cuts the torque;
  - voltage outside the 4–9 V range;
  - temperature above 70 °C.
- Range in position mode: 0–1023 steps (about 300°).

**Protocol.** Half-duplex TTL serial over a single wire, 8N1, 1 Mbps. Packet:

```
FF FF  ID  LEN  INSTR  PARAM...  CHK        LEN = number of parameters + 2
                                            CHK = ~(ID + LEN + INSTR + Σ PARAM) & 0xFF
```

Instructions:

| Code | Instruction |
|---|---|
| `0x02` | Read |
| `0x03` | Write |

Most used registers (2-byte values go **high byte first**):

| Register | Use |
|---|---|
| `0x05` | ID |
| `0x06` | Baud rate |
| `0x28` | Torque enable (1/0) |
| `0x2A` | Target position (2 bytes) |
| `0x2C` | Time to get there, in ms |
| `0x2E` | Speed |
| `0x38` | Current position (read) |

Implementation: `lib/StackChanHW/src/board.cpp` (`scsSend`, `scsWritePos`).

**Scale.** The datasheet says 3.41 steps/°, but the official Stack-chan firmware and ours use **3.2 steps/°** (183.35 steps/rad). Which one is exact is **unverified**; the difference is about 6 %.

## 3. K151 base

Source: K151 manual and schematics in [hardware/schematics/](hardware/schematics/).

- **550 mAh battery.** It goes to the BAT pin of the M5-Bus, so the CoreS3's AXP2101 charges it when you plug in the USB-C (inferred from the schematic). Charge current and battery life: **unverified**.
- **INA226** (0x41) measures the battery current with a 10 mΩ shunt.
- **Servo power.** A TPS61088 boost converter makes 5 V ("VM") from the battery. It only switches on when the PY32 enables `VM_EN`.
- **12 RGB LEDs** WS2812C in two rows, driven by the PY32.
- **Infrared**: emitter and IRM-56384 receiver (38 kHz).
- **NFC** ST25R3916.
- **3-zone touch panel** on top (Si12T).
- LEGO-compatible holes.

### PY32L020 coprocessor (I2C 0x6F)

A Cortex-M0+ microcontroller that works as an I/O expander. What we use (`lib/StackChanHW/src/board.cpp`):

| Register | Use |
|---|---|
| `0x02` | Version. If it is 0 or 0xFF, the base hasn't booted yet. |
| `0x03`/`0x04` | Pin direction (1 = output) |
| `0x05`/`0x06` | Output. **Bit 0 of `0x05` = VM_EN** (switches the servos on). |
| `0x09`–`0x0C` | Pull-up / pull-down |
| `0x13`/`0x14` | Output type: 1 = open drain, 0 = push-pull. The LED pin 13 is push-pull. |
| `0x24` | Bits 0–5: number of LEDs (12). Bit 6: refresh. |
| `0x30`… | Colour of each LED: 2 bytes RGB565, low byte first |

## 4. Movement limits and calibration

They are in `lib/StackChanHW/src/stackchan_limits.h`. The app receives them from the robot and never asks for values outside them.

| Axis | Angle | Radians | Allowed steps | Centre |
|---|---|---|---|---|
| Pan (ID 1) | any angle (360°); position arc −137.5° … +169.7° | −π … +π (arc −2.3998 … +2.9616) | 20 … 1003 | 460 |
| Tilt (ID 2) | −6.9° … +73.1° (0 = head level) | −0.1200 … +1.2763 | 598 … 854 | 620 |

**Why these limits**
- **Pan, 360°.** The base turns with no mechanical stop (K151 manual), but in position mode the servo only knows where it is over raw 20…1003 (its own angle limits), about 302°. Measured by hand with demo 01's `probe` command (2026-10-21): past the ends the sensor sticks near 0 or 1022, or gives garbage near 0, for the remaining ~58° (the *blind zone*, behind the robot). The robot accepts any pan angle and takes the short way round: when that crosses the blind zone, `panCross()` in `board.cpp` switches the servo to motor mode (angle limits 0/0, speed in register `0x2C`, bit 10 = reverse), turns until the sensor reads a trustworthy value on the far side (about 1.2–1.6 s) and goes back to position mode with the goal where it stopped. A target inside the blind zone goes to the nearest end of the arc. The robot announces pan limits of ±π, and the app then treats pan as a circle (it smooths it the short way round).
- **Tilt.** Measured the same way: chin on the base at raw 582 (−12°), head level at 620, fully up at 871 (+78°), the 90° of the manual. M5 recommends keeping 5° away from each end (the servo can jam there), so the limits are 598…854. Pushing against an end for 2 s trips the servo's overload protection, which cuts its torque; the firmware switches it back on with the next move, at most every 2 s.
- **Limited speed.** The servo loop runs at 25 Hz and moves at most about 94°/s in pan and 47°/s in tilt. Sudden jumps cause current spikes that reset the base (*brownout*).
- **Centres.** 460 and 620 are the same ones the official Stack-chan firmware uses.

## 5. Software

| | |
|---|---|
| Environment | PlatformIO, platform `espressif32@7.1.2` (`demos/*/firmware/platformio.ini`) |
| Framework | Arduino-ESP32 2.0.17 (on ESP-IDF 4.4.7) |
| Library | M5Unified 0.2.21: screen, touch, speaker, PMIC and IMU |
| Compiler | xtensa-esp32s3 GCC 8.4.0 |
| Upload speed | 921 600 baud (1.5 M fails on some Linux machines) |

**Alternatives**
- **Arduino IDE:** add the board URL `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`, install "M5Stack" and choose *M5CoreS3*.
- **ESP-IDF** and **UiFlow2:** they work too.

### USB port on each system

| System | Port | What you need |
|---|---|---|
| Linux | `/dev/ttyACM0` | Be in the `dialout` group and have PlatformIO's udev rules (`setup.sh` does both). If it doesn't show up, uninstall `brltty`. |
| Windows 10/11 | `COMx` | The driver installs itself if there is Internet. If Device Manager shows "USB JTAG/serial debug unit" twice with no COM port, follow [Espressif's guide](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-usb-driver.html). |

The USB port is created by the program itself. If the firmware hangs or goes into deep sleep, the port disappears: use download mode.

## 6. Recovery

**Download mode** (when the computer can't see the robot or the firmware is hung):
1. Connect the USB-C. M5 recommends the one on the base, so the motors don't move on their own.
2. Hold **RST** for about 3 s, until the LED next to it turns **green**, and let go.
3. Flash as usual (`./tools/flash.sh <firmware>` or PlatformIO's → button).
4. Press RST once to start the new program.

**Back to M5's factory firmware:**
1. Install [M5Burner](https://docs.m5stack.com/en/download).
2. Search for "StackChan" with the *Only Official* filter.
3. Click *Burn*.

## 7. Unverified data

- Power use of the whole robot and battery life with the 550 mAh battery.
- Charge current of the base battery.
- Real torque and speed of the servos at 5 V.
- Real servo scale: 3.2 or 3.41 steps/°.
- The manual calls the pan "360° continuous rotation". In position mode, the one we use, the range is 0–1023.
- How TX (G6) and RX (G7) are joined on the servos' single signal wire.
