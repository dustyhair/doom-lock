#!/usr/bin/env python3
"""Exercise the real X11 event loop with simulated PAM on an isolated display."""
import os
import subprocess
import time
from pathlib import Path
from PIL import Image, ImageChops

root = Path(__file__).resolve().parents[1]
output = root / "build/test-results"
output.mkdir(parents=True, exist_ok=True)
subprocess.run(["python3", str(root / "tools/prepare-pam.py"), str(output / "pam")], check=True)
subprocess.run(["cc", "-D_GNU_SOURCE", "-shared", "-fPIC", "-I" + str(root / ".build-deps/root/usr/include"),
                str(root / "tests/pam-stub.c"), "-o", str(output / "pam-stub.so")], check=True)
read_fd, write_fd = os.pipe()
server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1280x1024x24", "-ac"],
                          pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.close(write_fd)
with os.fdopen(read_fd) as pipe:
    display = ":" + pipe.readline().strip()
assert display != ":", "Xvfb did not start"
environment = dict(os.environ, DISPLAY=display, LD_PRELOAD=str(output / "pam-stub.so"),
                   DOOM_LOCK_ASSETS=str(root / "assets"))
environment["DOOM_LOCK_PAM_DIR"] = str(output / "pam")
environment.pop("XAUTHORITY", None)


def run(*command):
    return subprocess.run(command, env=environment, check=True, capture_output=True)


def trace_lines():
    trace = Path(environment["DOOM_TEST_TRACE"])
    return trace.read_text().splitlines() if trace.exists() else []


def wait_for(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.03)
    raise AssertionError("Timed out")


def start(mode):
    trace = output / f"{mode}.trace"
    trace.unlink(missing_ok=True)
    environment.update(DOOM_TEST_MODE=mode, DOOM_TEST_TRACE=str(trace))
    process = subprocess.Popen([str(root / "build/i3lock-doom"), "-n", "-c", "080808"],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wait_for(lambda: trace_lines() == ["empty"])
    assert process.poll() is None
    return process


process = None
try:
    process = start("password")
    run("import", "-window", "root", str(output / "lock-screen.png"))
    run("xdotool", "type", "--clearmodifiers", "--delay", "35", "doom-test")
    submitted = time.monotonic()
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines(), timeout=0.8)
    assert process.poll() is None, "Locker exited before PAM returned"
    process.wait(timeout=5)
    assert time.monotonic() - submitted < 3, "Password waited for the fingerprint timeout"
    assert process.returncode == 0
    assert trace_lines() == ["empty", "match"], "Password typed during scanning was lost"
    print("PASS: correct password verifies immediately while a 10-second fingerprint scan runs")

    process = start("fingerprint")
    time.sleep(0.25)
    assert process.poll() is None, "Locker exited before fingerprint authentication"
    time.sleep(0.80)
    run("import", "-window", "root", str(output / "bfg-launch.png"))
    launch = Image.open(output / "bfg-launch.png").convert("RGB")
    assert launch.getpixel((10, 10)) == (8, 8, 8), "BFG flashed before hitting the monster"
    time.sleep(0.32)
    run("import", "-window", "root", str(output / "bfg-flight.png"))
    flight = Image.open(output / "bfg-flight.png").convert("RGB")
    assert ImageChops.difference(launch, flight).getbbox(), "BFG projectile did not travel"
    assert process.poll() is None, "Locker exited before BFG impact"
    time.sleep(0.50)
    run("import", "-window", "root", str(output / "bfg.png"))
    red, green, blue = Image.open(output / "bfg.png").getpixel((10, 10))[:3]
    assert green > red * 2 and green > blue * 2, "Fingerprint success did not fire BFG"
    process.wait(timeout=5)
    assert process.returncode == 0 and trace_lines() == ["empty"]
    print("PASS: fingerprint success, BFG flight, impact flash, death animation")

    process = start("failure")
    time.sleep(1.2)
    run("xdotool", "type", "--clearmodifiers", "wrong-password")
    run("xdotool", "key", "Return")
    wait_for(lambda: "wrong" in trace_lines(), timeout=0.8)
    time.sleep(0.4)
    run("import", "-window", "root", str(output / "denied.png"))
    run("xdotool", "key", "Escape")
    time.sleep(2.2)
    assert process.poll() is None, "Failed authentication or Escape unlocked the screen"
    run("import", "-window", "root", str(output / "death-screen.png"))
    red, green, blue = Image.open(output / "death-screen.png").getpixel((10, 10))[:3]
    assert red > green * 4 and red > blue * 4, "Wrong password did not show persistent death screen"
    run("xdotool", "type", "--clearmodifiers", "--delay", "35", "doom-test")
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines())
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: failed authentication stays locked on death screen; typing retries successfully")

    process = start("animation")
    time.sleep(2.2)
    run("import", "-window", "root", str(output / "idle.png"))
    time.sleep(0.32)
    run("import", "-window", "root", str(output / "walking.png"))
    idle = Image.open(output / "idle.png").convert("RGB")
    walking = Image.open(output / "walking.png").convert("RGB")
    assert ImageChops.difference(idle, walking).getbbox(), "Idle monster did not walk or float"
    run("xdotool", "type", "a")
    time.sleep(0.02)
    run("import", "-window", "root", str(output / "hit.png"))
    hit = Image.open(output / "hit.png").convert("RGB")
    assert ImageChops.difference(walking, hit).getbbox(), "Typing did not hit the monster"
    process.terminate()
    process.wait(timeout=3)
    print("PASS: idle walking or floating, keystroke hit animation")

    process = start("editing")
    run("xdotool", "type", "doom-tesx")
    run("xdotool", "key", "BackSpace")
    run("xdotool", "type", "t")
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines(), timeout=0.8)
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: Backspace corrects password input during a fingerprint scan")

    process = start("clear-input")
    run("xdotool", "type", "wrong-prefix")
    run("xdotool", "key", "ctrl+u")
    run("xdotool", "type", "doom-test")
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines(), timeout=0.8)
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: Ctrl+U clears the password without disabling fingerprint authentication")

    process = start("queued-edit")
    run("xdotool", "type", "wrong")
    run("xdotool", "key", "Return")
    wait_for(lambda: "wrong" in trace_lines(), timeout=0.8)
    run("xdotool", "type", "--delay", "5", "doom-tesx")
    run("xdotool", "key", "Return")
    run("xdotool", "key", "BackSpace")
    run("xdotool", "type", "t")
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines())
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: editing a queued retry after Enter preserves the password")

    trace = output / "daemon.trace"
    trace.unlink(missing_ok=True)
    environment.update(DOOM_TEST_MODE="fingerprint", DOOM_TEST_TRACE=str(trace))
    process = subprocess.Popen([str(root / "build/i3lock-doom"), "-c", "080808"], env=environment)
    process.wait(timeout=3)
    assert process.returncode == 0
    wait_for(lambda: trace_lines() == ["empty"])
    assert run("xdotool", "search", "--class", "^i3lock$").stdout.strip()
    def unlocked():
        return subprocess.run(["xdotool", "search", "--class", "^i3lock$"],
                              env=environment, capture_output=True).returncode != 0
    wait_for(unlocked)
    print("PASS: daemon fork, automatic fingerprint startup, authenticated exit")
finally:
    if process and process.poll() is None:
        process.terminate()
        process.wait(timeout=3)
    server.terminate()
    server.wait(timeout=3)
