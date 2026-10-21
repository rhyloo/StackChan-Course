"""Conversation with Gemini Live using the robot's microphone and speaker.

    robot (mic, 16 kHz)  --AUDIO_IN 0x26--> this relay --> Gemini Live
    robot (speaker)      <--AUDIO_OUT 0x27-- this relay <-- Gemini's voice
                                             └-> functions: filters, head, LEDs...

Gemini runs nothing itself: it decides to call a function (below, in
functions()) and we are the ones who run it and send back the result. To give
it a new skill, add a function to functions() with its description.

Two modes:
- Hands-free (default): the robot's mic is open whenever the robot isn't
  speaking, and Gemini detects by itself when you start and stop talking.
  Tapping the robot's screen, L1 or Enter mutes / unmutes the mic.
- Push to talk (--push-to-talk): like a walkie-talkie. The turn starts when
  audio arrives from the robot and ends when it stops arriving (300 ms).
"""
import asyncio
import os
import queue
import threading
import time
from datetime import datetime

import cv2
from google import genai
from google.genai import types

import processing
from stackchan.relay import debug, log

T_AUDIO_IN = 0x26
T_AUDIO_OUT = 0x27
OUT_CHUNK = 1180            # PCM bytes per datagram (fits in 1200 with header and seq)
END_OF_TURN_S = 0.3         # push to talk: no audio from the robot for 300 ms = end of turn
FRAME_EVERY_S = 3.0         # hands-free: one image every 3 s (more images = slower answers)
SILENCE_MS = 500            # hands-free: silence that ends your turn
VOICE_HOLD_S = 10.0         # after moving the head by voice, phone control is ignored for this long

INSTRUCTIONS = """You are Stack-chan, a small, friendly robot with a camera in its face.
The person talking to you wears a VR viewer and sees what your camera sees.
Always reply in the same language the person speaks to you.
Use very short sentences (one or two), because you are heard through a small speaker.
When they ask you to change the image, move your head, change the LEDs or save a photo, use the functions.
If they ask what you see, describe the last camera image you received."""

_voice = None   # the active instance, for hooks.py


def push_to_talk(pressed, robot, phone):
    """Gamepad button. Push to talk: pressed = listen, released = end of turn.
    Hands-free: each press mutes or unmutes the mic."""
    if _voice and _voice.hands_free:
        if pressed:
            _voice.toggle_mute()
        return
    robot.cmd("mic on" if pressed else "mic off")
    if pressed:
        phone.hud("Listening...", ms=1500)


class Voice:
    instructions = INSTRUCTIONS   # a demo that subclasses Voice can change them

    def __init__(self, relay, model, voice_name, vision, hands_free):
        self.relay = relay
        self.robot = relay.robot
        self.phone = relay.phones
        self.model = model
        self.vision = vision
        self.hands_free = hands_free
        self.muted = False
        self.mode_sent_uptime = None    # to resend the mode to the robot if it reboots
        self.last_frame = 0.0
        self.client = genai.Client(api_key=os.environ.get("GEMINI_API_KEY"))
        self.loop = asyncio.new_event_loop()
        self.queue = None             # asyncio.Queue of what has to be sent to Gemini
        self.in_turn = False
        self.last_audio = 0.0
        self.out_seq = 0
        self.said = []                # transcript of the current answer
        self.heard = []
        self.photos_dir = os.path.join(os.getcwd(), "photos")
        self.out_queue = queue.Queue()  # audio for the robot, sent by its own thread
        self.t_end = None               # when you stopped talking (to measure the latency)
        self.t_tool = None
        self.t_audio = None
        self.config = types.LiveConnectConfig(
            response_modalities=["AUDIO"],
            system_instruction=self.instructions,
            speech_config=types.SpeechConfig(voice_config=types.VoiceConfig(
                prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=voice_name))),
            tools=[types.Tool(function_declarations=[f["decl"] for f in self.functions().values()])],
            input_audio_transcription=types.AudioTranscriptionConfig(),
            output_audio_transcription=types.AudioTranscriptionConfig(),
            realtime_input_config=types.RealtimeInputConfig(
                automatic_activity_detection=types.AutomaticActivityDetection(
                    disabled=False,
                    start_of_speech_sensitivity=types.StartSensitivity.START_SENSITIVITY_LOW,   # so noise does not trigger it
                    end_of_speech_sensitivity=types.EndSensitivity.END_SENSITIVITY_HIGH,         # answer as soon as you stop
                    prefix_padding_ms=200,
                    silence_duration_ms=SILENCE_MS,
                ) if hands_free else types.AutomaticActivityDetection(disabled=True)),
            # With video a session lasts 2 minutes; compressing the context lets it go on.
            context_window_compression=types.ContextWindowCompressionConfig(sliding_window=types.SlidingWindow()),
        )

    # ------------------------------------------------------------------ functions
    def functions(self):
        S = types.Schema
        return {
            "set_filter": {
                "decl": types.FunctionDeclaration(
                    name="set_filter",
                    description="Changes the filter of the image shown in the VR viewer. 'none' removes it.",
                    parameters=S(type="OBJECT", required=["filter"], properties={
                        "filter": S(type="STRING", enum=list(processing.FILTERS))})),
                "run": self.fn_filter,
            },
            "move_head": {
                "decl": types.FunctionDeclaration(
                    name="move_head",
                    description="Turns the robot's head. pan: degrees to the left (+) or right (-), from -130 to 160. "
                                "tilt: degrees up, from -2 to 27.",
                    parameters=S(type="OBJECT", required=["pan", "tilt"], properties={
                        "pan": S(type="NUMBER"), "tilt": S(type="NUMBER")})),
                "run": self.fn_move,
            },
            "center_head": {
                "decl": types.FunctionDeclaration(name="center_head", description="Makes the head look straight ahead."),
                "run": self.fn_center,
            },
            "set_leds": {
                "decl": types.FunctionDeclaration(
                    name="set_leds", description="Changes the color of the 12 LEDs in the base (RGB from 0 to 255).",
                    parameters=S(type="OBJECT", required=["r", "g", "b"], properties={
                        "r": S(type="INTEGER"), "g": S(type="INTEGER"), "b": S(type="INTEGER")})),
                "run": lambda a: (self.robot.cmd(f"leds {int(a['r'])} {int(a['g'])} {int(a['b'])}"), {"ok": True})[1],
            },
            "show_text": {
                "decl": types.FunctionDeclaration(
                    name="show_text", description="Shows a short text on the robot's screen and in the VR viewer.",
                    parameters=S(type="OBJECT", required=["text"], properties={"text": S(type="STRING")})),
                "run": self.fn_text,
            },
            "set_volume": {
                "decl": types.FunctionDeclaration(
                    name="set_volume", description="Changes the volume of the robot's speaker, from 0 (silent) to 100.",
                    parameters=S(type="OBJECT", required=["level"], properties={"level": S(type="INTEGER")})),
                "run": self.fn_volume,
            },
            "robot_status": {
                "decl": types.FunctionDeclaration(
                    name="robot_status",
                    description="The robot's battery, power draw, Wi-Fi and head position, measured right now."),
                "run": self.fn_status,
            },
            "save_photo": {
                "decl": types.FunctionDeclaration(name="save_photo", description="Saves a photo of what the camera sees now."),
                "run": self.fn_photo,
            },
        }

    def fn_filter(self, args):
        f = args.get("filter", "none")
        if f not in processing.FILTERS:
            return {"error": f"there is no filter called {f}"}
        processing.FILTER = f
        self.phone.hud(f"Filter: {f}", ms=1500)
        return {"ok": True, "filter": f}

    def fn_move(self, args):
        import math
        self.robot.pause_phone_control(VOICE_HOLD_S)
        self.robot.pose(math.radians(float(args.get("pan", 0))), math.radians(float(args.get("tilt", 0))))
        return {"ok": True, "note": f"for {VOICE_HOLD_S:.0f} s; then, if the VR viewer controls the head, it follows the person again"}

    def fn_center(self, args):
        self.robot.pause_phone_control(VOICE_HOLD_S)
        self.robot.pose(0.0, 0.0)
        self.robot.cmd("home")
        return {"ok": True}

    def fn_volume(self, args):
        level = max(0, min(100, int(args.get("level", 50))))
        self.robot.cmd(f"volume {level * 200 // 100}")   # 100 % = 200/255: louder distorts and draws a lot
        return {"ok": True, "level": level}

    def fn_status(self, args):
        t = self.robot.telemetry if self.robot.online else {}
        if not t:
            return {"error": "no data from the robot"}
        ma = t.get("current_ma")
        return {"battery_percent": t.get("batt_pct"), "battery_volts": t.get("batt_v"),
                "current_ma": ma, "charging": ma is not None and ma < 0,
                "wifi_rssi_dbm": t.get("rssi"), "camera_fps": t.get("fps")}

    def fn_text(self, args):
        text = str(args.get("text", ""))[:60]
        self.robot.cmd("text " + text)
        self.phone.hud(text, ms=3000)
        return {"ok": True}

    def fn_photo(self, args):
        img = processing.last_frame
        if img is None:
            return {"error": "no camera image has arrived yet"}
        os.makedirs(self.photos_dir, exist_ok=True)
        path = os.path.join(self.photos_dir, datetime.now().strftime("photo-%Y%m%d-%H%M%S.jpg"))
        cv2.imwrite(path, img)
        log(f"Photo saved: {path}")
        return {"ok": True, "file": os.path.basename(path)}

    # ------------------------------------------------------------------ relay thread
    def on_audio_in(self, payload, addr):
        """Audio arrives from the robot's mic (UDP receive thread)."""
        if len(payload) <= 4 or self.queue is None:
            return
        self.last_audio = time.monotonic()
        if self.hands_free:
            # Gemini decides when each turn starts and ends; we only send the
            # audio and, now and then, what the camera sees.
            if self.vision and self.last_audio - self.last_frame >= FRAME_EVERY_S and processing.last_frame is not None:
                self.last_frame = self.last_audio
                ok, jpg = cv2.imencode(".jpg", processing.last_frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
                if ok:
                    self._put(("frame", jpg.tobytes()))
            self._put(("audio", payload[4:]))
            return
        if not self.in_turn:
            self.in_turn = True
            self.heard = []
            self._put(("start", None))
            # Only one image per turn, at the start: each image stays in the
            # session context and makes every following answer slower.
            if self.vision and processing.last_frame is not None:
                ok, jpg = cv2.imencode(".jpg", processing.last_frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
                if ok:
                    self._put(("frame", jpg.tobytes()))
        self._put(("audio", payload[4:]))

    def toggle_mute(self):
        self.muted = not self.muted
        self.robot.cmd("mic mute" if self.muted else "mic unmute")
        self.phone.hud("Mic muted" if self.muted else "Listening", ms=1500)
        log("Robot mic " + ("MUTED" if self.muted else "on"))

    def _sync_robot_mode(self):
        """Tells the robot which mode we are in, also after it reboots (its uptime goes down)."""
        up = self.robot.uptime
        if not self.robot.online or up is None:
            return
        if self.mode_sent_uptime is None or up < self.mode_sent_uptime:
            self.robot.cmd("listen auto" if self.hands_free else "listen manual")
            if self.muted:
                self.robot.cmd("mic mute")
        self.mode_sent_uptime = up

    def _put(self, item):
        self.loop.call_soon_threadsafe(self.queue.put_nowait, item)

    def start(self):
        threading.Thread(target=self.loop.run_forever, daemon=True).start()
        threading.Thread(target=self._audio_out_loop, daemon=True, name="audio_out").start()
        asyncio.run_coroutine_threadsafe(self._main(), self.loop)

    # ------------------------------------------------------------------ asyncio
    async def _main(self):
        self.queue = asyncio.Queue()
        asyncio.create_task(self._turn_watchdog())
        while True:
            try:
                await self._session()
            except Exception as e:
                log(f"Gemini: session closed ({e.__class__.__name__}: {str(e)[:120]}). Reconnecting in 2 s")
                await asyncio.sleep(2)

    async def _turn_watchdog(self):
        while True:
            await asyncio.sleep(0.05)
            now = time.monotonic()
            self._sync_robot_mode()
            if self.in_turn and now - self.last_audio > END_OF_TURN_S:
                self.in_turn = False
                self.queue.put_nowait(("end", None))

    async def _session(self):
        log(f"Gemini: connecting to {self.model}...")
        async with self.client.aio.live.connect(model=self.model, config=self.config) as session:
            log("Gemini: ready. " + ("Just talk: it listens whenever the robot isn't speaking. "
                                     "Tapping the robot's screen, L1 or Enter mutes the mic." if self.hands_free else
                                     "Talk with L1 on the gamepad, by tapping the robot's screen or with Enter here."))
            receiver = asyncio.create_task(self._receive(session))
            try:
                while not receiver.done():
                    try:
                        kind, data = await asyncio.wait_for(self.queue.get(), timeout=1.0)
                    except asyncio.TimeoutError:
                        continue
                    if kind == "start":
                        await session.send_realtime_input(activity_start=types.ActivityStart())
                    elif kind == "audio":
                        await session.send_realtime_input(audio=types.Blob(data=data, mime_type="audio/pcm;rate=16000"))
                    elif kind == "frame":
                        await session.send_realtime_input(video=types.Blob(data=data, mime_type="image/jpeg"))
                    elif kind == "end":
                        await session.send_realtime_input(activity_end=types.ActivityEnd())
                        self.t_end, self.t_tool, self.t_audio = time.monotonic(), None, None
                receiver.result()   # raises the error if receiving failed
            finally:
                receiver.cancel()

    async def _receive(self, session):
        while True:
            async for msg in session.receive():
                if msg.data:
                    if self.t_end and self.t_audio is None:
                        self.t_audio = time.monotonic()
                    self.out_queue.put(msg.data)
                if msg.tool_call:
                    if self.t_end and self.t_tool is None:
                        self.t_tool = time.monotonic()
                    await self._run_tools(session, msg.tool_call.function_calls)
                sc = msg.server_content
                if sc is None:
                    continue
                if sc.input_transcription and sc.input_transcription.text:
                    self.heard.append(sc.input_transcription.text)
                    if self.hands_free:
                        # Without a button we don't know when you stopped: the closest
                        # thing is the last transcribed word (it arrives a bit late, so
                        # the real latency is somewhat higher).
                        self.t_end, self.t_tool, self.t_audio = time.monotonic(), None, None
                if sc.output_transcription and sc.output_transcription.text:
                    self.said.append(sc.output_transcription.text)
                if sc.turn_complete or sc.interrupted:
                    if self.t_end:
                        ms = lambda t: f"{(t - self.t_end) * 1000:.0f} ms" if t else "-"  # noqa: E731
                        log(f"Latency since {'your last word' if self.hands_free else 'you stopped talking'}: function {ms(self.t_tool)}, first audio {ms(self.t_audio)}"
                            + ("  (interrupted)" if sc.interrupted else ""))
                        self.t_end = None
                    if self.heard:
                        log("You: " + "".join(self.heard).strip())
                        self.heard = []
                    if self.said:
                        text = "".join(self.said).strip()
                        log("Stack-chan: " + text)
                        self.phone.hud(text[:80], ms=min(5000, 1500 + 60 * len(text)))
                        self.said = []

    async def _run_tools(self, session, calls):
        responses = []
        for fc in calls:
            f = self.functions().get(fc.name)
            try:
                # In a separate thread: a function that waits (e.g. for you to bring a
                # card close) must not stop receiving from Gemini.
                result = await asyncio.get_running_loop().run_in_executor(None, f["run"], dict(fc.args or {})) \
                    if f else {"error": "unknown function"}
            except Exception as e:
                result = {"error": str(e)}
            log(f"Gemini -> {fc.name}({dict(fc.args or {})}) = {result}")
            debug(f"tool_call id={fc.id}")
            responses.append(types.FunctionResponse(id=fc.id, name=fc.name, response=result))
        await session.send_tool_response(function_responses=responses)

    def _audio_out_loop(self):
        """Gemini's voice (PCM 24 kHz) to the robot's speaker, in chunks that fit in a
        datagram. It runs in its own thread so it doesn't slow down receiving from Gemini."""
        while True:
            pcm = self.out_queue.get()
            if not self.robot.online:
                continue
            for i in range(0, len(pcm), OUT_CHUNK):
                self.out_seq = (self.out_seq + 1) & 0xFFFFFFFF
                self.robot.send(T_AUDIO_OUT, self.out_seq.to_bytes(4, "big") + pcm[i:i + OUT_CHUNK])
                time.sleep(0.002)   # don't flood the ESP32 receive queue


def setup(relay, voice_class=Voice):
    """Hooks the voice into the relay (main.py calls it before starting). Another
    demo can pass its own subclass of Voice, with other functions."""
    global _voice
    if not os.environ.get("GEMINI_API_KEY"):
        raise SystemExit("Missing Gemini key: export GEMINI_API_KEY=... (get one at https://aistudio.google.com/apikey)")
    _voice = voice_class(relay, relay.args.model, relay.args.voice, not relay.args.no_vision, not relay.args.push_to_talk)
    relay.extra_robot_handlers[T_AUDIO_IN] = _voice.on_audio_in
    _voice.start()

    def keyboard():
        talking = False
        while True:
            try:
                input()
            except EOFError:
                return
            if _voice.hands_free:
                _voice.toggle_mute()
                continue
            talking = not talking
            relay.robot.cmd("mic on" if talking else "mic off")
            log("Robot mic " + ("OPEN: talk and press Enter when you finish" if talking else "closed"))
    threading.Thread(target=keyboard, daemon=True).start()
