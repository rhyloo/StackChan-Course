"""Demo 02's voice with two new functions for Gemini: write_card and read_card.

    "write on the card: my name is Stack-chan"
      Gemini --> write_card(text="my name is Stack-chan")
             --> CMD "nfc write my name is Stack-chan" --> the robot waits for the card and writes it
             <-- NFC 0x28 {"event":"write","ok":true,...}  <-- the robot
      Gemini <-- {"ok": true}  -->  "Done, it's written!"

The function really waits for you to bring the card close (up to WRITE_WAIT_S):
voice.py runs the functions in a separate thread, so the conversation doesn't stop.
"""
import json
import queue
import time

from google.genai import types

import voice
from stackchan.relay import log

T_NFC = 0x28                 # robot -> laptop: JSON with a read or the result of a write
WRITE_WAIT_S = 23            # the robot waits 20 s for a card; a bit more for its answer
READ_WAIT_S = 6
MAX_BYTES = 140              # an NTAG213, the smallest, has 144 bytes

INSTRUCTIONS = """You are Stack-chan, a small, friendly robot that can read and write NFC cards.
Always reply in the same language the person speaks to you.
Use very short sentences (one or two), because you are heard through a small speaker.
When they ask you to write or save something on a card, call write_card with the exact text to save,
without the command ("write on the card") and without quotes. If it is not clear what text they want, ask.
The function waits for the person to bring the card close to the robot's base; the robot's screen already asks for it.
When they ask what a card says, call read_card.
If a function returns an error, explain the reason in a few words."""


class NFCVoice(voice.Voice):
    instructions = INSTRUCTIONS

    def __init__(self, *args, **kwargs):
        self.writes = queue.Queue()    # write results sent by the robot
        self.last_tag = None           # (time, data) of the last card read
        super().__init__(*args, **kwargs)

    def functions(self):
        S = types.Schema
        base = super().functions()
        return {
            "write_card": {
                "decl": types.FunctionDeclaration(
                    name="write_card",
                    description="Writes a text on an NFC card. Waits up to 20 s for someone to bring it close to the robot "
                                "and returns whether it was written correctly. The text is saved as an NDEF text record.",
                    parameters=S(type="OBJECT", required=["text"], properties={"text": S(type="STRING")})),
                "run": self.fn_write,
            },
            "read_card": {
                "decl": types.FunctionDeclaration(
                    name="read_card",
                    description="Reads the NFC card lying on the robot: type, serial number and the text it holds."),
                "run": self.fn_read,
            },
            "set_leds": base["set_leds"],
            "set_volume": base["set_volume"],
            "robot_status": base["robot_status"],
        }

    # ---------------------------------------------------------------- messages from the robot
    def on_nfc(self, payload, addr):
        try:
            ev = json.loads(payload.decode("utf-8"))
        except ValueError:
            return
        if ev.get("event") == "write":
            self.writes.put(ev)
            log("Card WRITTEN: " + repr(ev.get("text")) if ev.get("ok") else f"Could not write: {ev.get('error')}")
        elif ev.get("event") == "tag":
            self.last_tag = (time.monotonic(), ev)
            log(f"Card read: {ev.get('type')} {ev.get('uid')} -> {ev.get('text')!r}")

    # ---------------------------------------------------------------- functions
    def fn_write(self, args):
        text = " ".join(str(args.get("text", "")).split())
        if not text:
            return {"error": "there is no text to write"}
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        trimmed = text.encode()[:MAX_BYTES].decode(errors="ignore")
        while not self.writes.empty():
            self.writes.get_nowait()
        self.robot.cmd("nfc write " + trimmed)
        log(f"Waiting for a card to write {trimmed!r}...")
        try:
            ev = self.writes.get(timeout=WRITE_WAIT_S)
        except queue.Empty:
            self.robot.cmd("nfc cancel")
            return {"error": "the robot did not answer"}
        if not ev.get("ok"):
            return {"ok": False, "error": ev.get("error")}
        res = {"ok": True, "text_written": ev.get("text")}
        if trimmed != text:
            res["note"] = f"the text was long and only {MAX_BYTES} bytes fit: it was trimmed"
        return res

    def fn_read(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        asked = time.monotonic()
        self.robot.cmd("nfc read")
        while time.monotonic() - asked < READ_WAIT_S:
            if self.last_tag and self.last_tag[0] >= asked:
                ev = self.last_tag[1]
                if not ev.get("ok"):
                    return {"error": "there is a card but it could not be read: bring it closer and keep it still"}
                return {"type": ev.get("type"), "uid": ev.get("uid"),
                        "text": ev.get("text") or ("(no text)" if ev.get("ndef") else "(empty or not NDEF formatted)")}
            time.sleep(0.1)
        return {"error": "there is no card on the robot"}


def setup(relay):
    voice.setup(relay, NFCVoice)
    relay.extra_robot_handlers[T_NFC] = voice._voice.on_nfc
