"""React to what happens on the phone. Edit freely: this is the course playground.

`robot` and `phone` are the relay's handles:
    robot.cmd("tone 1000 120")      # beep: Hz, ms
    robot.cmd("leds 0 0 255")       # base LEDs, RGB 0-255
    robot.cmd("text Hello!")         # text on the robot's screen
    robot.cmd("home")               # center the head
    robot.cmd("quality 60")         # robot JPEG quality, 5-95
    phone.hud("Hello", ms=2000)      # message in the headset
    phone.vibrate(200)              # needs "Allow phone actions" in the app
    phone.say("Hello")              # same (text to speech)

Sensors only arrive if you subscribe with --sensors/--events and the user
turns on "Share with laptop" in the app.
"""
import random


def on_button(name, pressed, robot, phone):
    """PS4 gamepad buttons: cross, circle, square, triangle, l1, r1, l2, r2,
    share, options, ps, l3, r3, dpad_up, dpad_down, dpad_left, dpad_right.
    The app already uses cross/circle/square/triangle/options; the rest are yours."""
    if not pressed:
        return
    if name == "r1":
        robot.cmd("tone 1800 60")
    elif name == "l1":
        robot.cmd("leds %d %d %d" % tuple(random.randint(0, 255) for _ in range(3)))
    elif name == "dpad_up":
        robot.cmd("text Hello from the gamepad!")


def on_event(name, data, robot, phone):
    """shake, step, significant_motion, proximity, touch, controller."""
    if name == "shake":
        robot.cmd("tone 600 200")
        phone.hud("Shake!", ms=1000)


def on_sensors(readings, robot, phone):
    """readings: {key: (values, accuracy)}, e.g. {"light": ([120.0], 3)}.
    Called for every SENSOR_DATA packet; keep it light."""
    pass
