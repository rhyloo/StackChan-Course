#!/usr/bin/env python3
"""Dice en qué puerto USB está el robot (o una placa ESP32 DevKit). Lo usan los run.sh / run.bat.

    python tools/find_port.py            # el robot: USB nativo del ESP32-S3 (VID 303A)
    python tools/find_port.py --devkit   # una ESP32 DevKit: chip USB-serie CP210x o CH34x

Imprime el puerto (/dev/ttyACM0, COM5...) o nada si no lo encuentra.
"""
import sys

from serial.tools import list_ports

ROBOT_VIDS = {0x303A}              # Espressif
DEVKIT_VIDS = {0x10C4, 0x1A86}     # Silicon Labs CP210x, WCH CH34x

vids = DEVKIT_VIDS if "--devkit" in sys.argv else ROBOT_VIDS
ports = sorted(p.device for p in list_ports.comports() if p.vid in vids)
if ports:
    print(ports[0])
