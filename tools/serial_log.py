#!/usr/bin/env python3
"""Graba el monitor serie del robot en logs/ y traduce los fallos a fichero:línea.

    .venv/bin/python tools/serial_log.py demos/02-voz-gemini/firmware [puerto]

A diferencia del monitor de PlatformIO, no se cierra cuando el robot se
reinicia (el USB del CoreS3 desaparece un momento): espera y reconecta.
Cuando el ESP32 falla imprime "Backtrace: 0x4200...:0x3fc9..."; este script
busca esas direcciones en el firmware compilado (firmware.elf) con addr2line.
"""
import glob
import os
import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x303A:          # USB nativo de Espressif (CoreS3)
            return p.device
    ports = [p.device for p in list_ports.comports() if "USB" in p.device or "ACM" in p.device]
    return ports[0] if ports else None


def decode(line, elf, addr2line):
    addrs = re.findall(r"(0x4[0-9a-fA-F]{7}):0x", line)
    if not addrs or not elf or not addr2line:
        return []
    out = subprocess.run([addr2line, "-pfiaC", "-e", elf] + addrs, capture_output=True, text=True).stdout
    return ["    " + l for l in out.strip().splitlines()]


def main():
    project = sys.argv[1] if len(sys.argv) > 1 else "demos/02-voz-gemini/firmware"
    port = sys.argv[2] if len(sys.argv) > 2 else None
    elf = next(iter(glob.glob(os.path.join(ROOT, project, ".pio/build/*/firmware.elf"))), None)
    addr2line = next(iter(glob.glob(os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line"))), None)
    if not elf:
        print(f"Aviso: no encuentro {project}/.pio/build/*/firmware.elf; compílalo para poder traducir los fallos.")
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    path = os.path.join(ROOT, "logs", time.strftime("robot-%Y%m%d-%H%M%S.log"))
    print(f"Guardando en {path}. Ctrl+C para salir.")
    with open(path, "w", buffering=1) as f:
        def out(text):
            line = time.strftime("%H:%M:%S ") + text
            print(line)
            f.write(line + "\n")
        while True:
            dev = port or find_port()
            if not dev:
                time.sleep(0.5)
                continue
            try:
                with serial.Serial(dev, 115200, timeout=0.5) as s:
                    out(f"--- conectado a {dev}")
                    while True:
                        raw = s.readline()
                        if not raw:
                            continue
                        text = raw.decode(errors="replace").rstrip()
                        out(text)
                        if "Backtrace:" in text:
                            for l in decode(text, elf, addr2line):
                                out(l)
            except (serial.SerialException, OSError):
                out("--- puerto perdido (¿el robot se ha reiniciado?), esperando...")
                time.sleep(0.5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
