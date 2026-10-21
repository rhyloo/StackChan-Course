# stackchan (Python)

Shared Python code for the laptop side of the demos.

| Module | What it is |
|---|---|
| `protocol.py` | The wire protocol: message types, ports, packet builders and parsers, and `FrameAssembler` (rebuilds chunked JPEG frames). Mirrors `lib/StackChanLink/src/link.cpp`. |
| `relay.py` | The base relay between the robot (UDP 5600) and the Stack-chan VR phone app (UDP 5601): pairing, video, telemetry, head control and a status line. |

## Using the relay in a demo

Each demo has a `relay/main.py` that adds `lib/python` to `sys.path` and calls:

```python
from stackchan.relay import build_parser, main
main(hooks, processing, parser=build_parser(), setup=None)
```

- `hooks` reacts to the phone: `on_button(name, pressed, robot, phone)`, `on_event(name, data, robot, phone)`, `on_sensors(readings, robot, phone)`.
- `processing` handles the video: `needs_decoding(args)` and `process(frame, args)` (an OpenCV image).
- `setup(relay)` lets a demo extend the relay before it starts, e.g. with `relay.extra_robot_handlers[type] = fn`.
- `robot` has `pose()`, `cmd()`, `send()` and `pause_phone_control()`; `phone` has `hud()`, `vibrate()`, `say()` and `torch()`.
- `log()` prints a line above the status line; with `--debug` everything also goes to `logs/relay-*.log`.

Template: `demos/01-vr-cardboard/relay/`.

## Tests

```bash
.venv/bin/python -m unittest discover -s lib/python/tests
```
