#!/usr/bin/env python3
"""Web page to move the robotic arm's three motors, from any browser on the Wi-Fi.

    demos/14-robot-arm/run.sh          # or: .venv/bin/python demos/14-robot-arm/laptop/arm_web.py
    ... --http-port 8014 --port /dev/ttyACM0 --no-browser

    browser --HTTP--> this server --USB serial--> micro:bit --I2C--> Nezha Pro --> M1 M2 M3

Only the standard library: the page (web/index.html) sends commands with
POST /cmd (the same text as on the serial port) and asks GET /state five
times per second for the angles, each motor's state and the new log lines.
"""
import argparse
import json
import os
import socket
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from microbit_link import MicroBit

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "web", "index.html")
LOG_LINES = 200


class Arm:
    """What the micro:bit has said: angles, motor states and the last lines."""

    def __init__(self):
        self.lock = threading.Lock()
        self.angles = [0, 0, 0]
        self.motors = [{"state": "idle", "last": ""} for _ in range(3)]
        self.speed = None
        self.lines = []               # (seq, text)
        self.seq = 0

    def on_line(self, text):
        words = text.split()
        with self.lock:
            self.seq += 1
            self.lines = (self.lines + [(self.seq, time.strftime("%H:%M:%S ") + text)])[-LOG_LINES:]
            try:
                if words[:1] == ["pos"] and len(words) == 4:
                    self.angles = [int(w) for w in words[1:]]
                elif words[:1] == ["moving"]:
                    self.motors[int(words[1][1:]) - 1] = {"state": words[2], "last": ""}
                elif words[:1] == ["stopped"]:
                    self.motors[int(words[1][1:]) - 1] = {"state": "idle", "last": words[2]}
                elif words[:1] == ["speed"]:
                    self.speed = int(words[1])
            except (ValueError, IndexError):
                pass

    def state(self, since):
        with self.lock:
            return {"angles": self.angles, "motors": self.motors, "speed": self.speed, "seq": self.seq,
                    "log": [line for line in self.lines if line[0] > since]}


def make_handler(arm, mb):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):         # no line per request in the terminal
            pass

        def _send(self, code, body, ctype="application/json"):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urlparse(self.path)
            if url.path in ("/", "/index.html"):
                with open(PAGE, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            elif url.path == "/state":
                since = int(parse_qs(url.query).get("since", ["0"])[0] or 0)
                st = arm.state(since)
                st["connected"] = mb.connected
                st["port"] = mb.device
                self._send(200, st)
            else:
                self._send(404, {"error": "not found"})

        def do_POST(self):
            if urlparse(self.path).path != "/cmd":
                self._send(404, {"error": "not found"})
                return
            n = int(self.headers.get("Content-Length") or 0)
            text = self.rfile.read(min(n, 2000)).decode(errors="replace")
            ok = all([mb.send(line) for line in text.splitlines() if line.strip()])
            self._send(200, {"ok": ok})

    return Handler


def lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="the micro:bit's serial port (default: found automatically)")
    ap.add_argument("--http-port", type=int, default=8014, help="web page port (default 8014)")
    ap.add_argument("--no-browser", action="store_true", help="don't open the page in the browser")
    args = ap.parse_args()

    arm = Arm()

    def on_line(text):
        arm.on_line(text)
        if not text.startswith(("pos ", "> ")):    # the terminal only gets what matters
            print(time.strftime("%H:%M:%S ") + text, flush=True)

    mb = MicroBit(args.port, on_line=on_line)
    server = ThreadingHTTPServer(("", args.http_port), make_handler(arm, mb))
    url = f"http://localhost:{args.http_port}"
    ip = lan_ip()
    print(f"Saving to {mb.log_path}.")
    print(f"Open {url}" + (f" (from a phone on the same Wi-Fi: http://{ip}:{args.http_port})" if ip else "")
          + ". Ctrl+C to quit.")
    if not args.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    mb.send("stop")
    time.sleep(0.2)
    print()


if __name__ == "__main__":
    main()
