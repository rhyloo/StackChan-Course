"""La parte del portátil del fotomatón: recibe la foto, aplica el filtro y devuelve
la imagen ya preparada para la impresora térmica.

    robot --PHOTO 0x29 (JPEG en trozos)--> aquí: se guarda en fotos/ y se confirma ("photo ack N")
    robot --PHOTO_EVENT 0x2A {"event":"filter","name":"ghibli"}--> filtros.aplicar() + a_termica()
    robot <--PRINT_BITMAP 0x2B (1 bit, 384 px, en trozos)-- aquí, hasta que el robot dice "bitmap_ok"
"""
import json
import os
import struct
import threading
import time
from datetime import datetime

import cv2

import filtros
from stackchan.relay import log, log_exception

T_PHOTO, T_PHOTO_EVENT, T_BITMAP = 0x29, 0x2A, 0x2B
_PHOTO = struct.Struct("!IHHI")      # photoId, idx, count, ms
_BITMAP = struct.Struct("!IHHHH")    # job, idx, count, width, height
BITMAP_CHUNK = 1000
ACK_S = 1.5
INTENTOS = 3


class Fotomaton:
    def __init__(self, relay):
        self.robot = relay.robot
        self.dir = os.path.join(os.getcwd(), "fotos")
        self.trozos = {}           # photoId -> (count, {idx: bytes})
        self.fotos = {}            # photoId -> JPEG completo
        self.job = 0
        self.confirmado = {}       # job -> threading.Event
        self.ocupado = threading.Lock()

    # ------------------------------------------------------------ mensajes del robot
    def on_photo(self, payload, addr):
        if len(payload) <= _PHOTO.size:
            return
        pid, idx, count, _ = _PHOTO.unpack_from(payload)
        if pid in self.fotos:              # el robot reenvía: nuestra confirmación se perdió
            self.robot.cmd(f"photo ack {pid}")
            return
        total, partes = self.trozos.setdefault(pid, (count, {}))
        partes[idx] = payload[_PHOTO.size:]
        if len(partes) < total:
            return
        jpeg = b"".join(partes[i] for i in range(total))
        del self.trozos[pid]
        self.fotos = {pid: jpeg}           # solo nos interesa la última
        self.robot.cmd(f"photo ack {pid}")
        os.makedirs(self.dir, exist_ok=True)
        self.base = os.path.join(self.dir, datetime.now().strftime("fotomaton-%Y%m%d-%H%M%S"))
        with open(self.base + ".jpg", "wb") as f:
            f.write(jpeg)
        log(f"Foto #{pid} recibida ({len(jpeg)} bytes): {self.base}.jpg")

    def on_event(self, payload, addr):
        try:
            ev = json.loads(payload.decode("utf-8"))
        except ValueError:
            return
        kind = ev.get("event")
        if kind == "filter":
            threading.Thread(target=self._filtrar, args=(ev.get("photo"), ev.get("name")), daemon=True).start()
        elif kind == "bitmap_ok":
            e = self.confirmado.get(ev.get("job"))
            if e:
                e.set()
        elif kind == "printed":
            log("Impresa en la MX10" if ev.get("ok") else f"No se pudo imprimir: {ev.get('error')}")

    # ------------------------------------------------------------ trabajo
    def _estado(self, texto):
        self.robot.cmd("photo status " + texto[:150])

    def _error(self, texto):
        log(f"Fotomatón: {texto}")
        self.robot.cmd("photo error " + texto[:150])

    def _filtrar(self, pid, nombre):
        if not self.ocupado.acquire(blocking=False):
            self._error("Espera, todavía estoy con el filtro anterior")
            return
        try:
            jpeg = self.fotos.get(pid)
            if jpeg is None:
                self._error("No tengo esa foto: haz otra")
                return
            t0 = time.monotonic()
            img = filtros.aplicar(nombre, jpeg, self._estado)
            self._estado("Preparando la imagen para la impresora...")
            filas, alto, vista = filtros.a_termica(img)
            cv2.imwrite(f"{self.base}-{nombre}.png", img)
            cv2.imwrite(f"{self.base}-{nombre}-termica.png", vista)
            log(f"Filtro {nombre}: {time.monotonic() - t0:.1f} s, {filtros.ANCHO}x{alto} -> {self.base}-{nombre}.png")
            if not self._enviar(filas, alto):
                self._error("El robot no ha recibido la imagen")
        except Exception as e:
            log_exception(f"filtro {nombre}")
            self._error(f"Error en el filtro: {str(e)[:100]}")
        finally:
            self.ocupado.release()

    def _enviar(self, filas, alto):
        """Manda la imagen en trozos y repite hasta que el robot confirma."""
        self.job += 1
        job, total = self.job, len(filas)
        count = -(-total // BITMAP_CHUNK)
        per = -(-total // count)           # el robot calcula el mismo tamaño de trozo
        listo = self.confirmado[job] = threading.Event()
        try:
            for _ in range(INTENTOS):
                for i in range(count):
                    head = _BITMAP.pack(job, i, count, filtros.ANCHO, alto)
                    self.robot.send(T_BITMAP, head + filas[i * per:(i + 1) * per])
                    time.sleep(0.003)
                if listo.wait(ACK_S):
                    return True
            return False
        finally:
            self.confirmado.pop(job, None)


def setup(relay):
    f = Fotomaton(relay)
    relay.extra_robot_handlers[T_PHOTO] = f.on_photo
    relay.extra_robot_handlers[T_PHOTO_EVENT] = f.on_event
    if not os.environ.get("GEMINI_API_KEY"):
        log("Sin GEMINI_API_KEY: funcionan Original, Boceto y Píxel; Ghibli, Gaming y Cómic necesitan la clave.")
    log("Fotomatón listo: toca la pantalla del robot para hacer una foto.")
