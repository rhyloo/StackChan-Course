"""PS4 gamepad (DualShock 4) paired with the laptop over Bluetooth.

pygame (SDL2) reads it the same way on Linux, Windows and macOS. It uses SDL's
"game controller" API, which gives every gamepad the same button names
(the Xbox ones): A = ✕, B = ○, X = □, Y = △.

    left stick       moves the head; the more you tilt it, the faster
    right stick      LED color depending on where you tilt it
    D-pad            look up, down, left or right
    ✕                head to the center
    ○                let's dance! with a song on the robot's speaker
    △ / □            nod / shake the head
    R2               "pew!": laser shot, red flash and rumble
    L2               horn
    R1 (hold)        head at double speed
    L1               talk to Gemini (hands-free: mutes or unmutes the mic)
    Options          mutes or unmutes the mic
    PS               everything to rest: stops the dance, head to the center, LEDs off

The head is only sent while you touch the stick: that way it doesn't undo the
moves Gemini makes by voice ("look to the left").
"""
import colorsys
import math
import os
import threading
import time

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")                 # no window: we only want the gamepad
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")  # read it even if the terminal has no focus
import pygame  # noqa: E402
from pygame._sdl2 import controller as sdl_controller  # noqa: E402

import sounds  # noqa: E402
import voice  # noqa: E402
from stackchan.relay import log, log_exception  # noqa: E402

PERIOD = 0.04                     # 25 times per second
DEAD_ZONE = 0.15                  # the stick never returns to exactly 0
PAN_SPEED = math.radians(70)      # rad/s with the stick all the way
TILT_SPEED = math.radians(35)     # tilt has little travel: slower
TURBO = 2.0
PHONE_PAUSE = 10.0                # after using the gamepad, the head ignores the phone (Head mode) this long
LED = 40                          # LED brightness (the firmware never goes above LED_MAX_LEVEL)
LOOK = {                          # D-pad: (pan, tilt) in radians
    pygame.CONTROLLER_BUTTON_DPAD_UP: (0.0, 0.42),
    pygame.CONTROLLER_BUTTON_DPAD_DOWN: (0.0, 0.0),
    pygame.CONTROLLER_BUTTON_DPAD_LEFT: (0.9, 0.15),
    pygame.CONTROLLER_BUTTON_DPAD_RIGHT: (-0.9, 0.15),
}
# Dance steps, one per bar (4 beats): (pan, tilt) on each beat.
STEPS = [
    [(0.35, 0.10), (-0.35, 0.10), (0.35, 0.10), (-0.35, 0.10)],   # left-right
    [(0.0, 0.35), (0.0, 0.02), (0.0, 0.35), (0.0, 0.02)],         # head bob
    [(0.6, 0.30), (0.2, 0.05), (-0.2, 0.30), (-0.6, 0.05)],       # sweep
    [(0.25, 0.25), (-0.25, 0.25), (0.25, 0.05), (-0.25, 0.05)],   # square
]
DANCE_COLORS = [(LED, 0, 0), (0, LED, 0), (0, 0, LED), (LED, 0, LED), (LED, LED // 2, 0), (0, LED, LED)]


def _axis(value):
    """-32768..32767 -> -1..1, with the dead zone removed and no jump when leaving it."""
    v = value / 32767.0
    if abs(v) < DEAD_ZONE:
        return 0.0
    return math.copysign((abs(v) - DEAD_ZONE) / (1 - DEAD_ZONE), v)


class Gamepad:
    def __init__(self, relay):
        self.robot = relay.robot
        self.phones = relay.phones
        self.pad = None
        self.before = {}              # button -> pressed on the previous round
        self.pan = self.tilt = 0.0
        self.moving = False
        self.at_limit = False
        self.last_notice = 0.0
        self.dancing = False
        self.stick_leds = None        # last color sent with the right stick
        self.sound_pew = sounds.pew()
        self.sound_horn = sounds.horn()
        self.song = sounds.song()

    def start(self):
        threading.Thread(target=self._loop, daemon=True, name="gamepad").start()

    # ------------------------------------------------------------ loop
    def _loop(self):
        pygame.init()
        sdl_controller.init()
        while True:
            try:
                self._round()
            except Exception:
                log_exception("gamepad")
                time.sleep(1)
            time.sleep(PERIOD)

    def _round(self):
        pygame.event.pump()           # SDL updates the gamepad state here
        if self.pad is not None and not self.pad.attached():
            log("Gamepad disconnected")
            self.pad = None
        if self.pad is None:
            self._find()
            return
        self._buttons()
        self._right_stick()
        self._stick()

    def _find(self):
        for i in range(sdl_controller.get_count()):
            if sdl_controller.is_controller(i):
                self.pad = sdl_controller.Controller(i)
                self.before = {}
                log(f"Gamepad connected: {self.pad.name}. ○ = dance, R2 = pew!, left stick = head.")
                self.pad.rumble(0.4, 0.4, 250)
                return
        if time.monotonic() - self.last_notice > 15:
            self.last_notice = time.monotonic()
            log("Waiting for the PS4 gamepad over Bluetooth (pair it with tools/pair_gamepad.sh)...")

    def _pressed(self, button):
        """True only at the moment of pressing (edge), not while held."""
        now = bool(self.pad.get_button(button))
        before = self.before.get(button, False)
        self.before[button] = now
        return now and not before

    def _trigger(self, axis, name):
        """The L2/R2 triggers are axes from 0 to 32767: "pressed" from halfway."""
        now = self.pad.get_axis(axis) > 16000
        before = self.before.get(name, False)
        self.before[name] = now
        return now and not before

    # ------------------------------------------------------------ buttons
    def _buttons(self):
        b = pygame
        now = bool(self.pad.get_button(b.CONTROLLER_BUTTON_LEFTSHOULDER))       # L1: press and release
        if now != self.before.get("L1", False):
            self.before["L1"] = now
            voice.push_to_talk(now, self.robot, self.phones)
        if self._pressed(b.CONTROLLER_BUTTON_START) and voice._voice:          # Options
            voice._voice.toggle_mute()
        if self._pressed(b.CONTROLLER_BUTTON_A):                               # ✕
            self.dancing = False
            self._go(0.0, 0.0)
            log("Gamepad: head to the center")
        if self._pressed(b.CONTROLLER_BUTTON_GUIDE):                           # PS
            self.dancing = False
            self._go(0.0, 0.0)
            self.robot.cmd("leds 0 0 0")
            log("Gamepad: everything to rest")
        if self._pressed(b.CONTROLLER_BUTTON_B):                               # ○
            if not self.dancing:
                threading.Thread(target=self._dance, daemon=True, name="dance").start()
        if self._pressed(b.CONTROLLER_BUTTON_Y):                               # △
            self._gesture([(0, 0.22), (0, -0.04), (0, 0.22), (0, 0)])
        if self._pressed(b.CONTROLLER_BUTTON_X):                               # □
            self._gesture([(0.35, 0), (-0.35, 0), (0.35, 0), (0, 0)])
        if self._trigger(b.CONTROLLER_AXIS_TRIGGERRIGHT, "R2"):
            self._pew()
        if self._trigger(b.CONTROLLER_AXIS_TRIGGERLEFT, "L2"):
            self._play(self.sound_horn)
            self.pad.rumble(0.6, 0.2, 300)
        for button, (pan, tilt) in LOOK.items():                               # D-pad
            if self._pressed(button):
                self.dancing = False
                self._go(pan, tilt)

    def _pose(self, pan, tilt, seconds=PHONE_PAUSE):
        """Every head command from the gamepad goes through here. If the phone app is in
        Head mode, it resends its pose every 500 ms and would undo the gamepad's: ignore it for a while."""
        self.robot.pause_phone_control(seconds)
        self.robot.pose(pan, tilt)

    def _go(self, pan, tilt):
        self.pan, self.tilt = pan, tilt
        self._pose(pan, tilt)

    def _play(self, pcm):
        """To the robot's speaker, through the same queue as Gemini's voice."""
        if voice._voice:
            voice._voice.out_queue.put(pcm)

    def _pew(self):
        self._play(self.sound_pew)
        self.robot.cmd(f"leds {LED} 0 0")
        self.pad.rumble(1.0, 1.0, 120)

        def off():
            time.sleep(0.25)
            self.robot.cmd("leds 0 0 0" if not self.dancing else "leds 0 0 %d" % LED)
        threading.Thread(target=off, daemon=True).start()

    def _gesture(self, steps):
        """Nod or shake: a few positions around where it is looking now."""
        base_pan, base_tilt = self._current_position()

        def run():
            for dp, dt in steps:
                self._pose(base_pan + dp, max(base_tilt, 0.05) + dt)
                time.sleep(0.35)
        threading.Thread(target=run, daemon=True).start()

    def _dance(self):
        """One step per beat, in sync with the song. Stopped by the stick, ✕, PS or the D-pad."""
        self.dancing = True
        log("Gamepad: let's dance!")
        start = time.monotonic()
        for n, chunk in enumerate(self.song):
            if not self.dancing:
                break
            self._play(chunk)
            # The robot buffers ~0.2 s of audio before playing: the head follows with that delay.
            wait = start + n * sounds.BEAT + 0.25 - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            if not self.dancing:
                break
            bar, pulse = divmod(n, 4)
            pan, tilt = STEPS[bar % len(STEPS)][pulse]
            self._pose(pan, tilt)
            self.robot.cmd("leds %d %d %d" % DANCE_COLORS[n % len(DANCE_COLORS)])
            if pulse == 0 and self.pad:
                self.pad.rumble(0.3, 0.0, 80)       # the gamepad "feels" every bar
            left = start + (n + 1) * sounds.BEAT - time.monotonic()
            if left > 0:
                time.sleep(left)
        self.dancing = False
        self._go(0.0, 0.0)
        self.robot.cmd("leds 0 0 0")

    # ------------------------------------------------------------ sticks
    def _current_position(self):
        t = self.robot.telemetry or {}
        return float(t.get("pan", self.pan)), float(t.get("tilt", self.tilt))

    def _right_stick(self):
        """LED color: the direction picks the hue and the tilt, the brightness."""
        x = _axis(self.pad.get_axis(pygame.CONTROLLER_AXIS_RIGHTX))
        y = _axis(self.pad.get_axis(pygame.CONTROLLER_AXIS_RIGHTY))
        strength = min(1.0, math.hypot(x, y))
        if strength == 0:
            self.stick_leds = None
            return
        hue = (math.atan2(-y, x) / (2 * math.pi)) % 1.0
        color = tuple(round(c * LED * strength) for c in colorsys.hsv_to_rgb(hue, 1, 1))
        if color != self.stick_leds:
            self.stick_leds = color
            self.robot.cmd("leds %d %d %d" % color)

    def _stick(self):
        x = _axis(self.pad.get_axis(pygame.CONTROLLER_AXIS_LEFTX))
        y = _axis(self.pad.get_axis(pygame.CONTROLLER_AXIS_LEFTY))
        if x == 0 and y == 0:
            self.moving = False
            return
        self.dancing = False              # moving the stick stops the dance
        if not self.moving:               # a move starts: begin from where the head really is
            self.moving = True
            self.pan, self.tilt = self._current_position()
        k = TURBO if self.pad.get_button(pygame.CONTROLLER_BUTTON_RIGHTSHOULDER) else 1.0
        self.pan -= x * PAN_SPEED * k * PERIOD     # stick right -> look right (negative pan)
        self.tilt -= y * TILT_SPEED * k * PERIOD   # stick up (negative y) -> look up
        lim = self.robot.limits
        if lim["pan"][1] - lim["pan"][0] >= 2 * math.pi - 0.01:
            # The head turns all the way round: keep turning, past the back too.
            pan = self.pan = math.atan2(math.sin(self.pan), math.cos(self.pan))
        else:
            pan = min(max(self.pan, lim["pan"][0]), lim["pan"][1])
        tilt = min(max(self.tilt, lim["tilt"][0]), lim["tilt"][1])
        at_limit = (pan, tilt) != (self.pan, self.tilt)
        if at_limit and not self.at_limit:
            self.pad.rumble(0.2, 0.6, 120)        # rumbles when the head reaches its limit
        self.at_limit = at_limit
        self.pan, self.tilt = pan, tilt
        self._pose(pan, tilt)


class GamepadVoice(voice.Voice):
    instructions = """You are Stack-chan, a small, friendly robot with a camera in its face.
A person moves your head with a PS4 gamepad and talks to you. With the gamepad they can also make you
dance, fire a toy laser ("pew!") and honk the horn.
Always reply in the same language the person speaks to you.
Use very short sentences (one or two), because you are heard through a small speaker.
When they ask you to move your head, change the LEDs, the volume or tell your status, use the functions.
If they ask what you see, describe the last camera image you received."""


def setup(relay):
    voice.setup(relay, GamepadVoice)
    Gamepad(relay).start()
