"""The PS4 gamepad drives the tank while the phone (VR viewer) moves the head.

The same controls as laptop/drive.py, inside the demo 02 relay:

    left stick       drive: forward/back and turn (the more you tilt it, the faster)
    D-pad            forward, back and spin on the spot, at a fixed speed
    R1 (hold)        full speed
    ✕                stop
    L2               horn, on the Stack-chan's speaker
    R2               blaster shot (Star Wars style), on the Stack-chan's speaker
    L1               talk to Gemini (hands-free: mutes or unmutes the mic)
    Options          mutes or unmutes the mic

The sounds go out through the same queue as Gemini's voice: only one program
can talk to the robot (it pairs with the first relay), so drive.py's
RobotSpeaker can't be used here.
"""
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "laptop"))

from drive import NORMAL, PERIOD, TRIGGER, tracks  # noqa: E402  (first: it sets SDL up before pygame loads)
import pygame  # noqa: E402
from pygame._sdl2 import controller as sdl_gamepad  # noqa: E402

import sounds  # noqa: E402  (demo 10's sound effects: drive.py puts them on the path)
import voice  # noqa: E402
from stackchan.relay import log, log_exception  # noqa: E402
from tank import Tank  # noqa: E402

_tank = None   # for main.py, to stop it on exit


class TankPad:
    def __init__(self, tank):
        self.tank = tank
        self.pad = None
        self.before = {}              # button or trigger -> pressed on the previous round
        self.last_notice = 0.0
        self.horn, self.blaster = sounds.horn(), sounds.blaster()

    def start(self):
        threading.Thread(target=self._loop, daemon=True, name="tank_pad").start()

    def _loop(self):
        pygame.init()
        sdl_gamepad.init()
        while True:
            try:
                self._round()
            except Exception:
                log_exception("tank gamepad")
                self.tank.stop()
                time.sleep(1)
            time.sleep(PERIOD)

    def _round(self):
        pygame.event.pump()
        if self.pad is not None and not self.pad.attached():
            log("Gamepad disconnected: tank stopped")
            self.pad = None
            self.tank.stop()
        if self.pad is None:
            self._find()
            return
        if self.pad.get_button(pygame.CONTROLLER_BUTTON_A):              # ✕
            left = right = 0.0
        else:
            left, right = tracks(self.pad)
        k = 255 * (1.0 if self.pad.get_button(pygame.CONTROLLER_BUTTON_RIGHTSHOULDER) else NORMAL)
        self.tank.move(left * k, right * k)
        if self._edge("L2", self.pad.get_axis(pygame.CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER):
            self._play(self.horn)
        if self._edge("R2", self.pad.get_axis(pygame.CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER):
            self._play(self.blaster)
            self.pad.rumble(1.0, 1.0, 120)
        now = bool(self.pad.get_button(pygame.CONTROLLER_BUTTON_LEFTSHOULDER))   # L1: press and release
        if now != self.before.get("L1", False):
            self.before["L1"] = now
            voice.push_to_talk(now, voice._voice.robot, voice._voice.phone)
        if self._edge("Options", self.pad.get_button(pygame.CONTROLLER_BUTTON_START)):
            voice._voice.toggle_mute()

    def _find(self):
        for i in range(sdl_gamepad.get_count()):
            if sdl_gamepad.is_controller(i):
                self.pad = sdl_gamepad.Controller(i)
                self.before = {}
                log(f"Gamepad connected: {self.pad.name}. Left stick = tank, L2 horn, R2 blaster, L1 talk.")
                self.pad.rumble(0.4, 0.4, 200)
                return
        if time.monotonic() - self.last_notice > 15:
            self.last_notice = time.monotonic()
            log("Waiting for the PS4 gamepad over Bluetooth (pair it with tools/pair_gamepad.sh)...")

    def _edge(self, name, now):
        """True only at the moment of pressing, not while held."""
        before = self.before.get(name, False)
        self.before[name] = now
        return now and not before

    def _play(self, pcm):
        voice._voice.out_queue.put(pcm)


class TankVoice(voice.Voice):
    instructions = """You are Stack-chan, a small, friendly robot with a camera in its face, riding on top of a tank.
The person talking to you wears a VR viewer: they see what your camera sees and move your head with theirs.
They drive the tank with a PS4 gamepad, and with it they can also honk the horn and fire a toy blaster.
Always reply in the same language the person speaks to you.
Use very short sentences (one or two), because you are heard through a small speaker.
You can't move your head or the tank yourself. When they ask you to change the image, change the LEDs
or save a photo, use the functions.
If they ask what you see, describe the last camera image you received."""


def setup(relay):
    global _tank
    voice.setup(relay, TankVoice)
    _tank = Tank(relay.args.tank, log=log)
    TankPad(_tank).start()
