#!/usr/bin/env python3
"""Record the actual locker on Xvfb with test credentials and a simulated scan."""
import os
import subprocess
import time
from pathlib import Path

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
recorder = None
locker = None
desktop = None


def key(*arguments):
    subprocess.run(["xdotool", *arguments], env=environment, check=True)


def start(mode):
    trace = output / (mode + ".trace")
    trace.unlink(missing_ok=True)
    test_environment = dict(environment, LD_PRELOAD=str(stub), DOOM_TEST_MODE=mode,
                            DOOM_TEST_TRACE=str(trace), DOOM_LOCK_ASSETS=str(root / "assets"),
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
    recorder = subprocess.Popen(["ffmpeg", "-y", "-f", "x11grab", "-framerate", "25",
                                 "-video_size", "1280x1024", "-i", display,
                                 "-vf", "scale=960:768", "-c:v", "libx264", "-preset", "fast",
                                 "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                                 str(output / "doom-lock-preview.mp4")],
                                env=environment, stdin=subprocess.PIPE, stdout=log, stderr=log)
    time.sleep(2.5)
    key("type", "--clearmodifiers", "--delay", "260", "wrong")
    time.sleep(0.3)
    key("key", "Return")
    time.sleep(2)
    key("type", "--clearmodifiers", "--delay", "230", "doom-test")
    key("key", "Return")
    locker.wait(timeout=5)
    time.sleep(0.75)
    locker = start("fingerprint")
    locker.wait(timeout=5)
    time.sleep(0.75)
    recorder.communicate(input=b"q", timeout=10)
    if recorder.returncode != 0:
        raise RuntimeError("Video recording failed; see ffmpeg.log")
    print(output / "doom-lock-preview.mp4")
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
