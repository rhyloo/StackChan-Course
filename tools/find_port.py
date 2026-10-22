#!/usr/bin/env python3
"""Tells which USB port the robot (or an ESP32 DevKit board) is on. Used by run.sh / run.bat.

    python tools/find_port.py            # the robot: native USB of the ESP32-S3 (VID 303A)
    python tools/find_port.py --devkit   # an ESP32 DevKit: CP210x or CH34x USB-serial chip
    python tools/find_port.py --arduino  # an Arduino UNO (the tank's): ATmega16U2, CH34x, FTDI or CP210x
    python tools/find_port.py --microbit # a BBC micro:bit (its USB interface chip, VID 0D28)

Prints the port (/dev/ttyACM0, COM5...) or nothing if it doesn't find one.
"""
import sys

from serial.tools import list_ports

ROBOT_VIDS = {0x303A}              # Espressif
DEVKIT_VIDS = {0x10C4, 0x1A86}     # Silicon Labs CP210x, WCH CH34x
ARDUINO_VIDS = {0x2341, 0x2A03, 0x1A86, 0x0403, 0x10C4}   # Arduino, Arduino.org, CH34x, FTDI, CP210x
MICROBIT_VIDS = {0x0D28}           # Arm DAPLink (the micro:bit's USB chip)

vids = (DEVKIT_VIDS if "--devkit" in sys.argv else ARDUINO_VIDS if "--arduino" in sys.argv
        else MICROBIT_VIDS if "--microbit" in sys.argv else ROBOT_VIDS)
ports = sorted(p.device for p in list_ports.comports() if p.vid in vids)
if ports:
    print(ports[0])
