"""PS4 gamepad buttons in the voice demo.

L1: hold it to talk to Gemini (like a walkie-talkie).
The other buttons work as in demo 01: edit them as you like.
"""
import voice


def on_button(name, pressed, robot, phone):
    if name == "l1":
        voice.push_to_talk(pressed, robot, phone)


def on_event(name, data, robot, phone):
    pass


def on_sensors(readings, robot, phone):
    pass
