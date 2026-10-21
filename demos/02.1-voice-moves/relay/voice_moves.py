"""Demo 02's voice with functions to move the head, show moods and dance.

    "look to the left"           Gemini --> look(pan=60, tilt=0)
    "a bit more"                 Gemini --> turn_head(pan=20, tilt=0)
    "look behind you"            Gemini --> look(pan=180, tilt=0)
    "are you sad?"               Gemini --> set_mood(mood="sad")
    "dance!"                     Gemini --> dance(seconds=8)

Angles are in degrees, with 0 = looking straight ahead: pan + is left, - is right,
and the head turns all the way round (180 = behind); tilt + is up. There is no VR
viewer here, so nothing else moves the head.
"""
import math
import threading
import time

from google.genai import types

import voice
from stackchan.relay import log

TILT_MIN, TILT_MAX = -7.0, 73.0      # degrees, see lib/StackChanHW/src/stackchan_limits.h
LED = 40                             # LED level for the moods (the firmware never goes above LED_MAX_LEVEL)
# mood -> (LED color, head tilt in degrees or None to keep it)
MOODS = {
    "neutral": ((0, LED, 0), None),
    "happy": ((LED, LED, 0), 15.0),
    "sad": ((0, 0, LED), -5.0),
    "surprised": ((LED, LED, LED), 25.0),
    "angry": ((LED, 0, 0), 5.0),
    "sleepy": ((LED // 4, 0, LED // 4), -5.0),
}
BEAT_S = 0.5
# Dance steps, one per bar (4 beats): (pan, tilt) in degrees on each beat.
DANCE = [
    [(20, 10), (-20, 10), (20, 10), (-20, 10)],     # left-right
    [(0, 25), (0, 2), (0, 25), (0, 2)],             # head bob
    [(35, 20), (10, 5), (-10, 20), (-35, 5)],       # sweep
    [(15, 15), (-15, 15), (15, 3), (-15, 3)],       # square
]
DANCE_COLORS = [(LED, 0, 0), (0, LED, 0), (0, 0, LED), (LED, 0, LED), (LED, LED // 2, 0), (0, LED, LED)]

INSTRUCTIONS = """You are Stack-chan, a small, friendly robot with a head that turns and a face on a screen.
Always reply in the same language the person speaks to you.
Use very short sentences (one or two), because you are heard through a small speaker.
You can move your head, show a mood on your face and dance: use the functions for it.
Angles are degrees from looking straight ahead: pan positive turns to YOUR left, negative to your right,
and your head turns all the way round (180 = behind you); tilt positive looks up.
"Left" and "right" are yours unless the person says otherwise. "Look at me" means straight ahead (pan 0, tilt 0).
For "a bit more", "a little to the left" and similar, use turn_head, which is relative to where you look now.
When you feel or are asked to feel something, show it with set_mood. Say something short while you act."""


def _wrap180(deg):
    return (deg + 180.0) % 360.0 - 180.0 if deg != 180.0 else 180.0


class MovesVoice(voice.Voice):
    instructions = INSTRUCTIONS

    def __init__(self, *args, **kwargs):
        self.pan = 0.0                 # where we last sent the head, degrees
        self.tilt = 0.0
        self.mood = "neutral"
        self.dance_stop = threading.Event()
        self.dance_thread = None
        super().__init__(*args, **kwargs)

    def functions(self):
        S = types.Schema
        base = super().functions()
        angles = S(type="OBJECT", required=["pan", "tilt"], properties={
            "pan": S(type="NUMBER", description="degrees, + to your left, - to your right"),
            "tilt": S(type="NUMBER", description="degrees, + up, - down")})
        return {
            "look": {
                "decl": types.FunctionDeclaration(
                    name="look",
                    description="Points the head at an absolute direction. pan from -180 to 180 (0 = straight ahead, "
                                "180 = behind), tilt from -7 to 73 (0 = straight ahead).",
                    parameters=angles),
                "run": self.fn_look,
            },
            "turn_head": {
                "decl": types.FunctionDeclaration(
                    name="turn_head",
                    description="Turns the head from where it looks now, by these degrees (for 'a bit more to the left').",
                    parameters=angles),
                "run": self.fn_turn,
            },
            "center_head": {
                "decl": types.FunctionDeclaration(name="center_head", description="Looks straight ahead (pan 0, tilt 0)."),
                "run": lambda a: self.fn_look({"pan": 0, "tilt": 0}),
            },
            "set_mood": {
                "decl": types.FunctionDeclaration(
                    name="set_mood",
                    description="Shows a mood: face on the screen, color of the LEDs and head posture.",
                    parameters=S(type="OBJECT", required=["mood"], properties={
                        "mood": S(type="STRING", enum=list(MOODS))})),
                "run": self.fn_mood,
            },
            "dance": {
                "decl": types.FunctionDeclaration(
                    name="dance",
                    description="Dances: moves the head to the beat and changes the LED colors. Returns at once; "
                                "the dance goes on for that many seconds.",
                    parameters=S(type="OBJECT", properties={
                        "seconds": S(type="NUMBER", description="from 3 to 30, 8 if not said")})),
                "run": self.fn_dance,
            },
            "stop_dance": {
                "decl": types.FunctionDeclaration(name="stop_dance", description="Stops dancing."),
                "run": self.fn_stop_dance,
            },
            "set_leds": base["set_leds"],
            "set_volume": base["set_volume"],
            "robot_status": base["robot_status"],
        }

    # ---------------------------------------------------------------- head
    def _send(self, pan, tilt):
        self.pan = _wrap180(pan)
        self.tilt = max(TILT_MIN, min(TILT_MAX, tilt))
        self.robot.pose(math.radians(self.pan), math.radians(self.tilt))

    def fn_look(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        self._stop_dancing()
        self._send(float(args.get("pan", 0)), float(args.get("tilt", 0)))
        log(f"Head: pan {self.pan:+.0f}, tilt {self.tilt:+.0f}")
        return {"ok": True, "pan": round(self.pan), "tilt": round(self.tilt)}

    def fn_turn(self, args):
        return self.fn_look({"pan": self.pan + float(args.get("pan", 0)), "tilt": self.tilt + float(args.get("tilt", 0))})

    # ---------------------------------------------------------------- moods
    def fn_mood(self, args):
        mood = args.get("mood", "neutral")
        if mood not in MOODS:
            return {"error": f"there is no mood called {mood}"}
        self._stop_dancing()
        self._show_mood(mood)
        return {"ok": True, "mood": mood}

    def _show_mood(self, mood):
        self.mood = mood
        (r, g, b), tilt = MOODS[mood]
        self.robot.cmd(f"face {mood}")
        self.robot.cmd(f"leds {r} {g} {b}")
        if tilt is not None and self.robot.online:
            self._send(self.pan, tilt)
        log(f"Mood: {mood}")

    # ---------------------------------------------------------------- dance
    def fn_dance(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        seconds = max(3.0, min(30.0, float(args.get("seconds", 8) or 8)))
        self._stop_dancing()
        self.dance_stop.clear()
        self.dance_thread = threading.Thread(target=self._dance, args=(seconds,), daemon=True, name="dance")
        self.dance_thread.start()
        return {"ok": True, "note": f"dancing for {seconds:.0f} s"}

    def fn_stop_dance(self, args):
        was = self.dance_thread is not None and self.dance_thread.is_alive()
        self._stop_dancing()
        return {"ok": True, "was_dancing": was}

    def _stop_dancing(self):
        t = self.dance_thread
        if t is not None and t.is_alive() and t is not threading.current_thread():
            self.dance_stop.set()
            t.join(timeout=1.0)

    def _dance(self, seconds):
        log(f"Dancing for {seconds:.0f} s")
        mood = self.mood
        self.robot.cmd("face happy")
        end = time.monotonic() + seconds
        beat = 0
        while time.monotonic() < end and not self.dance_stop.is_set():
            bar = DANCE[(beat // 4) % len(DANCE)]
            pan, tilt = bar[beat % 4]
            self._send(pan, tilt)
            r, g, b = DANCE_COLORS[beat % len(DANCE_COLORS)]
            self.robot.cmd(f"leds {r} {g} {b}")
            beat += 1
            self.dance_stop.wait(BEAT_S)
        self._send(0, 0)
        self._show_mood(mood)       # back to the face and LEDs it had before
        log("Dance finished")


def setup(relay):
    voice.setup(relay, MovesVoice)
