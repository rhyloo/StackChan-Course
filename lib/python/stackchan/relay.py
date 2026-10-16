#!/usr/bin/env python3
"""Laptop relay between Stack-chan and the Stack-chan VR phone app.

    robot --UDP 5600--> relay --UDP 5601--> phone (Stack-chan VR app)
    robot <--- pose ---- relay <-- control/sensors -- phone

Shared by the demos: each demo calls main() with its own `hooks` module
(reactions to buttons, sensors and events) and `processing` module (what to
do with every video frame). See demos/01-vr-cardboard/relay/.
"""
import argparse
import faulthandler
import os
import shutil
import signal
import socket
import sys
import threading
import time
import traceback

from . import protocol as P

PHONE_TIMEOUT = 3.0     # drop a phone after 3 s without HELLO (app spec 3.1)
ROBOT_TIMEOUT = 3.0


_logfile = None          # with --debug, everything also goes here
_debug = False


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    sys.stdout.write("\r\033[K" + line + "\n")
    sys.stdout.flush()
    if _logfile:
        _logfile.write(line + "\n")
        _logfile.flush()


def debug(msg):
    """Only with --debug: to the log file, not to the screen."""
    if _logfile:
        _logfile.write(time.strftime("%H:%M:%S ") + "[debug] " + msg + "\n")
        _logfile.flush()


def log_exception(where):
    log(f"ERROR en {where}: {sys.exc_info()[1]!r}" + ("" if _debug else "  (más detalle con --debug)"))
    debug(traceback.format_exc())


def start_debug_log(directory="logs"):
    """--debug: log file, tracebacks from every thread and a dump if Python itself crashes."""
    global _logfile, _debug
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, time.strftime("relay-%Y%m%d-%H%M%S.log"))
    _logfile = open(path, "w", buffering=1)
    _debug = True
    faulthandler.enable(_logfile, all_threads=True)

    def hook(args):
        log(f"ERROR no capturado en el hilo {args.thread.name}: {args.exc_value!r}")
        debug("".join(traceback.format_exception(args.exc_type, args.exc_value, args.exc_traceback)))
    threading.excepthook = hook
    log(f"Modo depuración: todo se guarda en {path}")
    return path


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class Robot:
    """The paired robot: its address, last telemetry, and how to command it."""

    def __init__(self, sock, wanted):
        self.sock = sock
        self.wanted = wanted          # --robot NAME|IP, or None for "first one heard"
        self.addr = None
        self.name = None
        self.last_seen = 0.0
        self.telemetry = {}
        self.limits = dict(P.DEFAULT_LIMITS)
        self.seen = {}                # name -> ip, every robot announcing on the network
        self.pose_seq = 0
        self.want_video = False       # someone is watching: the robot only captures then
        self.uptime = None            # from HELLO: if it goes down, the robot rebooted
        self.lock = threading.Lock()

    @property
    def online(self):
        return self.addr is not None and time.monotonic() - self.last_seen < ROBOT_TIMEOUT

    def on_hello(self, info, addr):
        name = str(info.get("name", addr[0]))
        if name not in self.seen:
            self.seen[name] = addr[0]
            log(f"Robot on the network: {name} ({addr[0]})")
        if self.wanted and self.wanted not in (name, addr[0]):
            return
        if self.online and addr != self.addr:
            return
        if self.addr != addr:
            log(f"Paired with robot {name} at {addr[0]}")
        uptime = info.get("uptime_s")
        if isinstance(uptime, (int, float)) and self.uptime is not None and uptime + 2 < self.uptime:
            reason = info.get("reset", "?")
            hint = {"panic": "el programa ha fallado: mira tools/serial_log.py",
                    "brownout": "ha caído la tensión: batería baja o pico de los servos",
                    "task_watchdog": "una tarea se ha quedado bloqueada",
                    "int_watchdog": "una interrupción se ha quedado bloqueada"}.get(reason, "")
            log(f"El robot se ha REINICIADO. Motivo: {reason}" + (f" ({hint})" if hint else ""))
        if isinstance(uptime, (int, float)):
            if self.uptime is None:
                debug(f"robot uptime {uptime} s, último reinicio: {info.get('reset')}")
            self.uptime = uptime
        self.addr, self.name = addr, name
        self.last_seen = time.monotonic()
        lim = info.get("limits")
        if isinstance(lim, dict) and "pan" in lim and "tilt" in lim:
            self.limits = {"pan": lim["pan"], "tilt": lim["tilt"]}
        self.send_ack()

    def send_ack(self):
        if self.addr is not None:
            self.sock.sendto(P.header(P.R_ACK) + bytes((1 if self.want_video else 0,)), self.addr)

    def is_mine(self, addr):
        return self.addr == addr

    def pose(self, pan, tilt):
        if not self.online:
            return
        pan = clamp(pan, *self.limits["pan"])
        tilt = clamp(tilt, *self.limits["tilt"])
        with self.lock:
            self.pose_seq += 1
            self.sock.sendto(P.pose_packet(self.pose_seq, pan, tilt), self.addr)

    def cmd(self, text):
        if self.online:
            self.sock.sendto(P.header(P.R_CMD) + text.encode()[:200], self.addr)

    def send(self, msg_type, payload=b""):
        """Any other message to the robot (demos define their own types)."""
        if self.online:
            self.sock.sendto(P.header(msg_type) + payload, self.addr)


class Phones:
    """Every phone that sent HELLO in the last 3 s gets video and telemetry."""

    def __init__(self, sock):
        self.sock = sock
        self.clients = {}             # addr -> last HELLO time
        self.actuate_seq = 0

    def hello(self, addr):
        if addr not in self.clients:
            log(f"Phone connected: {addr[0]}:{addr[1]}")
        self.clients[addr] = time.monotonic()

    def expire(self):
        now = time.monotonic()
        for addr in [a for a, t in self.clients.items() if now - t > PHONE_TIMEOUT]:
            del self.clients[addr]
            log(f"Phone disconnected: {addr[0]}")

    def send_all(self, data):
        for addr in list(self.clients):
            try:
                self.sock.sendto(data, addr)
            except OSError:
                pass

    def actuate(self, obj):
        self.actuate_seq += 1
        self.send_all(P.json_packet(P.ACTUATE, {"seq": self.actuate_seq, **obj}))

    def hud(self, text, ms=1500):
        self.actuate({"cmd": "hud_message", "text": str(text)[:80], "ms": ms})

    def vibrate(self, ms=200):
        self.actuate({"cmd": "vibrate", "ms": ms})

    def say(self, text):
        self.actuate({"cmd": "say", "text": str(text)[:200]})

    def torch(self, on):
        self.actuate({"cmd": "torch", "on": bool(on)})


class LatestFrame:
    """Single slot: the video worker always takes the newest frame, never a backlog."""

    def __init__(self):
        self.cond = threading.Condition()
        self.item = None

    def put(self, item):
        with self.cond:
            self.item = item
            self.cond.notify()

    def take(self, timeout=0.5):
        with self.cond:
            if self.item is None:
                self.cond.wait(timeout)
            item, self.item = self.item, None
            return item


class Relay:
    def __init__(self, args, hooks, processing):
        self.args = args
        self.hooks = hooks
        self.processing = processing
        self.robot_sock = self._bind(args.robot_port, broadcast=True)
        self.phone_sock = self._bind(args.port, broadcast=False)
        self.robot = Robot(self.robot_sock, args.robot)
        self.phones = Phones(self.phone_sock)
        self.frames = LatestFrame()
        self.assembler = P.FrameAssembler()
        self.out_frame_id = 0
        self.decode = processing.needs_decoding(args)
        self.extra_robot_handlers = {}    # msg type -> fn(payload, addr): lets a demo add robot messages
        self.stats = {"in": 0, "out": 0, "ctrl": 0}
        self.relay_fps = 0.0
        self.sensor_counts = {}
        self.subscribe = None
        if args.sensors or args.events:
            rates = {}
            for item in filter(None, args.sensors.split(",")):
                key, _, hz = item.partition("=")
                rates[key.strip()] = float(hz or 1)
            self.subscribe = {"rates": rates, "events": [e.strip() for e in args.events.split(",") if e.strip()]}
        self.recorder = None
        self.running = True
        self._last_frame_error = 0.0
        self._last_status_log = 0.0

    @staticmethod
    def _bind(port, broadcast):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if broadcast:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
        s.bind(("", port))
        s.settimeout(0.5)
        return s

    # ---- robot side ----
    def robot_loop(self):
        while self.running:
            try:
                data, addr = self.robot_sock.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            msg = P.parse(data)
            if not msg:
                continue
            mtype, payload = msg
            if mtype == P.R_HELLO:
                info = P.load_json(payload)
                if info is not None:
                    self.robot.on_hello(info, addr)
                continue
            if not self.robot.is_mine(addr):
                continue
            self.robot.last_seen = time.monotonic()
            if mtype == P.R_VIDEO:
                chunk = P.parse_video_chunk(data)
                if chunk:
                    done = self.assembler.add(*chunk)
                    if done:
                        self.stats["in"] += 1
                        self.frames.put(done)
            elif mtype == P.R_TELEMETRY:
                t = P.load_json(payload)
                if t is not None:
                    self.robot.telemetry = t
            elif mtype in self.extra_robot_handlers:
                try:
                    self.extra_robot_handlers[mtype](payload, addr)
                except Exception:
                    log_exception(f"mensaje del robot 0x{mtype:02x}")

    # ---- video worker: optional processing, then out to the phones ----
    def video_loop(self):
        cv2 = np = None
        if self.decode:
            import cv2
            import numpy as np
        while self.running:
            item = self.frames.take()
            if item is None:
                continue
            _, jpeg, capture_ms = item
            try:
                jpeg = self._prepare_frame(jpeg, cv2, np)
            except Exception:
                # A bug in processing.py must not stop the video: log it, at most once per second.
                now = time.monotonic()
                if now - self._last_frame_error > 1.0:
                    self._last_frame_error = now
                    log_exception("processing.process()")
                continue
            if jpeg is None:
                continue
            if self.phones.clients:
                self.out_frame_id = (self.out_frame_id + 1) & 0xFFFFFFFF
                try:
                    for pkt in P.video_chunks(P.VIDEO_CHUNK, self.out_frame_id, jpeg, capture_ms):
                        self.phones.send_all(pkt)
                    self.stats["out"] += 1
                except ValueError as e:
                    log(f"Frame not sent: {e}")

    def _prepare_frame(self, jpeg, cv2, np):
        """Decode + process + re-encode if needed. Returns the JPEG to send, or None to skip."""
        if not self.decode:
            return jpeg
        frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if frame is None:
            return None
        frame = self.processing.process(frame, self.args)
        ok, enc = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, self.args.quality])
        if not ok:
            return None
        if self.args.show:
            cv2.imshow("Stack-chan relay", frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                self.running = False
        if self.args.record:
            self._record(cv2, frame)
        return enc.tobytes()

    def _record(self, cv2, frame):
        if self.recorder is None:
            os.makedirs(self.args.record, exist_ok=True)
            path = os.path.join(self.args.record, time.strftime("stackchan-%Y%m%d-%H%M%S.mp4"))
            h, w = frame.shape[:2]
            self.recorder = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"mp4v"), 12, (w, h))
            self.record_size = (w, h)
            log(f"Recording to {path}")
        if (frame.shape[1], frame.shape[0]) == self.record_size:
            self.recorder.write(frame)

    # ---- phone side ----
    def phone_loop(self):
        while self.running:
            try:
                data, addr = self.phone_sock.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            msg = P.parse(data)
            if not msg:
                continue
            try:
                self.handle_phone(msg[0], msg[1], addr)
            except Exception:  # a bad packet or a bug in hooks.py must not kill the relay
                log_exception(f"paquete del móvil 0x{msg[0]:02x} (hooks.py)")

    def handle_phone(self, mtype, payload, addr):
        if mtype == P.DISCOVER:
            self.phone_sock.sendto(P.json_packet(P.ANNOUNCE, {
                "name": self.args.name, "proto": 1, "limits": self.robot.limits}), addr)
            return
        if mtype == P.HELLO:
            new = addr not in self.phones.clients
            self.phones.hello(addr)
            if new and self.subscribe:
                self.phone_sock.sendto(P.json_packet(P.SUBSCRIBE, self.subscribe), addr)
            return
        if addr not in self.phones.clients:
            return
        obj = P.load_json(payload)
        if mtype == P.PING and obj is not None:
            self.phone_sock.sendto(P.json_packet(P.PONG, {"t": obj.get("t", 0)}), addr)
        elif mtype == P.CONTROL and obj is not None:
            try:
                pan, tilt = float(obj["pan"]), float(obj["tilt"])
            except (KeyError, TypeError, ValueError):
                return
            self.stats["ctrl"] += 1
            self.robot.pose(pan, tilt)
        elif mtype == P.COMMAND and obj is not None:
            log(f"COMMAND {obj.get('cmd')}")
            if obj.get("cmd") == "home":
                self.robot.cmd("home")
        elif mtype == P.BUTTON and obj is not None:
            if self.args.verbose:
                log(f"BUTTON {obj.get('button')} {'down' if obj.get('pressed') else 'up'}")
            self.hooks.on_button(obj.get("button"), bool(obj.get("pressed")), self.robot, self.phones)
        elif mtype == P.EVENT and obj is not None:
            log(f"EVENT {obj.get('event')} {obj.get('data')}")
            self.hooks.on_event(obj.get("event"), obj.get("data") or {}, self.robot, self.phones)
        elif mtype == P.SENSOR_DATA and obj is not None:
            readings = {}
            for r in obj.get("r", []):
                if isinstance(r, list) and len(r) >= 3:
                    readings[r[0]] = (r[2], r[3] if len(r) > 3 else 3)
                    self.sensor_counts[r[0]] = self.sensor_counts.get(r[0], 0) + 1
            self.hooks.on_sensors(readings, self.robot, self.phones)
        elif mtype == P.DEVICE_INFO and obj is not None:
            d = obj.get("device", {})
            log(f"DEVICE_INFO part {obj.get('part')}/{obj.get('parts')}: {d.get('manufacturer')} "
                f"{d.get('model')} (SDK {d.get('android_sdk')}), {len(obj.get('sensors', []))} sensors")
        elif mtype == P.BLE_SCAN and obj is not None and self.args.verbose:
            log(f"BLE_SCAN {len(obj.get('devices', []))} devices")

    # ---- periodic: telemetry, subscription, status line ----
    def run(self):
        threads = [threading.Thread(target=f, daemon=True) for f in (self.robot_loop, self.phone_loop)]
        for t in threads:
            t.start()
        # OpenCV windows must live on the main thread on some platforms, so the
        # video worker runs here when --show is on and the timers run in a thread.
        if self.args.show:
            threading.Thread(target=self.timer_loop, daemon=True).start()
            self.video_loop()
        else:
            threading.Thread(target=self.video_loop, daemon=True).start()
            self.timer_loop()

    def timer_loop(self):
        last_tel = last_sub = last_status = time.monotonic()
        last_out = 0
        robot_was_online = False
        while self.running:
            time.sleep(0.05)
            now = time.monotonic()
            self.phones.expire()
            want = bool(self.phones.clients) or bool(self.args.show) or bool(self.args.record) or self.args.always_video
            if want != self.robot.want_video:
                self.robot.want_video = want
                self.robot.send_ack()   # don't wait for the next HELLO (up to 1 s)
            online = self.robot.online
            if online != robot_was_online:
                robot_was_online = online
                if not online:
                    log("Robot offline (no packets for 3 s)")
                    self.phones.hud("Robot offline")
            if now - last_tel >= 0.5:
                last_tel = now
                t = self.robot.telemetry
                tel = {"robot_online": online, "relay_fps": round(self.relay_fps, 1), "limits": self.robot.limits}
                if online:
                    for src, dst in (("batt_v", "batt_v"), ("batt_pct", "batt_pct"), ("pan", "pan"),
                                     ("tilt", "tilt"), ("fps", "robot_fps")):
                        if src in t:
                            tel[dst] = t[src]
                self.phones.send_all(P.json_packet(P.TELEMETRY, tel))
            if self.subscribe and now - last_sub >= 5.0:
                last_sub = now
                self.phones.send_all(P.json_packet(P.SUBSCRIBE, self.subscribe))
            if now - last_status >= 1.0:
                dt = now - last_status
                last_status = now
                self.relay_fps = (self.stats["out"] - last_out) / dt
                last_out = self.stats["out"]
                self._status(dt)

    def _status(self, dt):
        t = self.robot.telemetry if self.robot.online else {}
        robot = (f"robot {self.robot.name} {t.get('fps', 0):.1f}fps {t.get('jpeg_bytes', 0) // 1024}KB "
                 f"bat {t.get('batt_pct', '?')}% {t.get('current_ma', '?')}mA rssi {t.get('rssi', '?')}" if self.robot.online
                 else "robot: searching...")
        sensors = " ".join(f"{k}:{v / dt:.0f}Hz" for k, v in sorted(self.sensor_counts.items()))
        self.sensor_counts.clear()
        line = (f"{robot} | phones {len(self.phones.clients)} out {self.relay_fps:.1f}fps "
                f"ctrl {self.stats['ctrl'] / dt:.0f}/s drop {self.assembler.dropped} {sensors}")
        self.stats["ctrl"] = 0
        now = time.monotonic()
        if now - self._last_status_log >= 10:
            self._last_status_log = now
            debug("estado: " + line)
        if self.args.quiet or not sys.stdout.isatty():
            return
        # Exactly one terminal line: a longer one wraps and "\r" only rewrites
        # the last piece, so the screen fills up with copies.
        width = shutil.get_terminal_size((100, 20)).columns - 1
        sys.stdout.write("\r\033[K" + line[:width])
        sys.stdout.flush()


def build_parser(description="Stack-chan laptop relay (robot <-> phone)"):
    p = argparse.ArgumentParser(description=description)
    p.add_argument("--name", default=socket.gethostname(), help="name the phone sees (default: hostname)")
    p.add_argument("--robot", help="pair only with this robot (name shown on its screen, or its IP)")
    p.add_argument("--port", type=int, default=P.PHONE_PORT, help="UDP port for the phone (default 5601)")
    p.add_argument("--robot-port", type=int, default=P.ROBOT_PORT, help="UDP port for the robot (default 5600)")
    p.add_argument("--show", action="store_true", help="show the video in a window on the laptop")
    p.add_argument("--record", metavar="DIR", help="record the processed video to DIR as .mp4")
    p.add_argument("--always-video", action="store_true",
                   help="keep the robot streaming even with nobody watching (uses more battery)")
    p.add_argument("--enhance", action="store_true", help="boost contrast (CLAHE)")
    p.add_argument("--upscale", type=int, default=1, choices=(1, 2, 3), help="resize factor before sending")
    p.add_argument("--quality", type=int, default=80, help="JPEG quality when re-encoding (default 80)")
    p.add_argument("--sensors", default="", help="phone sensors to subscribe, e.g. accelerometer=20,light=2")
    p.add_argument("--events", default="", help="phone events to subscribe, e.g. shake,touch,proximity")
    p.add_argument("-v", "--verbose", action="store_true", help="print buttons and BLE scans")
    p.add_argument("-q", "--quiet", action="store_true", help="no status line (only events and errors)")
    p.add_argument("--debug", action="store_true",
                   help="save everything (errors with traceback, Gemini messages, robot reboots) to logs/")
    return p


def local_ips():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "?"


def main(hooks, processing, parser=None, setup=None, argv=None):
    """Runs the relay. `setup(relay)` lets a demo extend it before it starts."""
    args = (parser or build_parser()).parse_args(argv)
    # Stop cleanly on SIGTERM too, so a recording is closed properly.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt))
    if args.debug:
        start_debug_log()
    relay = Relay(args, hooks, processing)
    if setup:
        setup(relay)
    log(f"Stack-chan VR relay '{args.name}' on {local_ips()}: robot UDP {args.robot_port}, phone UDP {args.port}")
    log("Processing: " + ("decode + process()" if relay.decode else "pass-through (no re-encoding)"))
    if relay.subscribe:
        log(f"Subscribing phone sensors: {relay.subscribe}")
    try:
        relay.run()
    except KeyboardInterrupt:
        pass
    relay.running = False
    if relay.recorder is not None:
        relay.recorder.release()
    print("\nRelay stopped.")
