#!/usr/bin/env python3
"""Builds a MakeCode (micro:bit) project without the browser and copies it to the micro:bit.

    python tools/microbit.py build demos/14-robot-arm/microbit   # -> built/binary.hex
    python tools/microbit.py flash demos/14-robot-arm/microbit   # build + copy to the MICROBIT drive

It is the compiler of makecode.microbit.org (pxt), installed with npm into
.makecode/ the first time (about 600 MB, needs Node.js). The extensions in
pxt.json (pxt-nezha2...) are downloaded from GitHub on the first build.
The same project can be opened at makecode.microbit.org: Import > Import File.
"""
import glob
import os
import shutil
import string
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLCHAIN = os.path.join(ROOT, ".makecode")
TARGET = "pxt-microbit@8.1.32"
# What the "pxt" npm command does: the CLI of pxt-core, with the micro:bit target.
CLI = ('const p = require("path"), nm = p.join(process.argv[1], "node_modules");'
       'require(p.join(nm, "pxt-core", "built", "pxt.js")).mainCli(p.join(nm, "pxt-microbit"), process.argv.slice(2))')


def fail(msg):
    sys.exit(f"\n{msg}")


def tool(name):
    path = shutil.which(name)
    if not path:
        fail(f"{name} is missing. Install Node.js (https://nodejs.org, version 18 or newer) and try again.")
    return path


def install_toolchain():
    if os.path.isdir(os.path.join(TOOLCHAIN, "node_modules", "pxt-microbit")):
        return
    print(f"Installing the MakeCode compiler ({TARGET}) in .makecode/. Only once, it takes a minute...")
    os.makedirs(TOOLCHAIN, exist_ok=True)
    with open(os.path.join(TOOLCHAIN, "package.json"), "w") as f:
        f.write('{"name": "makecode-toolchain", "private": true}\n')
    r = subprocess.run([tool("npm"), "install", "--no-audit", "--no-fund", TARGET], cwd=TOOLCHAIN)
    if r.returncode:
        fail("npm could not install the MakeCode compiler. Internet connection?")


def pxt(project, *args):
    return subprocess.run([tool("node"), "-e", CLI, TOOLCHAIN, *args], cwd=project).returncode


def build(project):
    project = os.path.abspath(project)
    if not os.path.isfile(os.path.join(project, "pxt.json")):
        fail(f"{project} is not a MakeCode project (no pxt.json).")
    install_toolchain()
    if not os.path.isdir(os.path.join(project, "pxt_modules")) and pxt(project, "install"):
        fail("Could not download the project's extensions (pxt install). Internet connection?")
    if pxt(project, "build"):
        fail("The program has errors: see above.")
    return os.path.join(project, "built", "binary.hex")


def microbit_drives():
    """Where the micro:bit's USB drive is mounted: it has a DETAILS.TXT file."""
    if os.name == "nt":
        roots = [f"{d}:\\" for d in string.ascii_uppercase]
    else:
        user = os.environ.get("USER", "")
        roots = glob.glob(f"/media/{user}/*/") + glob.glob(f"/run/media/{user}/*/") + glob.glob("/Volumes/*/")
    return [r for r in roots if os.path.isfile(os.path.join(r, "DETAILS.TXT"))]


def flash(project):
    hexfile = build(project)
    drives = microbit_drives()
    if not drives:
        fail("I can't find the micro:bit. Connect it with a USB data cable: a drive called MICROBIT appears.\n"
             f"You can also drag {hexfile} onto that drive by hand.")
    print(f"Copying {os.path.basename(hexfile)} to {drives[0]} ...")
    shutil.copy(hexfile, os.path.join(drives[0], "binary.hex"))
    print("Done. The micro:bit's yellow LED blinks while it saves the program, then it restarts on its own.")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("build", "flash"):
        sys.exit(__doc__)
    if sys.argv[1] == "build":
        print("Built:", build(sys.argv[2]))
    else:
        flash(sys.argv[2])
