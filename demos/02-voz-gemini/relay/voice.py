"""Conversación con Gemini Live usando el micrófono y el altavoz del robot.

    robot (micro, 16 kHz) --AUDIO_IN 0x26--> esta pasarela --> Gemini Live
    robot (altavoz)      <--AUDIO_OUT 0x27-- esta pasarela <-- voz de Gemini
                                             └-> funciones: filtros, cabeza, LEDs...

Gemini no ejecuta nada: decide llamar a una función (abajo, en FUNCIONES) y
somos nosotros quienes la ejecutamos y le devolvemos el resultado. Para darle
una habilidad nueva, añade una función a FUNCIONES con su descripción.

Turnos tipo walkie-talkie: el turno empieza cuando llega audio del robot y
termina cuando deja de llegar (300 ms), da igual si fue el botón L1 del mando,
Enter en esta terminal o un toque en la pantalla del robot.
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
OUT_CHUNK = 1180            # bytes de PCM por datagrama (cabe en 1200 con cabecera y seq)
END_OF_TURN_S = 0.3         # sin audio del robot durante 300 ms = has terminado de hablar

INSTRUCCIONES = """Eres Stack-chan, un robot pequeño y simpático con una cámara en la cara.
La persona que te habla lleva unas gafas de realidad virtual y ve lo que ve tu cámara.
Habla en español de España, con frases muy cortas (una o dos), porque te escuchan por un altavoz pequeño.
Cuando te pidan cambiar la imagen, mover la cabeza, los LEDs o guardar una foto, usa las funciones.
Si te preguntan qué ves, describe la última imagen de la cámara que te ha llegado."""

_voice = None   # la instancia activa, para hooks.py


def push_to_talk(pressed, robot, phone):
    """Botón del mando: pulsado = el robot escucha; soltado = fin del turno."""
    robot.cmd("mic on" if pressed else "mic off")
    if pressed:
        phone.hud("Escuchando...", ms=1500)


class Voice:
    def __init__(self, relay, model, voice_name, vision):
        self.relay = relay
        self.robot = relay.robot
        self.phone = relay.phones
        self.model = model
        self.vision = vision
        self.client = genai.Client(api_key=os.environ.get("GEMINI_API_KEY"))
        self.loop = asyncio.new_event_loop()
        self.queue = None             # asyncio.Queue de lo que hay que enviar a Gemini
        self.in_turn = False
        self.last_audio = 0.0
        self.out_seq = 0
        self.said = []                # transcripción de la respuesta en curso
        self.heard = []
        self.photos_dir = os.path.join(os.getcwd(), "fotos")
        self.out_queue = queue.Queue()  # audio para el robot, lo envía su propio hilo
        self.t_end = None               # cuándo terminaste de hablar (para medir la latencia)
        self.t_tool = None
        self.t_audio = None
        self.config = types.LiveConnectConfig(
            response_modalities=["AUDIO"],
            system_instruction=INSTRUCCIONES,
            speech_config=types.SpeechConfig(voice_config=types.VoiceConfig(
                prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=voice_name))),
            tools=[types.Tool(function_declarations=[f["decl"] for f in self.functions().values()])],
            input_audio_transcription=types.AudioTranscriptionConfig(),
            output_audio_transcription=types.AudioTranscriptionConfig(),
            realtime_input_config=types.RealtimeInputConfig(
                automatic_activity_detection=types.AutomaticActivityDetection(disabled=True)),
            # Con vídeo, una sesión dura 2 minutos; comprimiendo el contexto puede seguir.
            context_window_compression=types.ContextWindowCompressionConfig(sliding_window=types.SlidingWindow()),
        )

    # ------------------------------------------------------------------ funciones
    def functions(self):
        S = types.Schema
        return {
            "activar_filtro": {
                "decl": types.FunctionDeclaration(
                    name="activar_filtro",
                    description="Cambia el filtro de la imagen que se ve en las gafas. 'ninguno' lo quita.",
                    parameters=S(type="OBJECT", required=["filtro"], properties={
                        "filtro": S(type="STRING", enum=list(processing.FILTROS))})),
                "run": self.fn_filtro,
            },
            "mover_cabeza": {
                "decl": types.FunctionDeclaration(
                    name="mover_cabeza",
                    description="Gira la cabeza del robot. pan: grados a la izquierda (+) o derecha (-), de -130 a 160. "
                                "tilt: grados hacia arriba, de -2 a 27.",
                    parameters=S(type="OBJECT", required=["pan", "tilt"], properties={
                        "pan": S(type="NUMBER"), "tilt": S(type="NUMBER")})),
                "run": self.fn_mover,
            },
            "centrar_cabeza": {
                "decl": types.FunctionDeclaration(name="centrar_cabeza", description="Pone la cabeza mirando al frente."),
                "run": lambda args: (self.robot.cmd("home"), {"ok": True})[1],
            },
            "color_leds": {
                "decl": types.FunctionDeclaration(
                    name="color_leds", description="Cambia el color de los 12 LEDs de la base (RGB de 0 a 255).",
                    parameters=S(type="OBJECT", required=["r", "g", "b"], properties={
                        "r": S(type="INTEGER"), "g": S(type="INTEGER"), "b": S(type="INTEGER")})),
                "run": lambda a: (self.robot.cmd(f"leds {int(a['r'])} {int(a['g'])} {int(a['b'])}"), {"ok": True})[1],
            },
            "mostrar_texto": {
                "decl": types.FunctionDeclaration(
                    name="mostrar_texto", description="Muestra un texto corto en la pantalla del robot y en las gafas.",
                    parameters=S(type="OBJECT", required=["texto"], properties={"texto": S(type="STRING")})),
                "run": self.fn_texto,
            },
            "volumen": {
                "decl": types.FunctionDeclaration(
                    name="volumen", description="Cambia el volumen del altavoz del robot, de 0 (silencio) a 100.",
                    parameters=S(type="OBJECT", required=["nivel"], properties={"nivel": S(type="INTEGER")})),
                "run": self.fn_volumen,
            },
            "estado_robot": {
                "decl": types.FunctionDeclaration(
                    name="estado_robot",
                    description="Batería, consumo, Wi-Fi y posición de la cabeza del robot, medidos ahora mismo."),
                "run": self.fn_estado,
            },
            "guardar_foto": {
                "decl": types.FunctionDeclaration(name="guardar_foto", description="Guarda una foto de lo que ve la cámara ahora."),
                "run": self.fn_foto,
            },
        }

    def fn_filtro(self, args):
        f = args.get("filtro", "ninguno")
        if f not in processing.FILTROS:
            return {"error": f"no existe el filtro {f}"}
        processing.FILTRO = f
        self.phone.hud(f"Filtro: {f}", ms=1500)
        return {"ok": True, "filtro": f}

    def fn_mover(self, args):
        import math
        self.robot.pose(math.radians(float(args.get("pan", 0))), math.radians(float(args.get("tilt", 0))))
        return {"ok": True, "aviso": "si el modo de control de las gafas está activo, la cabeza volverá a seguir a la persona"}

    def fn_volumen(self, args):
        nivel = max(0, min(100, int(args.get("nivel", 50))))
        self.robot.cmd(f"volume {nivel * 200 // 100}")   # 100 % = 200/255: más alto distorsiona y gasta mucho
        return {"ok": True, "nivel": nivel}

    def fn_estado(self, args):
        t = self.robot.telemetry if self.robot.online else {}
        if not t:
            return {"error": "no hay datos del robot"}
        ma = t.get("current_ma")
        return {"bateria_porcentaje": t.get("batt_pct"), "bateria_voltios": t.get("batt_v"),
                "corriente_ma": ma, "cargando": ma is not None and ma < 0,
                "wifi_rssi_dbm": t.get("rssi"), "fps_camara": t.get("fps")}

    def fn_texto(self, args):
        texto = str(args.get("texto", ""))[:60]
        self.robot.cmd("text " + texto)
        self.phone.hud(texto, ms=3000)
        return {"ok": True}

    def fn_foto(self, args):
        img = processing.ultimo_fotograma
        if img is None:
            return {"error": "todavía no ha llegado ninguna imagen de la cámara"}
        os.makedirs(self.photos_dir, exist_ok=True)
        path = os.path.join(self.photos_dir, datetime.now().strftime("foto-%Y%m%d-%H%M%S.jpg"))
        cv2.imwrite(path, img)
        log(f"Foto guardada: {path}")
        return {"ok": True, "archivo": os.path.basename(path)}

    # ------------------------------------------------------------------ hilo de la pasarela
    def on_audio_in(self, payload, addr):
        """Llega audio del micro del robot (hilo de recepción UDP)."""
        if len(payload) <= 4 or self.queue is None:
            return
        self.last_audio = time.monotonic()
        if not self.in_turn:
            self.in_turn = True
            self.heard = []
            self._put(("start", None))
            # Una sola imagen por turno, al empezar: cada imagen se queda en el
            # contexto de la sesión y hace más lentas todas las respuestas siguientes.
            if self.vision and processing.ultimo_fotograma is not None:
                ok, jpg = cv2.imencode(".jpg", processing.ultimo_fotograma, [cv2.IMWRITE_JPEG_QUALITY, 70])
                if ok:
                    self._put(("frame", jpg.tobytes()))
        self._put(("audio", payload[4:]))

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
                log(f"Gemini: sesión cerrada ({e.__class__.__name__}: {str(e)[:120]}). Reconecto en 2 s")
                await asyncio.sleep(2)

    async def _turn_watchdog(self):
        while True:
            await asyncio.sleep(0.05)
            now = time.monotonic()
            if self.in_turn and now - self.last_audio > END_OF_TURN_S:
                self.in_turn = False
                self.queue.put_nowait(("end", None))

    async def _session(self):
        log(f"Gemini: conectando a {self.model}...")
        async with self.client.aio.live.connect(model=self.model, config=self.config) as session:
            log("Gemini: listo. Habla con L1 del mando, tocando la pantalla del robot o con Enter aquí.")
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
                receiver.result()   # propaga el error si la recepción falló
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
                if sc.output_transcription and sc.output_transcription.text:
                    self.said.append(sc.output_transcription.text)
                if sc.turn_complete or sc.interrupted:
                    if self.t_end:
                        ms = lambda t: f"{(t - self.t_end) * 1000:.0f} ms" if t else "-"  # noqa: E731
                        log(f"Latencia desde que dejaste de hablar: función {ms(self.t_tool)}, primer audio {ms(self.t_audio)}"
                            + ("  (interrumpido)" if sc.interrupted else ""))
                        self.t_end = None
                    if self.heard:
                        log("Tú: " + "".join(self.heard).strip())
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
                result = f["run"](dict(fc.args or {})) if f else {"error": "función desconocida"}
            except Exception as e:
                result = {"error": str(e)}
            log(f"Gemini -> {fc.name}({dict(fc.args or {})}) = {result}")
            debug(f"tool_call id={fc.id}")
            responses.append(types.FunctionResponse(id=fc.id, name=fc.name, response=result))
        await session.send_tool_response(function_responses=responses)

    def _audio_out_loop(self):
        """Voz de Gemini (PCM 24 kHz) al altavoz del robot, en trozos que caben en un
        datagrama. Va en su propio hilo para no frenar la recepción de Gemini."""
        while True:
            pcm = self.out_queue.get()
            if not self.robot.online:
                continue
            for i in range(0, len(pcm), OUT_CHUNK):
                self.out_seq = (self.out_seq + 1) & 0xFFFFFFFF
                self.robot.send(T_AUDIO_OUT, self.out_seq.to_bytes(4, "big") + pcm[i:i + OUT_CHUNK])
                time.sleep(0.002)   # no saturar la cola de recepción del ESP32


def setup(relay):
    """Engancha la voz a la pasarela (main.py la llama antes de arrancar)."""
    global _voice
    if not os.environ.get("GEMINI_API_KEY"):
        raise SystemExit("Falta la clave de Gemini: export GEMINI_API_KEY=... (consíguela en https://aistudio.google.com/apikey)")
    _voice = Voice(relay, relay.args.modelo, relay.args.voz, not relay.args.sin_vision)
    relay.extra_robot_handlers[T_AUDIO_IN] = _voice.on_audio_in
    _voice.start()

    def keyboard():
        talking = False
        while True:
            try:
                input()
            except EOFError:
                return
            talking = not talking
            relay.robot.cmd("mic on" if talking else "mic off")
            log("Micrófono del robot " + ("ABIERTO: habla y pulsa Enter al terminar" if talking else "cerrado"))
    threading.Thread(target=keyboard, daemon=True).start()
