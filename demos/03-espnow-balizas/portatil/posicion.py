#!/usr/bin/env python3
"""Demo 03: estima la posición del Stack-chan a partir del RSSI de 3 balizas ESP-NOW.

    .venv/bin/python demos/03-espnow-balizas/portatil/posicion.py              # mapa en una ventana
    .venv/bin/python demos/03-espnow-balizas/portatil/posicion.py --calibrar 1 # robot a 1 m de la baliza 1

Del RSSI a la distancia, con el modelo log-distancia:

    rssi = rssi_1m - 10 * n * log10(d)   ->   d = 10 ** ((rssi_1m - rssi) / (10 * n))

rssi_1m es lo que se mide a 1 m (se calibra) y n el exponente de pérdidas: 2 en
espacio libre, 2,5-4 en interiores con paredes y gente. Con tres distancias
buscamos el punto que mejor las cumple (mínimos cuadrados).

Aviso honesto: el RSSI en interiores rebota en paredes, muebles y personas.
Espera errores de 1 a 3 m. Lo fiable es "a qué baliza estoy más cerca".
"""
import argparse
import json
import math
import os
import socket
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "lib", "python"))
from stackchan import protocol as P  # noqa: E402

PORT = 5602
MSG_BEACONS = 0x30
COLORS = {1: (60, 60, 255), 2: (60, 220, 60), 3: (255, 120, 40)}   # BGR: rojo, verde, azul como en el robot


def rssi_to_distance(rssi, rssi_1m, n):
    return 10 ** ((rssi_1m - rssi) / (10 * n))


def trilaterate(points, distances, start):
    """Gauss-Newton: el punto (x, y) cuyas distancias a `points` se parecen más a `distances`."""
    p = np.array(start, dtype=float)
    pts = np.asarray(points, dtype=float)
    d = np.asarray(distances, dtype=float)
    w = 1 / np.maximum(d, 0.3) ** 2   # las balizas cercanas son más fiables: pesan más
    for _ in range(20):
        diff = p - pts
        r = np.maximum(np.linalg.norm(diff, axis=1), 1e-6)
        J = diff / r[:, None]
        res = r - d
        step, *_ = np.linalg.lstsq(J * np.sqrt(w)[:, None], -res * np.sqrt(w), rcond=None)
        p += step
        if np.linalg.norm(step) < 1e-4:
            break
    return p


def receive(sock):
    """Último paquete del receptor, o None."""
    msg = None
    while True:
        try:
            data, _ = sock.recvfrom(2048)
        except (BlockingIOError, socket.timeout):
            return msg
        parsed = P.parse(data)
        if parsed and parsed[0] == MSG_BEACONS:
            msg = P.load_json(parsed[1]) or msg


def calibrate(sock, cfg, cfg_path, beacon_id, seconds=10):
    print(f"Pon el robot a 1 m de la baliza {beacon_id}, sin nada en medio. Midiendo {seconds} s...")
    samples = []
    end = time.time() + seconds
    while time.time() < end:
        m = receive(sock)
        for b in (m or {}).get("beacons", []):
            if b["id"] == beacon_id:
                samples.append(b["raw"])
        time.sleep(0.2)
    if not samples:
        sys.exit(f"No llegan datos de la baliza {beacon_id}. ¿Está encendida y el receptor en la misma red?")
    cfg["rssi_1m"] = round(float(np.median(samples)), 1)
    with open(cfg_path, "w") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
    print(f"rssi_1m = {cfg['rssi_1m']} dBm ({len(samples)} muestras, desviación {np.std(samples):.1f} dB). Guardado en {cfg_path}")


def run_map(sock, cfg, text_only):
    beacons = {int(k): v for k, v in cfg["balizas"].items()}
    xs = [p[0] for p in beacons.values()]
    ys = [p[1] for p in beacons.values()]
    margin = 1.5
    x0, x1, y0, y1 = min(xs) - margin, max(xs) + margin, min(ys) - margin, max(ys) + margin
    W = 800
    scale = W / (x1 - x0)
    H = int((y1 - y0) * scale)
    to_px = lambda x, y: (int((x - x0) * scale), int(H - (y - y0) * scale))  # noqa: E731
    pos = np.array([np.mean(xs), np.mean(ys)])
    trail = []
    cv2 = None
    if not text_only:
        import cv2
    print(f"Escuchando al receptor en UDP {PORT}. Ctrl+C (o 'q' en la ventana) para salir.")
    last, last_time = None, 0.0
    while True:
        m = receive(sock)
        if m is not None:
            last, last_time = m, time.monotonic()
        m = last if time.monotonic() - last_time < 2.0 else None   # el receptor envía 5 veces por segundo
        seen = {b["id"]: b for b in (m or {}).get("beacons", []) if b["id"] in beacons}
        dists = {i: rssi_to_distance(b["rssi"], cfg["rssi_1m"], cfg["n"]) for i, b in seen.items()}
        if len(dists) >= 3:
            ids = sorted(dists)
            est = trilaterate([beacons[i] for i in ids], [dists[i] for i in ids], pos)
            pos = 0.7 * pos + 0.3 * est   # suavizado
            trail = (trail + [pos.copy()])[-60:]
        status = "  ".join(f"B{i}: {seen[i]['rssi']:.0f} dBm ~{dists[i]:.1f} m" for i in sorted(seen)) or "esperando datos..."
        if len(dists) >= 3:
            status += f"  ->  x={pos[0]:.1f} y={pos[1]:.1f} m"
        elif seen:
            status += f"  (faltan balizas: con {len(seen)} no se puede triangular)"
        if text_only:
            sys.stdout.write("\r\033[K" + status)
            sys.stdout.flush()
            time.sleep(0.2)
            continue
        img = np.full((H, W, 3), 25, np.uint8)
        for gx in range(math.ceil(x0), math.floor(x1) + 1):
            cv2.line(img, to_px(gx, y0), to_px(gx, y1), (50, 50, 50), 1)
        for gy in range(math.ceil(y0), math.floor(y1) + 1):
            cv2.line(img, to_px(x0, gy), to_px(x1, gy), (50, 50, 50), 1)
        for i, (bx, by) in beacons.items():
            c = COLORS.get(i, (200, 200, 200))
            if i in dists:
                cv2.circle(img, to_px(bx, by), int(dists[i] * scale), c, 1, cv2.LINE_AA)
            cv2.circle(img, to_px(bx, by), 10, c, -1, cv2.LINE_AA)
            cv2.putText(img, str(i), (to_px(bx, by)[0] - 5, to_px(bx, by)[1] - 16), cv2.FONT_HERSHEY_SIMPLEX, 0.6, c, 2)
        for a, b in zip(trail, trail[1:]):
            cv2.line(img, to_px(*a), to_px(*b), (120, 120, 120), 1, cv2.LINE_AA)
        if len(dists) >= 3:
            cv2.circle(img, to_px(*pos), 12, (255, 255, 255), -1, cv2.LINE_AA)
            cv2.putText(img, "Stack-chan", (to_px(*pos)[0] + 16, to_px(*pos)[1] + 5), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)
        cv2.putText(img, status[:110], (10, H - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (220, 220, 220), 1)
        cv2.putText(img, "1 cuadro = 1 m", (10, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (150, 150, 150), 1)
        cv2.imshow("Stack-chan: balizas ESP-NOW", img)
        if cv2.waitKey(100) & 0xFF == ord("q"):
            break


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--config", default=os.path.join(HERE, "balizas.json"))
    ap.add_argument("--calibrar", type=int, metavar="ID", help="mide rssi_1m con el robot a 1 m de la baliza ID")
    ap.add_argument("--texto", action="store_true", help="sin ventana: solo texto en la terminal")
    args = ap.parse_args()
    with open(args.config) as f:
        cfg = json.load(f)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.setblocking(False)
    if args.calibrar:
        calibrate(sock, cfg, args.config, args.calibrar)
    else:
        run_map(sock, cfg, args.texto)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
