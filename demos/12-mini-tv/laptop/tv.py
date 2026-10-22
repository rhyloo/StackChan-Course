"""Demo 12 - Mini TV: turns videos into .tv files and sends them to the robot over Wi-Fi.

    tv.py                       list the robot's channels and upload the ones in channels.txt it doesn't have
    tv.py channels [FILE]       only that: download and upload what's missing from channels.txt (or FILE)
    tv.py add VIDEO [options]   convert any video (mp4, mkv, webm...) and upload it as a new channel
    tv.py list                  the channels on the robot's SD card
    tv.py remove NAME           delete a channel (NAME as `list` shows it)
    tv.py convert VIDEO -o OUT  only convert, without the robot

A .tv file is what the robot plays: a 128-byte header and, for each frame, a JPEG
(320x240 by default) followed by that frame's sound (16-bit mono PCM). ffmpeg does
the conversion. The format is described in firmware/src/main.cpp.
"""
import argparse
import http.client
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
VIDEOS = HERE.parent / "videos"          # downloaded videos (not in git)
CHANNELS = HERE.parent / "channels.txt"  # the channels `tv.py` keeps on the robot
ROBOT = "stackchan-tv.local"
JPEG_MAX = 64 * 1024                     # the robot's frame buffer


def fail(msg):
    print(f"\n{msg}", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------- conversion
def convert(video, out, title, fps=15, width=320, height=240, rate=16000, quality=6,
            start=None, duration=None, fit=False):
    """ffmpeg -> JPEG frames + raw audio -> one .tv file. Returns the number of frames."""
    if not shutil.which("ffmpeg"):
        fail("ffmpeg is missing. Install it: sudo apt install ffmpeg (Windows: winget install ffmpeg)")
    cut = (["-ss", str(start)] if start else []) + (["-t", str(duration)] if duration else [])
    if fit:   # the whole picture, with black bars
        vf = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2"
    else:     # fill the screen, cutting the sides
        vf = f"scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        print(f"Converting {Path(video).name} ({width}x{height}, {fps} fps)...")
        subprocess.run(["ffmpeg", "-loglevel", "error", *cut, "-i", str(video), "-vf", f"fps={fps},{vf}",
                        "-pix_fmt", "yuvj420p", "-q:v", str(quality), str(tmp / "f%06d.jpg")], check=True)
        # A small speaker can't play bass: cutting it leaves more room for voices and music.
        subprocess.run(["ffmpeg", "-loglevel", "error", *cut, "-i", str(video), "-vn", "-ac", "1", "-ar", str(rate),
                        "-af", "highpass=f=150", "-f", "s16le", str(tmp / "audio.raw")], check=False)
        frames = sorted(tmp.glob("f*.jpg"))
        if not frames:
            fail(f"ffmpeg didn't produce any frame from {video}.")
        audio = (tmp / "audio.raw").read_bytes() if (tmp / "audio.raw").exists() else b""
        if not audio:
            print("  (no sound in the video: the channel will be silent)")
        big = 0
        t = title.encode("utf-8")[:111]
        with open(out, "wb") as f:
            f.write(b"STV1" + struct.pack("<HHHHI", width, height, fps, rate, len(frames)) + t.ljust(112, b"\0"))
            for i, fr in enumerate(frames):
                jpg = fr.read_bytes()
                if len(jpg) > JPEG_MAX:
                    big += 1
                a0, a1 = round(i * rate / fps) * 2, round((i + 1) * rate / fps) * 2
                snd = audio[a0:a1].ljust(a1 - a0, b"\0")
                f.write(struct.pack("<II", len(jpg), len(snd)) + jpg + snd)
    size = Path(out).stat().st_size
    print(f"  {len(frames)} frames, {len(frames) / fps:.0f} s, {size / 1e6:.1f} MB")
    if big:
        print(f"  {big} frames are bigger than {JPEG_MAX // 1024} KB and the robot will skip them: use a higher --quality")
    return len(frames)


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:40] or "video"


# ---------------------------------------------------------------- robot
def connect(host, timeout=30):
    """Resolves the robot's name, waiting while it boots and joins the Wi-Fi."""
    end = time.time() + timeout
    while True:
        try:
            ip = socket.gethostbyname(host)
            c = http.client.HTTPConnection(ip, 80, timeout=10)
            c.request("GET", "/info")
            r = c.getresponse()
            info = r.read().decode().split()
            return ip, info if r.status == 200 else None
        except OSError:
            if time.time() > end:
                fail(f"I can't reach the robot at {host}.\n"
                     "- Is it on, with demo 12 flashed? Its screen shows its IP address.\n"
                     "- Is the laptop on the same Wi-Fi?\n"
                     "- If the name doesn't resolve, pass the IP: --robot 192.168.1.134")
            time.sleep(2)


def request(ip, method, path):
    c = http.client.HTTPConnection(ip, 80, timeout=20)
    c.request(method, path)
    r = c.getresponse()
    return r.status, r.read().decode()


def channels(ip):
    status, body = request(ip, "GET", "/list")
    rows = []
    for line in body.splitlines():
        p = line.split("\t")
        if len(p) >= 5:
            rows.append({"name": p[0], "bytes": int(p[1]), "frames": int(p[2]), "fps": int(p[3]), "title": p[4]})
    return rows


def show(ip, info):
    rows = channels(ip)
    if info:
        print(f"Robot {ip}: SD card {info[0]} MB, {info[1]} MB used")
    else:
        print(f"Robot {ip}: no SD card (insert a FAT32 card and restart it)")
    for n, r in enumerate(rows, 1):
        secs = r["frames"] / max(r["fps"], 1)
        print(f"  CH {n:<2} {r['name']:<34} {secs:5.0f} s  {r['bytes'] / 1e6:6.1f} MB  {r['title']}")
    if not rows:
        print("  (no channels)")
    return rows


def upload(ip, path, name):
    """Multipart POST to /upload, streamed from disk so big files don't fill the memory."""
    size = Path(path).stat().st_size
    boundary = "----stackchantv" + str(int(time.time()))
    head = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n").encode()
    tail = f"\r\n--{boundary}--\r\n".encode()
    c = http.client.HTTPConnection(ip, 80, timeout=60)
    c.putrequest("POST", f"/upload?size={size}")
    c.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
    c.putheader("Content-Length", str(len(head) + size + len(tail)))
    c.endheaders()
    c.send(head)
    sent, t0 = 0, time.time()
    with open(path, "rb") as f:
        while chunk := f.read(64 * 1024):
            c.send(chunk)
            sent += len(chunk)
            rate = sent / max(time.time() - t0, 0.01) / 1e6
            print(f"\r  Uploading {name}: {sent * 100 // size:3d} %  ({rate:.2f} MB/s)", end="", flush=True)
    c.send(tail)
    r = c.getresponse()
    body = r.read().decode().strip()
    print()
    if r.status != 200:
        fail(f"The robot refused the file: {body}")
    print(f"  Done in {time.time() - t0:.0f} s. The robot is now playing it.")


def add(ip, video, name, title, **opts):
    name = (slug(name) if name else slug(Path(video).stem)) + ".tv"
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / name
        convert(video, out, title or Path(video).stem, **opts)
        upload(ip, out, name)


def fetch(source, name):
    """The video of a channel: a local file, a web address or an HLS stream (.m3u8), saved in videos/."""
    if not re.match(r"https?://", source):
        path = Path(source).expanduser()
        path = path if path.is_absolute() else HERE.parent / path
        if not path.exists():
            fail(f"{path} doesn't exist.")
        return path
    hls = ".m3u8" in source
    dest = VIDEOS / (name + (".mp4" if hls else Path(source.split("?")[0]).suffix or ".mp4"))
    if dest.exists():
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {source}")
    part = dest.with_name(dest.name + ".part")
    if hls:   # a stream in pieces: ffmpeg joins them without converting
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", source, "-c", "copy", "-f", "mp4", str(part)], check=True)
    else:
        with urllib.request.urlopen(source, timeout=30) as r, open(part, "wb") as f:
            total = int(r.headers.get("Content-Length", 0))
            done = 0
            while chunk := r.read(256 * 1024):
                f.write(chunk)
                done += len(chunk)
                if total:
                    print(f"\r  {done * 100 // total:3d} %", end="", flush=True)
        print()
    part.rename(dest)
    return dest


def read_channels(path):
    """channels.txt: one channel per line, "name | title | video", # for comments."""
    rows = []
    for n, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 3:
            fail(f"{path}, line {n}: it should be \"name | title | video\".")
        rows.append(parts)
    return rows


def load_channels(ip, path, force=False, **opts):
    """Uploads the channels of the list that the robot's card doesn't have yet. Returns how many."""
    have = {r["name"] for r in channels(ip)}
    added = 0
    for name, title, source in read_channels(path):
        if slug(name) + ".tv" in have and not force:
            print(f"  {slug(name)}.tv is already on the card")
            continue
        add(ip, fetch(source, slug(name)), name, title, **opts)
        added += 1
    return added


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Demo 12 - Mini TV: send videos to the robot's SD card over Wi-Fi.",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("\n\n")[1])
    ap.add_argument("command", nargs="?", default="status",
                    choices=["status", "channels", "sample", "add", "list", "remove", "convert"])
    ap.add_argument("target", nargs="?", help="the video (add, convert), the channel name (remove) or the list (channels)")
    ap.add_argument("--force", action="store_true", help="channels: upload again the ones the card already has")
    ap.add_argument("--robot", default=ROBOT, help=f"robot name or IP (default {ROBOT})")
    ap.add_argument("--name", help="file name on the SD card (default: from the video's name)")
    ap.add_argument("--title", help="text shown when you switch to the channel")
    ap.add_argument("--start", help="start at this second (or hh:mm:ss)")
    ap.add_argument("--duration", help="only this many seconds")
    ap.add_argument("--fps", type=int, default=15, help="frames per second (default 15)")
    ap.add_argument("--quality", type=int, default=6, help="JPEG quality, 2 (best, bigger) to 31 (default 6)")
    ap.add_argument("--fit", action="store_true", help="show the whole picture with black bars instead of filling the screen")
    ap.add_argument("-o", "--output", help="convert: the .tv file to write")
    a = ap.parse_args()
    opts = dict(fps=a.fps, quality=a.quality, start=a.start, duration=a.duration, fit=a.fit)

    if a.command == "convert":
        if not a.target or not a.output:
            fail("Usage: tv.py convert VIDEO -o OUT.tv")
        convert(a.target, a.output, a.title or Path(a.target).stem, **opts)
        return
    if a.command in ("add", "remove") and not a.target:
        fail(f"Usage: tv.py {a.command} {'VIDEO' if a.command == 'add' else 'NAME'}")
    if a.command == "add" and not Path(a.target).exists():
        fail(f"{a.target} doesn't exist.")

    print(f"Looking for the robot ({a.robot})...")
    ip, info = connect(a.robot)
    if a.command in ("channels", "sample", "add") and not info:
        fail("The robot has no SD card. Insert a FAT32 card and restart it.")
    if a.command == "status":
        show(ip, info)
        if info:
            print(f"\nChannels in {CHANNELS.name}:")
            if load_channels(ip, CHANNELS, a.force, **opts):
                print()
                show(ip, info)
    elif a.command == "list":
        show(ip, info)
    elif a.command in ("channels", "sample"):   # "sample": its name before channels.txt
        load_channels(ip, a.target or CHANNELS, a.force, **opts)
    elif a.command == "add":
        add(ip, a.target, a.name, a.title, **opts)
    elif a.command == "remove":
        name = a.target if a.target.endswith(".tv") else a.target + ".tv"
        status, body = request(ip, "POST", f"/delete?name={name}")
        print(body.strip() if status == 200 else f"Can't delete {name}: {body.strip()}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
