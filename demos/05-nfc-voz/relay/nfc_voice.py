"""La voz de la demo 02 con dos funciones nuevas para Gemini: escribir_tarjeta y leer_tarjeta.

    «escribe en la tarjeta: me llamo Stack-chan»
      Gemini --> escribir_tarjeta(texto="me llamo Stack-chan")
             --> CMD "nfc write me llamo Stack-chan" --> el robot espera la tarjeta y la escribe
             <-- NFC 0x28 {"event":"write","ok":true,...}  <-- el robot
      Gemini <-- {"ok": true}  -->  «¡Listo, ya está escrita!»

La función espera de verdad a que acerques la tarjeta (hasta ESPERA_ESCRITURA_S):
voice.py ejecuta las funciones en un hilo aparte, así que la conversación no se para.
"""
import json
import queue
import time

from google.genai import types

import voice
from stackchan.relay import log

T_NFC = 0x28                 # robot -> portátil: JSON con una lectura o el resultado de una escritura
ESPERA_ESCRITURA_S = 23      # el robot espera 20 s una tarjeta; un poco más para su respuesta
ESPERA_LECTURA_S = 6
MAX_BYTES = 140              # una NTAG213, la más pequeña, tiene 144 bytes

INSTRUCCIONES = """Eres Stack-chan, un robot pequeño y simpático que sabe leer y escribir tarjetas NFC.
Habla en español de España, con frases muy cortas (una o dos), porque te escuchan por un altavoz pequeño.
Cuando te pidan escribir o guardar algo en una tarjeta, llama a escribir_tarjeta con el texto exacto que hay
que guardar, sin la orden («escribe en la tarjeta») y sin comillas. Si no está claro qué texto quieren, pregúntalo.
La función espera a que la persona acerque la tarjeta a la base del robot; la pantalla del robot ya se lo pide.
Cuando pregunten qué pone en una tarjeta, llama a leer_tarjeta.
Si una función devuelve un error, explica el motivo en pocas palabras."""


class NFCVoice(voice.Voice):
    instrucciones = INSTRUCCIONES

    def __init__(self, *args, **kwargs):
        self.writes = queue.Queue()    # resultados de escritura que manda el robot
        self.last_tag = None           # (momento, datos) de la última tarjeta leída
        super().__init__(*args, **kwargs)

    def functions(self):
        S = types.Schema
        base = super().functions()
        return {
            "escribir_tarjeta": {
                "decl": types.FunctionDeclaration(
                    name="escribir_tarjeta",
                    description="Escribe un texto en una tarjeta NFC. Espera hasta 20 s a que la acerquen al robot "
                                "y devuelve si se ha escrito bien. El texto se guarda como registro NDEF de texto.",
                    parameters=S(type="OBJECT", required=["texto"], properties={"texto": S(type="STRING")})),
                "run": self.fn_escribir,
            },
            "leer_tarjeta": {
                "decl": types.FunctionDeclaration(
                    name="leer_tarjeta",
                    description="Lee la tarjeta NFC que está sobre el robot: tipo, número de serie y el texto que lleva."),
                "run": self.fn_leer,
            },
            "color_leds": base["color_leds"],
            "volumen": base["volumen"],
            "estado_robot": base["estado_robot"],
        }

    # ---------------------------------------------------------------- mensajes del robot
    def on_nfc(self, payload, addr):
        try:
            ev = json.loads(payload.decode("utf-8"))
        except ValueError:
            return
        if ev.get("event") == "write":
            self.writes.put(ev)
            log("Tarjeta ESCRITA: " + repr(ev.get("text")) if ev.get("ok") else f"No se pudo escribir: {ev.get('error')}")
        elif ev.get("event") == "tag":
            self.last_tag = (time.monotonic(), ev)
            log(f"Tarjeta leída: {ev.get('type')} {ev.get('uid')} -> {ev.get('text')!r}")

    # ---------------------------------------------------------------- funciones
    def fn_escribir(self, args):
        texto = " ".join(str(args.get("texto", "")).split())
        if not texto:
            return {"error": "no hay texto que escribir"}
        if not self.robot.online:
            return {"error": "el robot no está conectado"}
        recortado = texto.encode()[:MAX_BYTES].decode(errors="ignore")
        while not self.writes.empty():
            self.writes.get_nowait()
        self.robot.cmd("nfc write " + recortado)
        log(f"Esperando una tarjeta para escribir {recortado!r}...")
        try:
            ev = self.writes.get(timeout=ESPERA_ESCRITURA_S)
        except queue.Empty:
            self.robot.cmd("nfc cancel")
            return {"error": "el robot no ha contestado"}
        if not ev.get("ok"):
            return {"ok": False, "error": ev.get("error")}
        res = {"ok": True, "texto_escrito": ev.get("text")}
        if recortado != texto:
            res["nota"] = f"el texto era largo y solo caben {MAX_BYTES} bytes: se ha recortado"
        return res

    def fn_leer(self, args):
        if not self.robot.online:
            return {"error": "el robot no está conectado"}
        pedido = time.monotonic()
        self.robot.cmd("nfc read")
        while time.monotonic() - pedido < ESPERA_LECTURA_S:
            if self.last_tag and self.last_tag[0] >= pedido:
                ev = self.last_tag[1]
                if not ev.get("ok"):
                    return {"error": "hay una tarjeta pero no se pudo leer: que la acerquen más y no la muevan"}
                return {"tipo": ev.get("type"), "uid": ev.get("uid"),
                        "texto": ev.get("text") or ("(sin texto)" if ev.get("ndef") else "(vacía o sin formato NDEF)")}
            time.sleep(0.1)
        return {"error": "no hay ninguna tarjeta sobre el robot"}


def setup(relay):
    voice.setup(relay, NFCVoice)
    relay.extra_robot_handlers[T_NFC] = voice._voice.on_nfc
