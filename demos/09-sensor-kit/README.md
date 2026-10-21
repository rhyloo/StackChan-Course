# Demo 09 · The 37-module sensor kit

"37 sensor" kits (KY-0xx modules) come with a bit of everything: buttons, tilt, flame, light and temperature sensors, a joystick, LEDs, a relay… This demo plugs them into the robot's **PORT.B** and shows what they measure on its screen. You'll learn how to read digital and analog signals safely, how single-wire protocols like the DHT11's and 1-Wire work, and how to read a rotary encoder with interrupts.

In the menu you pick the module type:

| Mode | Kit modules | What you see |
|---|---|---|
| **Digital** | tilt, vibration, shock, Hall, reed, magnetic switch, touch, flame (D0), obstacle, line tracking, button, sound (D0), mercury | 0 or 1, how many times it has changed and how long ago |
| **Analog** | photoresistor, KY-028 (A0), linear Hall, microphones (A0), heartbeat | the voltage and a live graph |
| **DHT11** | temperature and humidity (KY-015) | °C and % |
| **DS18B20** | KY-001 temperature | °C with two decimals |
| **Encoder** | rotary encoder (KY-040) | a dial that turns with the knob |
| **Joystick** | XY joystick (KY-023) | a dot that moves |
| **Outputs** | one- and two-colour LEDs, laser, relay, active and passive buzzer | two switches and a tune |

The base LEDs follow the reading: they light up on a digital sensor's 1, or grow with an analog sensor's level.

There are no I2C sensors in this kit: they all use digital or analog pins, or single-wire protocols.

## What you need

- **The robot.** No Wi-Fi or laptop needed, except to flash it.
- **The kit** and some female-female **Dupont wires**, or a Grove cable with a free end.
- **10 kΩ and 20 kΩ resistors** for the dividers. A 20 kΩ can be replaced by two 10 kΩ in series.

### The golden rule: 5 V out, 3.3 V in

PORT.B gives **5 V** to power the module, but the ESP32 pins take **3.3 V** at most. A module on 5 V sends back signals of up to 5 V, so **every signal that goes into the robot passes through a voltage divider**:

```
 module S (0-5 V) ──[ 10 kΩ ]──┬──> robot G9 (0-3.3 V)
                               │
                            [ 20 kΩ ]
                               │
                              GND
```

5 V × 20 / (10 + 20) = **3.3 V**. The firmware knows this: it multiplies what it measures by 1.5 and shows you the module's real voltage.

The **outputs** don't need it: there it is the robot that sends 3.3 V to the module.

### Wiring

PORT.B, Grove cable: **black GND**, **red 5 V**, **yellow G9**, **white G8**. In every case the module's `−` (or `GND`) goes to black and `+` (or `VCC`) to red.

| Mode | Module pin `S` / signal | How |
|---|---|---|
| Digital, Analog | `S`, `D0` or `A0` → **G9** | with a divider (10 kΩ in series, 20 kΩ to GND) |
| DHT11 | `S` → **G9**, direct | plus **20 kΩ from G9 to GND**, without the series one (see below) |
| DS18B20 (KY-001) | `S` → **G9**, direct | plus **10 kΩ from G9 to GND** (8.2 kΩ is better if you have one) |
| Encoder | `CLK` → **G9**, `DT` → **G8** | one divider on each; `SW` not connected |
| Joystick | `VRx` → **G9**, `VRy` → **G8** | one divider on each; `SW` not connected |
| Outputs | `S` → **G9** (and another module on **G8**) | direct, no divider |

**Why the DHT11 and the DS18B20 are different.** Both the robot and the sensor talk over their single wire. A series resistor would stop the robot from pulling the line down to 0 V. These modules already have a **pull-up** resistor to 5 V inside; you only add one to GND to make the divider with it. With the DHT11's 10 kΩ pull-up, 20 kΩ to GND leaves the line at 3.3 V. The KY-001 usually has 4.7 kΩ: with 10 kΩ the line sits at 3.4 V and with 8.2 kΩ, at 3.2 V. If your module has another value (the small resistor marked `103` is 10 kΩ; `472`, 4.7 kΩ), adjust the one to GND so the line stays close to 3.3 V.

## Run it

**With the robot switched off**, wire the module and its divider as in the table. Then connect the robot over USB-C:

```
demos/09-sensor-kit/run.sh               # flash the robot and start the demo
demos/09-sensor-kit/run.sh --flash-only  # only build and flash; the firmware stays on the robot
demos/09-sensor-kit/run.sh --debug       # like the default, and save logs/robot-*.log and logs/relay-*.log
demos/09-sensor-kit/run.sh --no-flash    # start the demo with what the robot already has
demos/09-sensor-kit/run.sh --help        # every option of this demo
```

On Windows use `run.bat` with `-FlashOnly`, `-DebugLogs` and `-NoFlash`.

This demo has no relay: the default already shows the robot's serial output (with the readings) and saves it to `logs/robot-*.log`, so `--debug` does the same.

## Try it

1. **Pick the module type** on the screen. "Menu", top right, takes you back to the list.
2. **Test the module:** tilt it, bring a magnet close, cover its light, blow on the microphone…
   - in **Encoder**, a tap on the screen resets the counter to zero;
   - in **Outputs**, each box switches its pin on or off, and the bottom one plays a tune on G9 for the passive buzzer.

The readings also show up in the serial monitor.

## How it works

`firmware/src/main.cpp`, one `update…()` function per mode:

| Part | What for |
|---|---|
| `analogReadMilliVolts()` + `DIVIDER` | The ESP32-S3 ADC (factory calibrated) measures 0-3.1 V on G9 or G8. × 1.5 gives the module's voltage. |
| `readDht11()` | The DHT11 protocol by hand: 20 ms at 0 V to wake it up, then 40 bits where a short pulse is a 0 and a long one a 1. It is read with interrupts off, because the timing is in microseconds. |
| `updateDs18b20()` | 1-Wire bus with the OneWire library: "convert", wait 750 ms and read the temperature with its CRC. |
| `onEncoder()` | An interrupt on both encoder pins and a quadrature table that discards contact bounce. |
| `updateOutputs()` | `digitalWrite` to switch on and off; `ledcWriteTone` for the passive buzzer's notes. |

## Troubleshooting

| Symptom | What to do |
|---|---|
| Digital always 0 or always 1 | Many modules give **0 when they detect** (flame, obstacle, line tracking). The ones with a blue potentiometer adjust the threshold: turn it until the module's LED changes. |
| Analog almost flat | On modules with two outputs (flame, sound, KY-028), use `A0`, not `D0`. |
| DHT11 / DS18B20 "No answer" or "Sensor not answering" | Check `S`, `+` and `−`, and that the resistor goes from G9 to **GND**, not in series. |
| The encoder counts in jumps | That's normal: the contacts "bounce". The firmware filters most bounces; turn it more slowly. |
| The relay clicks but doesn't switch | Some 5 V relays need more than 3.3 V on the `S` input. Try another module or a transistor. |

## Ideas to extend

- **Make the robot react:** look towards the flame, complain when you tilt it, or switch on the relay when it gets dark.
- **Combine it with voice (demo 02):** "what's the temperature?" and Gemini reads the DHT11.
- **Two sensors at once:** PORT.C (G17 and G18) is free too; so is PORT.A, if you don't use the external I2C bus.
