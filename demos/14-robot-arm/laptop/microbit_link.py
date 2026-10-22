"""The micro:bit's USB serial port, shared by console.py and arm_web.py.

    mb = MicroBit(port=None, on_line=print)    # None: the first micro:bit found
    mb.send("run 1 50")

It reopens the port if the micro:bit restarts (it does after every flash) and
saves everything to logs/microbit-*.log: what it sends with "> " in front.
"""
import os
import threading
import time

import serial
from serial.tools import list_ports

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def find_port():
    ports = sorted(p.device for p in list_ports.comports() if p.vid == 0x0D28)   # the micro:bit's USB chip
    return ports[0] if ports else None


class MicroBit:
    def __init__(self, port=None, on_line=print, echo=True):
        """on_line(text) gets every line, from the micro:bit and the "---" status ones.
        echo: also pass what we send (with "> " in front)."""
        self.port = port
        self.on_line = on_line
        self.echo = echo
        self.device = None            # the open port, or None
        self._s = None
        self._lock = threading.Lock()
        os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
        self.log_path = os.path.join(ROOT, "logs", time.strftime("microbit-%Y%m%d-%H%M%S.log"))
        self._log = open(self.log_path, "w", buffering=1)
        threading.Thread(target=self._read, daemon=True, name="microbit").start()

    @property
    def connected(self):
        return self._s is not None

    def send(self, line):
        line = line.strip()
        if not line:
            return False
        self._write_log("> " + line)
        if self.echo:
            self.on_line("> " + line)
        with self._lock:
            if self._s is None:
                self._status("the micro:bit isn't connected: command not sent")
                return False
            try:
                self._s.write((line + "\n").encode())
                return True
            except (serial.SerialException, OSError):
                self._status("could not send it: port lost")
                return False

    def _write_log(self, text):
        self._log.write(time.strftime("%H:%M:%S ") + text + "\n")

    def _status(self, text):
        self._write_log("--- " + text)
        self.on_line("--- " + text)

    def _read(self):
        warned = False
        while True:
            dev = self.port or find_port()
            if not dev:
                if not warned:
                    warned = True
                    self._status("waiting for the micro:bit on USB...")
                time.sleep(0.5)
                continue
            try:
                with serial.Serial(dev, 115200, timeout=0.5) as s:
                    self._s, self.device, warned = s, dev, False
                    self._status(f"connected to {dev}")
                    s.write(b"pos\n")             # the angles straight away
                    while True:
                        raw = s.readline()
                        if raw:
                            text = raw.decode(errors="replace").strip()
                            self._write_log(text)
                            self.on_line(text)
            except (serial.SerialException, OSError):
                with self._lock:
                    self._s = self.device = None
                self._status("port lost (is the micro:bit restarting?), waiting...")
                time.sleep(0.5)
