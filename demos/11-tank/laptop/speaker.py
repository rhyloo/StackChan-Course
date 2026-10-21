"""The Stack-chan's speaker, played from the laptop while you drive the tank.

The robot needs a firmware with audio: demo 02's (run.sh flashes it). This pairs
with the robot like a relay does (it answers its HELLO), turns its microphone off
and sends sounds as AUDIO_OUT: 16-bit PCM, 24 kHz, mono.

    s = RobotSpeaker()
    s.play(sounds.horn())
"""
import os
import queue
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
from stackchan import protocol as P  # noqa: E402
from stackchan.relay import Robot  # noqa: E402

T_AUDIO_OUT = 0x27
OUT_CHUNK = 1180            # PCM bytes per datagram (fits in 1200 with header and seq)


class RobotSpeaker:
    def __init__(self, robot=None, volume=120):
        """robot: its name ("stackchan-4F48") or IP, or None for the first one heard."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", P.ROBOT_PORT))
        self.robot = Robot(self.sock, robot)
        self.volume = volume
        self.seq = 0
        self.sounds = queue.Queue()
        threading.Thread(target=self._rx, daemon=True, name="robot_rx").start()
        threading.Thread(target=self._play_loop, daemon=True, name="robot_audio").start()

    @property
    def online(self):
        return self.robot.online

    def play(self, pcm):
        """Plays at once: a new sound replaces the ones still waiting."""
        while not self.sounds.empty():
            try:
                self.sounds.get_nowait()
            except queue.Empty:
                break
        self.sounds.put(pcm)

    def _rx(self):
        while True:
            data, addr = self.sock.recvfrom(2048)
            parsed = P.parse(data)
            if not parsed:
                continue
            mtype, payload = parsed
            if mtype == P.R_HELLO:
                info = P.load_json(payload)
                if info is not None:
                    self.robot.on_hello(info, addr)
                    if self.robot.is_mine(addr):
                        # Every HELLO, so a rebooted robot gets them too: only the speaker, no mic.
                        self.robot.cmd("listen manual")
                        self.robot.cmd(f"volume {self.volume}")
            elif self.robot.is_mine(addr):
                self.robot.last_seen = time.monotonic()   # telemetry, audio in...: it is alive

    def _play_loop(self):
        while True:
            pcm = self.sounds.get()
            if not self.robot.online:
                continue
            for i in range(0, len(pcm), OUT_CHUNK):
                self.seq = (self.seq + 1) & 0xFFFFFFFF
                self.robot.send(T_AUDIO_OUT, self.seq.to_bytes(4, "big") + pcm[i:i + OUT_CHUNK])
                time.sleep(0.002)   # don't flood the ESP32 receive queue
