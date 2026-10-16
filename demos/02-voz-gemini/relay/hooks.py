"""Botones del mando de PS4 en la demo de voz.

L1: mantener pulsado para hablar con Gemini (como un walkie-talkie).
El resto de botones, como en la demo 01: edítalos a tu gusto.
"""
import voice


def on_button(name, pressed, robot, phone):
    if name == "l1":
        voice.push_to_talk(pressed, robot, phone)


def on_event(name, data, robot, phone):
    pass


def on_sensors(readings, robot, phone):
    pass
