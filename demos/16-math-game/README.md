# Demo 16 · Math game

Stack-chan asks sums, subtractions, times tables and divisions, and the child answers by tapping the screen. Easy questions come with things to count (apples, stars, balls, flowers). Stack-chan nods when the answer is right and tilts its head when it isn't. The game remembers what each child fails, asks more of it, and opens the next level when one is mastered.

## What you need

- **The robot.** Nothing else to play.
- **For the statistics page:** a phone or laptop on the Wi-Fi in `config/wifi_secrets.h` (`./setup.sh --wifi`).

## Run it

```bash
demos/16-math-game/run.sh               # build, flash and show the robot's serial output
demos/16-math-game/run.sh --flash-only  # only flash it
```

On Windows use `run.bat` the same way.

## Play

1. **Home:** tap **+**, **−**, **×**, **÷** or **Mix**. A round is 10 questions.
2. **Each question** has four answers. The wrong ones are the mistakes children really make: for 7 × 8, *54*, *56*, *63* and *15*.
3. **Right:** a happy face, a nod, green LEDs and a short tune. **Wrong:** the right answer lights up green and Stack-chan tilts its head. The same question comes back three questions later, with the buttons in another order. Tap the screen to go on without waiting.
4. **End of the round:** the stars, and the skill to practise.
5. **Progress:** the skills of each operation, with how many of the last 10 answers were right. A star means mastered.

On the serial monitor, every answer is one line, and typing `reset` forgets the statistics.

## The levels

Each question belongs to a **skill**, and the skills of an operation open one after another. A skill is **mastered** when 8 of its last 10 answers are right, and that opens the next one.

| Operation | Skills, in order |
|---|---|
| + | up to 10 (with pictures) · across 10 (8 + 5) · tens, no carrying (23 + 14) · with carrying (38 + 45) |
| − | within 10 (with pictures, the ones taken away crossed out) · across 10 (13 − 5) · tens, no borrowing · with borrowing |
| × | the 2, 10, 5, 3, 4, 6, 7, 8 and 9 times tables (small ones with pictures: groups of things) |
| ÷ | divide by 2, 10, 5, 3, 4, 6, 7, 8, 9: always exact (small ones with pictures) |

**Reinforcement.** The next question is drawn at random among the open skills. A skill whose last answers went wrong comes up up to 4 times more often than one that goes well. A new one comes up more often while it has few answers. Mastered skills still come up now and then, to keep them fresh.

## Statistics

They are saved in the robot's flash (NVS), so they survive switching it off. You can see them on two pages:

- **On the robot:** *Progress* on the home screen.
- **On a web page served by the robot:** the address is at the bottom of the home screen (`http://192.168.x.x`, or `http://stackchan-XXXX.local`). It shows:
  - **To practise:** the open skills under 80 % right lately;
  - every skill, with its answers right and wrong and the average time;
  - the last 30 answers;
  - a button to forget everything.

## How it works

| File | What for |
|---|---|
| `game.cpp` | The skills, the questions and their wrong options, the statistics and the reinforcement. No screen. |
| `art.cpp` | Stack-chan's face, the + − × ÷ signs and the pictures, drawn with shapes: no image files. The big fonts only have ASCII, so the signs are drawn too. |
| `main.cpp` | The screens and the touch; the tunes, the LEDs and the head, played from `loop()` without blocking it. |
| `web.cpp` | The statistics page (Arduino `WebServer`, run from `loop()` like the game). |

The screen is drawn on a full-screen canvas in PSRAM and pushed at once, so it doesn't flicker. The words on the screen are together at the top of `main.cpp`, to translate them in one place.

## Ideas to extend

- **Another operation or skill:** add it to `SKILLS` and to `make()` in `game.cpp`, and change `STATS_VERSION` (the saved statistics no longer match).
- **Several children:** one `Preferences` namespace per player and a screen to choose who plays.
- **Voice:** an offline keyword model (TensorFlow Lite Micro) trained with recordings of "one", "two", "three", "four" in your language. ESP-SR, Espressif's recogniser, only understands English and Chinese, and it isn't in this Arduino version (2.0.17).
- **A timed mode:** as many as you can in a minute, for the tables already mastered.
