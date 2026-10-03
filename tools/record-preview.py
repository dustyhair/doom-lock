#!/usr/bin/env python3
"""Record the actual locker on Xvfb with test credentials and a simulated scan."""
import argparse
import json
import os
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wad", type=Path, help="Use a local WAD directly")
parser.add_argument("--gifs", action="store_true", help="Also regenerate the README demos")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = root / "build/preview"
output.mkdir(parents=True, exist_ok=True)
stub = root / "build/test-results/pam-stub.so"
read_fd, write_fd = os.pipe()
server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1280x1024x24", "-ac", "-noreset"],
                          pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.close(write_fd)
with os.fdopen(read_fd) as pipe:
    display = ":" + pipe.readline().strip()
assert display != ":"
environment = dict(os.environ, DISPLAY=display)
environment.pop("XAUTHORITY", None)
environment.pop("LD_PRELOAD", None)
environment.pop("WAYLAND_DISPLAY", None)
if args.wad:
    environment["DOOM_LOCK_WAD"] = str(args.wad.expanduser().resolve())
recorder = None
locker = None
desktop = None


def key(*arguments):
    subprocess.run(["xdotool", *arguments], env=environment, check=True)


def start(mode):
    trace = output / (mode + ".trace")
    trace.unlink(missing_ok=True)
    test_environment = dict(environment, LD_PRELOAD=str(stub), DOOM_TEST_MODE=mode,
                            DOOM_TEST_TRACE=str(trace), DOOM_LOCK_ASSETS=str(output / "no-png-assets" if args.wad else root / "assets"),
                            DOOM_LOCK_PAM_DIR=str(root / "build/test-results/pam"))
    process = subprocess.Popen([str(root / "build/i3lock-doom"), "-n", "-c", "080808"],
                               env=test_environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 5
    while not trace.exists():
        if time.monotonic() > deadline or process.poll() is not None:
            raise RuntimeError("Locker did not start")
        time.sleep(0.03)
    return process


try:
    desktop = subprocess.Popen([str(root / "build/test-results/x11-probe"), "--desktop"],
                               env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    if not desktop.stdout.readline().strip():
        raise RuntimeError("Desktop fixture did not start")
    locker = start("password")
    log = (output / "ffmpeg.log").open("w")
    recorder = subprocess.Popen(["ffmpeg", "-y", "-f", "x11grab", "-framerate", "30",
                                 "-video_size", "1280x1024", "-i", display,
                                 "-vf", "scale=960:768", "-c:v", "libx264", "-preset", "fast",
                                 "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                                 str(output / "doom-lock-preview.mp4")],
                                env=environment, stdin=subprocess.PIPE, stdout=log, stderr=log)
    beginning = time.monotonic()
    time.sleep(10)
    password_start = time.monotonic() - beginning
    key("type", "--clearmodifiers", "--delay", "260", "wrong")
    time.sleep(0.3)
    key("key", "Return")
    time.sleep(2)
    key("type", "--clearmodifiers", "--delay", "230", "doom-test")
    key("key", "Return")
    locker.wait(timeout=5)
    time.sleep(0.75)
    fingerprint_start = time.monotonic() - beginning
    locker = start("fingerprint")
    locker.wait(timeout=5)
    time.sleep(0.75)
    recorder.communicate(input=b"q", timeout=10)
    if recorder.returncode != 0:
        raise RuntimeError("Video recording failed; see ffmpeg.log")
    video = output / "doom-lock-preview.mp4"
    print(video)
    clips = {"maze": [1, 6], "password": [password_start, fingerprint_start - password_start - 0.2],
             "fingerprint": [fingerprint_start, time.monotonic() - beginning - fingerprint_start]}
    (output / "clips.json").write_text(json.dumps(clips, indent=2) + "\n")
    if args.gifs:
        destination = root / "docs/demo"
        destination.mkdir(parents=True, exist_ok=True)
        for name, (start_time, duration) in clips.items():
            palette = output / (name + "-palette.png")
            filters = "fps=12,scale=480:-1:flags=neighbor"
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-ss", str(start_time), "-t", str(duration),
                            "-i", str(video), "-vf", filters + ",palettegen=max_colors=128:stats_mode=diff", str(palette)], check=True)
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-ss", str(start_time), "-t", str(duration),
                            "-i", str(video), "-i", str(palette), "-lavfi",
                            filters + "[scene];[scene][1:v]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle",
                            "-loop", "0", str(destination / (name + ".gif"))], check=True)
            print(destination / (name + ".gif"))
finally:
    if locker and locker.poll() is None:
        locker.terminate()
        locker.wait(timeout=3)
    if recorder and recorder.poll() is None:
        recorder.communicate(input=b"q", timeout=10)
    if desktop and desktop.poll() is None:
        desktop.terminate()
        desktop.wait(timeout=3)
    server.terminate()
    server.wait(timeout=3)
