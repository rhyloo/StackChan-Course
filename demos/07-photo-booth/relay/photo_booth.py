"""The laptop side of the photo booth: receives the photo, applies the filter and
sends back the image ready for the thermal printer.

    robot --PHOTO 0x29 (JPEG in chunks)--> here: saved in photos/ and confirmed ("photo ack N")
    robot --PHOTO_EVENT 0x2A {"event":"filter","name":"ghibli"}--> filters.apply() + to_thermal()
    robot <--PRINT_BITMAP 0x2B (1 bit, 384 px, in chunks)-- here, until the robot says "bitmap_ok"
"""
import json
import os
import struct
import threading
import time
from datetime import datetime

import cv2

import filters
from stackchan.relay import log, log_exception

T_PHOTO, T_PHOTO_EVENT, T_BITMAP = 0x29, 0x2A, 0x2B
_PHOTO = struct.Struct("!IHHI")      # photoId, idx, count, ms
_BITMAP = struct.Struct("!IHHHH")    # job, idx, count, width, height
BITMAP_CHUNK = 1000
ACK_S = 1.5
TRIES = 3


class PhotoBooth:
    def __init__(self, relay):
        self.robot = relay.robot
        self.dir = os.path.join(os.getcwd(), "photos")
        self.chunks = {}           # photoId -> (count, {idx: bytes})
        self.photos = {}           # photoId -> whole JPEG
        self.job = 0
        self.confirmed = {}        # job -> threading.Event
        self.busy = threading.Lock()

    # ------------------------------------------------------------ messages from the robot
    def on_photo(self, payload, addr):
        if len(payload) <= _PHOTO.size:
            return
        pid, idx, count, _ = _PHOTO.unpack_from(payload)
        if pid in self.photos:             # the robot is resending: our confirmation got lost
            self.robot.cmd(f"photo ack {pid}")
            return
        total, parts = self.chunks.setdefault(pid, (count, {}))
        parts[idx] = payload[_PHOTO.size:]
        if len(parts) < total:
            return
        jpeg = b"".join(parts[i] for i in range(total))
        del self.chunks[pid]
        self.photos = {pid: jpeg}          # we only care about the last one
        self.robot.cmd(f"photo ack {pid}")
        os.makedirs(self.dir, exist_ok=True)
        self.base = os.path.join(self.dir, datetime.now().strftime("photo-booth-%Y%m%d-%H%M%S"))
        with open(self.base + ".jpg", "wb") as f:
            f.write(jpeg)
        log(f"Photo #{pid} received ({len(jpeg)} bytes): {self.base}.jpg")

    def on_event(self, payload, addr):
        try:
            ev = json.loads(payload.decode("utf-8"))
        except ValueError:
            return
        kind = ev.get("event")
        if kind == "filter":
            threading.Thread(target=self._filter, args=(ev.get("photo"), ev.get("name")), daemon=True).start()
        elif kind == "bitmap_ok":
            e = self.confirmed.get(ev.get("job"))
            if e:
                e.set()
        elif kind == "printed":
            log("Printed on the MX10" if ev.get("ok") else f"Could not print: {ev.get('error')}")

    # ------------------------------------------------------------ work
    def _status(self, text):
        self.robot.cmd("photo status " + text[:150])

    def _error(self, text):
        log(f"Photo booth: {text}")
        self.robot.cmd("photo error " + text[:150])

    def _filter(self, pid, name):
        if not self.busy.acquire(blocking=False):
            self._error("Wait, still busy with the last filter")
            return
        try:
            jpeg = self.photos.get(pid)
            if jpeg is None:
                self._error("I don't have that photo: take another")
                return
            t0 = time.monotonic()
            img = filters.apply(name, jpeg, self._status)
            self._status("Getting the image ready for the printer...")
            rows, height, preview = filters.to_thermal(img)
            cv2.imwrite(f"{self.base}-{name}.png", img)
            cv2.imwrite(f"{self.base}-{name}-thermal.png", preview)
            log(f"Filter {name}: {time.monotonic() - t0:.1f} s, {filters.WIDTH}x{height} -> {self.base}-{name}.png")
            if not self._send(rows, height):
                self._error("The robot did not get the image")
        except Exception as e:
            log_exception(f"filter {name}")
            self._error(f"Filter error: {str(e)[:100]}")
        finally:
            self.busy.release()

    def _send(self, rows, height):
        """Sends the image in chunks and repeats until the robot confirms."""
        self.job += 1
        job, total = self.job, len(rows)
        count = -(-total // BITMAP_CHUNK)
        per = -(-total // count)           # the robot works out the same chunk size
        done = self.confirmed[job] = threading.Event()
        try:
            for _ in range(TRIES):
                for i in range(count):
                    head = _BITMAP.pack(job, i, count, filters.WIDTH, height)
                    self.robot.send(T_BITMAP, head + rows[i * per:(i + 1) * per])
                    time.sleep(0.003)
                if done.wait(ACK_S):
                    return True
            return False
        finally:
            self.confirmed.pop(job, None)


def setup(relay):
    f = PhotoBooth(relay)
    relay.extra_robot_handlers[T_PHOTO] = f.on_photo
    relay.extra_robot_handlers[T_PHOTO_EVENT] = f.on_event
    if not os.environ.get("GEMINI_API_KEY"):
        log("No GEMINI_API_KEY: Original, Sketch and Pixel work; Ghibli, Gaming, Comic and Picasso need the key.")
    log("Photo booth ready: tap the robot's screen to take a photo.")
