"""Demo 15 - Sends firmware to the robot over the Wi-Fi (OTA) and shows what it says.

    ota.py find                     the robots with the OTA base on this Wi-Fi
    ota.py tests                    the tests in firmware/ and on-demand/ (one environment each)
    ota.py send TEST                build that test and send it; then show its Log lines
    ota.py send FOLDER [--env ENV]  any other PlatformIO project (it has no OTA: USB to come back)
    ota.py send FILE.bin            an already built firmware
    ota.py log                      only show the Log lines (Ctrl+C to quit)
    --host NAME|IP                  which robot, if there are several (stackchan-4F48, 192.168.1.134)

The robot answers "who" and "log" on UDP port 3233 with its state in JSON (test, build,
state pending/valid/safe...). The firmware travels with espota.py, Arduino's OTA tool:
the robot connects back to this computer over TCP to fetch it.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parent / "firmware"
ON_DEMAND = HERE.parent / "on-demand"   # tests with the OTA off until the robot's menu turns it on
ROOT = HERE.parents[2]
PIO = ROOT / ".venv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
INFO_PORT = 3233
OTA_PORT = 3232
CONFIRM_TIMEOUT_S = 120     # the robot rolls back by itself at 90 s


def fail(msg):
    print(f"\n{msg}", file=sys.stderr)
    sys.exit(1)


def say(msg):
    print(f"\033[1;36m==> {msg}\033[0m", flush=True)


# ---------------------------------------------------------------- robots on the Wi-Fi
def open_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("", 0))
    s.settimeout(0.2)
    return s


def receive(sock):
    """One packet: ("info", dict) for the robot's state, ("log", text) for a Log line, or None."""
    try:
        data, addr = sock.recvfrom(2048)
    except (socket.timeout, ConnectionResetError):
        return None
    text = data.decode("utf-8", "replace")
    if text.startswith('{"robot"'):
        try:
            info = json.loads(text)
            info["ip"] = addr[0]
            return "info", info
        except ValueError:
            pass
    return "log", text


def find(sock, seconds=2.0):
    """Robots that answer "who" to a broadcast, by name."""
    robots = {}
    end = time.time() + seconds
    last = 0
    while time.time() < end:
        if time.time() - last > 0.5:
            last = time.time()
            try:
                sock.sendto(b"who", ("255.255.255.255", INFO_PORT))
            except OSError:
                pass
        r = receive(sock)
        if r and r[0] == "info":
            robots[r[1]["robot"]] = r[1]
    return robots


def ask(sock, ip, what=b"who", seconds=1.5):
    """The state of the robot at that address, or None."""
    end = time.time() + seconds
    last = 0
    while time.time() < end:
        if time.time() - last > 0.5:
            last = time.time()
            sock.sendto(what, (ip, INFO_PORT))
        r = receive(sock)
        if r and r[0] == "info":
            return r[1]
    return None


def pick_robot(sock, host):
    """The robot to talk to: the one named by --host, or the only one on the Wi-Fi."""
    robots = find(sock)
    if host:
        h = host.lower().removesuffix(".local")
        for info in robots.values():
            if info["robot"].lower() == h or info["ip"] == host:
                return info
        try:   # maybe the broadcast doesn't get through: ask it directly
            ip = socket.gethostbyname(host)
        except OSError:
            fail(f"I can't find '{host}' on the Wi-Fi. Is the robot on and with the OTA base (demo 15)?")
        return ask(sock, ip) or {"robot": host, "ip": ip, "test": "?", "state": "?", "boot": None}
    if not robots:
        fail("I can't find any robot with the OTA base on this Wi-Fi.\n"
             "- The first time, flash it over USB: demos/15-ota/run.sh\n"
             "- An on-demand test (side-leds, screen-colors) has the OTA off: hold its screen 3 s -> Activate OTA.\n"
             "- Is it on the same Wi-Fi as this computer? Its screen shows its IP: try --host <IP>.")
    if len(robots) > 1:
        print_robots(robots)
        fail("There are several robots: choose one with --host NAME")
    return next(iter(robots.values()))


def print_robots(robots):
    for r in sorted(robots.values(), key=lambda r: r["robot"]):
        extra = "  (the last update was rolled back)" if r.get("rolled_back") else ""
        print(f"  {r['robot']:16} {r['ip']:15} test {r['test']:10} {r['state']:8} {r['slot']}  "
              f"up {r['uptime']} s  http://{r['ip']}{extra}")


# ---------------------------------------------------------------- build and send
def tests():
    """{test name: its PlatformIO project}."""
    found = {}
    for project in (FIRMWARE, ON_DEMAND):
        ini = (project / "platformio.ini").read_text(encoding="utf-8")
        found.update((env, project) for env in re.findall(r"^\[env:([^\]]+)\]", ini, re.M))
    return found


def build(project, env=None):
    cmd = [str(PIO), "run", "-d", str(project)] + (["-e", env] if env else [])
    say(f"Building {project}{' (' + env + ')' if env else ''}...")
    if subprocess.run(cmd).returncode:
        fail("It doesn't build: look at the first error above.")
    builds = project / ".pio" / "build"
    bins = [builds / env / "firmware.bin"] if env else sorted(builds.glob("*/firmware.bin"), key=lambda p: p.stat().st_mtime)
    if not bins or not bins[-1].exists():
        fail(f"I can't find firmware.bin in {builds}")
    return bins[-1]


def espota():
    homes = [Path(os.environ["PLATFORMIO_CORE_DIR"])] if os.environ.get("PLATFORMIO_CORE_DIR") else []
    homes.append(Path.home() / ".platformio")
    for home in homes:
        script = home / "packages" / "framework-arduinoespressif32" / "tools" / "espota.py"
        if script.exists():
            return script
    fail("I can't find espota.py (it comes with the ESP32 Arduino framework). Build any firmware first.")


def ota_password():
    """OTA_PASSWORD from config/wifi_secrets.h, if you put one there (the robot then asks for it)."""
    secrets = ROOT / "config" / "wifi_secrets.h"
    m = re.search(r'#define\s+OTA_PASSWORD\s+"([^"]*)"', secrets.read_text(encoding="utf-8")) if secrets.exists() else None
    return m.group(1) if m else None


def send_bin(robot, binfile):
    size = binfile.stat().st_size
    say(f"Sending {binfile.relative_to(ROOT) if binfile.is_relative_to(ROOT) else binfile} "
        f"({size // 1024} KB) to {robot['robot']} ({robot['ip']})")
    cmd = [sys.executable, str(espota()), "-i", robot["ip"], "-p", str(OTA_PORT), "-f", str(binfile), "-r"]
    pw = ota_password()
    if pw:
        cmd += ["-a", pw]
    if subprocess.run(cmd).returncode:
        fail("The update didn't get through.\n"
             "- The robot connects back to this computer: a firewall can stop it (on Windows, allow Python).\n"
             "- If you set OTA_PASSWORD, the robot must have been flashed with the same one.\n"
             "- Is it in the middle of another update? Try again in a few seconds.")


# ---------------------------------------------------------------- after the update
def follow(sock, robot, before, keep_logging):
    """Asks the robot for its Log lines and tells what happens to the update.
    before: the robot's state before sending, or None for `log` (only Log lines and restarts).
    Returns when the update is settled, or never with keep_logging (Ctrl+C)."""
    name, ip, boot = robot["robot"], robot["ip"], robot.get("boot")
    stage = "sent" if before else "settled"    # sent -> checking -> settled
    since = last_ask = last_heard = time.time()
    while True:
        now = time.time()
        if now - last_ask > 2:
            last_ask = now
            try:
                sock.sendto(b"log", (ip, INFO_PORT))
                if now - last_heard > 4:   # restarting, or a new IP: look for it by broadcast
                    sock.sendto(b"who", ("255.255.255.255", INFO_PORT))
            except OSError:
                pass
        if stage != "settled" and now - since > CONFIRM_TIMEOUT_S:
            say("The robot doesn't answer. If you sent a firmware without the OTA base, that's normal:\n"
                "    it no longer listens on the Wi-Fi. To come back: demos/15-ota/run.sh (over USB)")
            return
        r = receive(sock)
        if not r:
            continue
        if r[0] == "log":
            print(r[1], end="" if r[1].endswith("\n") else "\n", flush=True)
            continue
        info = r[1]
        if info["robot"] != name:
            continue
        last_heard, ip = now, info["ip"]
        restarted = info.get("boot") != boot
        boot = info.get("boot")
        what = f"'{info['test']}' (build {info['build']})"
        if stage == "settled":
            if restarted:
                say(f"The robot restarted: test {what}, {info['state']}")
            continue
        if stage == "sent" and restarted and info["state"] == "pending":
            say(f"Running {what}. It stays if it works for 15 s...")
            stage = "checking"
            continue
        if stage == "checking" and not restarted:
            if info["state"] != "valid":
                continue
            say(f"Confirmed: {what} stays on the robot.")
        elif restarted:                 # from "sent" or "checking": it is settled
            if info["state"] == "safe":
                say("The robot started in safe mode: it waits for another update.")
            elif info.get("rolled_back") and info["build"] == before["build"]:
                say("ROLLED BACK: the new firmware failed before it was confirmed (a crash?).\n"
                    f"    The robot is back on {what}.")
            else:
                say(f"Running {what}, {info['state']}.")
        else:
            continue
        if not keep_logging:
            return
        stage = "settled"
        say(f"Log lines of {name} (Ctrl+C to quit)")


def main():
    ap = argparse.ArgumentParser(description="Send firmware to Stack-chan over the Wi-Fi (demo 15).")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("find", help="the robots with the OTA base on this Wi-Fi")
    sub.add_parser("tests", help="the tests in firmware/")
    p = sub.add_parser("send", help="build and send a test, a PlatformIO folder or a .bin")
    p.add_argument("target", help="test name (see `tests`), PlatformIO folder or .bin file")
    p.add_argument("--env", help="the environment, for a folder with several")
    p.add_argument("--host", help="the robot: name (stackchan-4F48) or IP")
    p.add_argument("--no-log", action="store_true", help="return once the update is settled")
    p = sub.add_parser("log", help="show the robot's Log lines")
    p.add_argument("--host", help="the robot: name (stackchan-4F48) or IP")
    args = ap.parse_args()

    if args.cmd == "tests":
        print("Tests (demos/15-ota/run.sh send NAME):")
        for t, project in tests().items():
            print(f"  {t:14} {project.name}/src/tests/{t}/")
        return
    sock = open_socket()
    if args.cmd == "find":
        robots = find(sock, 3)
        if not robots:
            fail("No robot with the OTA base answered. Same Wi-Fi? The first time, flash it over USB: demos/15-ota/run.sh")
        print_robots(robots)
        return
    robot = pick_robot(sock, args.host)
    if args.cmd == "log":
        say(f"Log lines of {robot['robot']} ({robot['ip']}), test '{robot['test']}' (Ctrl+C to quit)")
        follow(sock, robot, None, True)
        return

    # send
    target = Path(args.target)
    ours = tests().get(args.target)
    if ours:
        binfile = build(ours, args.target)
    elif target.suffix == ".bin" and target.is_file():
        binfile = target.resolve()
    elif (target / "platformio.ini").is_file():
        binfile = build(target.resolve(), args.env)
    else:
        fail(f"'{args.target}' is not a test ({', '.join(tests())}), a PlatformIO folder or a .bin file.")
    if not ours:
        say("Careful: if this firmware isn't one of demo 15's tests, it has no OTA base.\n"
            "    After it, the robot won't listen on the Wi-Fi: to come back, demos/15-ota/run.sh over USB.")
    print(f"    The robot has now: test '{robot['test']}', {robot['state']}")
    send_bin(robot, binfile)
    if ours == ON_DEMAND:   # it restarts with the OTA off: it won't answer until its menu turns it on
        say(f"Sent. The robot restarts with '{args.target}' and the OTA off.\n"
            "    It stays if the test runs 15 s; if it crashes before, the robot goes back to the previous one.\n"
            "    Next update: hold the robot's screen 3 s -> Activate OTA, then run.sh send again.")
        return
    follow(sock, robot, robot if robot.get("boot") is not None else None, not args.no_log)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
