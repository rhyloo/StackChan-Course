# Demo 17 · Arcade

Classic games on Stack-chan's screen, played with a joystick and two buttons wired to the robot's Grove ports: a platform level, **Super Stack-chan**, an endless runner, **Dino Run**, plus **Snake**, **Breakout**, **Tetris** and **Space Invaders**. All the art is original pixel art, drawn for this course. The best score of each one stays in the robot's flash.

## What you need

- **The robot.**
- **A KY-023 joystick** (the one in the 37-sensor kit), and **two push buttons**: bare ones, or KY-004 modules.
- **Four resistors** for the joystick: two of 10 kΩ and two of 20 kΩ (or 2 × 10 kΩ in series).
- Female-female **Dupont wires**, or Grove cables with a free end.

## Wiring

The Grove ports give **5 V**, and the ESP32's pins only stand 3.3 V. The joystick is fed with 5 V, so its outputs go through a **divider**: 10 kΩ from the output to the pin, and 20 kΩ from the pin to GND, which leaves 5 V at 3.3 V. The buttons don't need one: they only join the pin to GND, and the ESP32's internal pull-up holds it high the rest of the time.

| What | To | How |
|---|---|---|
| Joystick `GND` | GND (black) | |
| Joystick `+5V` | 5 V (red) of PORT.B | |
| Joystick `VRx` | **G9**, white wire of PORT.B | 10 kΩ in series, 20 kΩ from G9 to GND |
| Joystick `VRy` | **G8**, yellow wire of PORT.B | 10 kΩ in series, 20 kΩ from G8 to GND |
| Button **A** | **G17** (PORT.C) and GND | direct |
| Button **B** | **G18** (PORT.C) and GND | direct |
| Joystick `SW` (pause, optional) | **G2** (PORT.A) | direct **only if** the module has no resistor to +5 V on SW; if it has one, a divider too |

With a **KY-004** button module, connect only `S` (to the pin) and `−` (to GND), and leave the middle pin (`+`) free. Pressing it joins `S` to `−`.

Do the wiring **with the robot switched off**.

## Run it

```bash
demos/17-arcade/run.sh               # build, flash and show the robot's serial output
demos/17-arcade/run.sh --flash-only  # only flash it
```

On Windows use `run.bat` the same way.

**Keep the joystick still while the robot starts:** it measures the joystick's center then. The serial monitor says whether it found it:

```
[arcade] joystick found (center X 1650 mV, Y 1690 mV)
```

1. **Check the wiring first:** the last item of the menu, **Controls**. The dot follows the joystick, and A, B and SW light up when pressed. Right must give + X and down + Y. If they come out swapped or backwards, because the joystick is held turned, change `SWAP_XY`, `INVERT_X` or `INVERT_Y` at the top of `firmware/src/input.cpp`.
2. **Choose a game:** joystick up and down, then A. You can also tap a game on the screen.
3. **Pause:** joystick click (SW) or a tap on the screen. In the pause, A goes on and B goes back to the menu.

| Game | Controls |
|---|---|
| Super Stack-chan | Joystick (analog): walk. A: jump (hold it: higher). B held: run. The leaf in the **?** blocks gives a raccoon tail: B swings it (knocks enemies over, breaks bricks); run with B until the **P** meter is full and keep tapping A to **fly**; tapping A while falling glides. Stomp on enemies. The goal flag: the higher you touch it, the more points. |
| Dino Run | A or joystick up: jump (hold it: higher). Down: duck under the birds, or fall faster. It speeds up, and every 700 points day turns into night. |
| Snake | Joystick: turn. Eat apples and don't bite yourself; every apple makes you longer and faster. |
| Breakout | Joystick (analog: the further, the faster): the paddle. A: launch the ball. Two-hit bricks from level 2. |
| Tetris | Left/right: move (hold to repeat). Down: faster. Up: drop at once. A: turn right, B: turn left. |
| Space Invaders | Joystick: move. A: fire. The fewer aliens are left, the faster they come down. |

Every 10 s, the serial monitor says how fast it goes: `[arcade] 29.8 fps, 31 ms of work per frame`.

## How it works

| File | What for |
|---|---|
| `main.cpp` | The cabinet: the menu, the controls screen, the pause, game over, the high scores (`Preferences` "arcade") and the 30 fps frame loop. |
| `input.cpp` | The joystick (two ADC channels, center measured at start, dead zone), the buttons and the screen taps; the directions repeat while held, like a keyboard. |
| `games/*.cpp` | One game each. |
| `games/platformer_art.h` | The platform level's pixel art: one character per pixel, `.` transparent, the palette in its first lines. `makeSprite()` (`main.cpp`) turns each drawing into a small sprite at start. |
| `games/platformer.cpp` | The biggest: a 240×24 tile level built in code (`buildLevel()`), tiles and the hero drawn once into small sprites at start, a camera that follows the hero (and goes up when it flies), physics with `dt` (gravity, variable jump, P meter, flight, glide), enemies that wake up as they come near. The hero stretches when it jumps and squashes when it lands (`pushRotateZoom` with two scales), with dust. |

**One frame:** read the controls, let the game move and draw everything on a 320×240 canvas in PSRAM, and push that canvas to the screen at once. Pushing it takes most of the 33 ms of a frame (the screen is on SPI), so the games are simple to draw and use `dt`, the time since the last frame, to move. If a frame takes longer, they still move at the right speed.

**A game is three functions** (`arcade.h`): `start()`, `frame(input, dt)`, which moves, draws and returns `false` when the game is over, and `score()`. A fourth one, `endTitle()`, is optional: the platform level uses it to say "COURSE CLEAR!" instead of "GAME OVER". To add one:

1. Copy a game in `games/` and change its `Game` at the end: name, one-line help, color, the three functions and `nullptr` (or `endTitle`).
2. Add it to `GAMES` in `main.cpp` (and its `extern` in `arcade.h`).
