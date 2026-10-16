#!/usr/bin/env python3
"""Simulated Stack-chan for testing the relay without hardware.

Speaks the robot side of the protocol exactly like firmware/src/link.cpp:
broadcasts HELLO, streams a 320x240 test pattern as R_VIDEO, sends TELEMETRY
and prints the POSE / CMD packets it receives.

    .venv/bin/python tools/fake_robot.py [--relay IP] [--fps 15]
"""
import argparse
import math
import os
import socket
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "relay"))
import cv2  # noqa: E402
import numpy as np  # noqa: E402
import protocol as P  # noqa: E402


def pattern(n, pan, tilt):
    img = np.full((240, 320, 3), (40, 30, 30), np.uint8)
    for x in range(0, 320, 32):
        cv2.line(img, (x, 0), (x, 240), (80, 60, 60), 1)
    for y in range(0, 240, 24):
        cv2.line(img, (0, y), (320, y), (80, 60, 60), 1)
    cx = int(160 + 120 * math.sin(n * 0.05))
    cv2.circle(img, (cx, 120), 18, (255, 229, 0), -1)
    cv2.putText(img, f"FAKE ROBOT #{n}", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
    cv2.putText(img, f"pan {math.degrees(pan):+.0f}  tilt {math.degrees(tilt):+.0f}", (10, 225),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 179, 255), 2)
    return cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 70])[1].tobytes()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--relay", default="255.255.255.255", help="relay IP (default: broadcast)")
    ap.add_argument("--port", type=int, default=P.ROBOT_PORT)
    ap.add_argument("--fps", type=float, default=15)
    ap.add_argument("--name", default="stackchan-FAKE")
    args = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("", 0))
    s.setblocking(False)
    hello_to = (args.relay, args.port)
    relay = None
    last_heard = last_hello = last_tel = next_frame = 0.0
    frame_id, pan, tilt, poses = 0, 0.0, 0.0, 0
    print(f"{args.name}: announcing to {hello_to[0]}:{args.port}")
    while True:
        now = time.monotonic()
        try:
            while True:
                data, addr = s.recvfrom(2048)
                msg = P.parse(data)
                if not msg or msg[0] not in (P.R_ACK, P.R_POSE, P.R_CMD):
                    continue
                if relay != addr:
                    print(f"Paired with relay {addr[0]}:{addr[1]}")
                relay, last_heard = addr, now
                if msg[0] == P.R_POSE:
                    _, pan, tilt = P.parse_pose(data)
                    poses += 1
                elif msg[0] == P.R_CMD:
                    print(f"CMD {msg[1].decode(errors='replace')}")
                    if msg[1] == b"home":
                        pan = tilt = 0.0
        except BlockingIOError:
            pass
        online = relay is not None and now - last_heard < 3
        if now - last_hello >= 1:
            last_hello = now
            s.sendto(P.json_packet(P.R_HELLO, {"name": args.name, "fw": "fake", "limits": P.DEFAULT_LIMITS}),
                     relay if online else hello_to)
        if online and now >= next_frame:
            next_frame = now + 1 / args.fps
            frame_id += 1
            for pkt in P.video_chunks(P.R_VIDEO, frame_id, pattern(frame_id, pan, tilt), int(now * 1000)):
                s.sendto(pkt, relay)
        if online and now - last_tel >= 0.5:
            last_tel = now
            s.sendto(P.json_packet(P.R_TELEMETRY, {"batt_v": 3.9, "batt_pct": 77, "pan": pan, "tilt": tilt,
                                                   "fps": args.fps, "rssi": -50}), relay)
            sys.stdout.write(f"\rposes {poses}  pan {math.degrees(pan):+6.1f}  tilt {math.degrees(tilt):+5.1f}   ")
            sys.stdout.flush()
        time.sleep(0.005)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
