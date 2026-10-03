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
# The stub supplies both PAM services. Host policy validation has its own tests.
(output / "pam").mkdir(exist_ok=True)
subprocess.run(["cc", "-D_GNU_SOURCE", "-shared", "-fPIC", "-I" + str(root / ".build-deps/root/usr/include"),
                str(root / "tests/pam-stub.c"), "-o", str(output / "pam-stub.so")], check=True)
subprocess.run(["cc", "-I" + str(root / ".build-deps/root/usr/include"),
                str(root / "tests/x11-probe.c"), "-o", str(output / "x11-probe"),
                "-l:libxcb.so.1", "-l:libxcb-shape.so.0"], check=True)
read_fd, write_fd = os.pipe()
server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1280x1024x24", "-ac", "-noreset"],
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
    wait_for(lambda: trace_lines()[:1] == ["empty"])
    assert process.poll() is None
    return process


def shape_and_grabs():
    window = run("xdotool", "search", "--class", "^i3lock$").stdout.splitlines()[0]
    return tuple(map(int, run(str(output / "x11-probe"), window.decode()).stdout.split()))


def screenshot(name):
    path = output / name
    run("import", "-window", "root", "-define", "png:compression-level=0",
        "-define", "png:compression-filter=0", str(path))
    return Image.open(path).convert("RGB")


process = None
desktop = None
compositor = None
compositor_log = None
try:
    if os.environ.get("DOOM_TEST_PICOM"):
        configuration = output / "picom.conf"
        configuration.write_text('backend = "xrender"; shadow = true; fading = true;\n')
        compositor_log = (output / "picom.log").open("w")
        compositor = subprocess.Popen(["picom", "--config", str(configuration)],
                                      env=environment, stdout=compositor_log, stderr=compositor_log)
        time.sleep(0.3)
        assert compositor.poll() is None, "Picom did not start; see build/test-results/picom.log"
    desktop = subprocess.Popen([str(output / "x11-probe"), "--desktop"], env=environment,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    assert desktop.stdout.readline().strip(), "Desktop fixture did not start"
    process = start("password")
    assert shape_and_grabs() == (1, 1280 * 1024, 1, 1), "Desktop exposed or grabs missing before authentication"
    screenshot("lock-screen.png")
    run("xdotool", "type", "--clearmodifiers", "--delay", "35", "doom-test")
    submitted = time.monotonic()
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines(), timeout=0.8)
    assert process.poll() is None, "Locker exited before PAM returned"
    wait_for(lambda: shape_and_grabs()[1] < 1280 * 1024, timeout=3)
    count, area, keyboard, pointer = shape_and_grabs()
    assert count > 1 and 0 < area < 1280 * 1024, "Success did not create staggered falling columns"
    assert (keyboard, pointer) == (1, 1), "Success melt released input grabs early"
    time.sleep(0.5)
    melt = screenshot("success-melt.png")
    assert melt.getpixel((10, 10)) == (36, 87, 128), "Success melt did not reveal the desktop"
    desktop.stdin.write(b"805724\n")
    desktop.stdin.flush()
    live = screenshot("success-melt-live.png")
    assert live.getpixel((10, 10)) == (128, 87, 36), "Success used a stale desktop screenshot"
    assert process.poll() is None, "Locker exited before the melt completed"
    process.wait(timeout=5)
    assert time.monotonic() - submitted < 4, "Password waited for the fingerprint timeout"
    assert process.returncode == 0
    assert trace_lines() == ["empty", "match"], "Password typed during scanning was lost"
    print("PASS: correct password verifies immediately while a 10-second fingerprint scan runs")
    print("PASS: classic success melt reveals the live desktop and retains keyboard/pointer grabs")

    process = start("fingerprint")
    time.sleep(0.25)
    assert process.poll() is None, "Locker exited before fingerprint authentication"
    time.sleep(0.80)
    screenshot("bfg-launch.png")
    launch = Image.open(output / "bfg-launch.png").convert("RGB")
    time.sleep(0.32)
    screenshot("bfg-flight.png")
    flight = Image.open(output / "bfg-flight.png").convert("RGB")
    assert flight.getpixel((10, 10)) == launch.getpixel((10, 10)), "BFG flashed before hitting the monster"
    assert ImageChops.difference(launch, flight).getbbox(), "BFG projectile did not travel"
    assert process.poll() is None, "Locker exited before BFG impact"
    def bfg_impact():
        # Textured PNG captures take longer with a compositor. Observe the
        # impact rather than assuming a fixed capture delay fits the flash.
        red, green, blue = screenshot("bfg.png").getpixel((10, 10))
        return green > red * 2 and green > blue * 2
    wait_for(bfg_impact, timeout=1.2)
    wait_for(lambda: shape_and_grabs()[1] < 1280 * 1024)
    assert shape_and_grabs()[2:] == (1, 1), "Fingerprint melt released input grabs early"
    process.wait(timeout=5)
    assert process.returncode == 0 and trace_lines() == ["empty"]
    print("PASS: fingerprint success, BFG flight, impact flash, death animation, desktop melt")

    process = start("failure")
    time.sleep(1.2)
    run("xdotool", "type", "--clearmodifiers", "wrong-password")
    run("xdotool", "key", "Return")
    wait_for(lambda: "wrong" in trace_lines(), timeout=0.8)
    wait_for(lambda: "denied" in trace_lines())
    time.sleep(0.4)
    screenshot("denied.png")
    assert shape_and_grabs() == (1, 1280 * 1024, 1, 1), "Failure melt exposed the desktop or released input"
    time.sleep(0.25)
    mixed = screenshot("failure-melt.png")
    assert mixed.getpixel((10, 10))[0] > mixed.getpixel((10, 10))[1] * 4
    assert mixed.getpixel((10, 1000)) != mixed.getpixel((10, 10)), "Failure skipped the falling lock-screen columns"
    run("xdotool", "key", "Escape")
    time.sleep(2.2)
    assert process.poll() is None, "Failed authentication or Escape unlocked the screen"
    screenshot("death-screen.png")
    red, green, blue = Image.open(output / "death-screen.png").getpixel((10, 10))[:3]
    assert red > green * 4 and red > blue * 4, "Wrong password did not show persistent death screen"
    run("xdotool", "type", "--clearmodifiers", "--delay", "35", "doom-test")
    run("xdotool", "key", "Return")
    wait_for(lambda: "match" in trace_lines())
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: failure melts into death screen while fully covered and grabbed; typing retries")

    process = start("fingerprint-during-failure")
    run("xdotool", "type", "--delay", "5", "wrong")
    run("xdotool", "key", "Return")
    wait_for(lambda: "denied" in trace_lines())
    assert shape_and_grabs() == (1, 1280 * 1024, 1, 1), "Concurrent failure exposed the desktop"
    wait_for(lambda: shape_and_grabs()[1] < 1280 * 1024)
    assert process.poll() is None, "Fingerprint success during failure skipped the success melt"
    process.wait(timeout=5)
    assert process.returncode == 0 and trace_lines() == ["empty", "wrong", "denied"]
    print("PASS: fingerprint success interrupts a failure melt and completes the authenticated melt")

    process = start("animation")
    time.sleep(2.2)
    screenshot("idle.png")
    time.sleep(0.32)
    screenshot("walking.png")
    idle = Image.open(output / "idle.png").convert("RGB")
    walking = Image.open(output / "walking.png").convert("RGB")
    assert ImageChops.difference(idle, walking).getbbox(), "Idle monster did not walk or float"
    view = (0, 0, 240, 800)
    assert ImageChops.difference(idle.crop(view), walking.crop(view)).getbbox(), "Maze camera did not move"
    # Include walls and floor as well as the ceiling. Actual Doom levels can
    # have a nearly black ceiling without losing their texture palette.
    colors = idle.crop(view).resize((120, 400)).getcolors(48000)
    assert colors and len(colors) > 80, "Level background lacks textured color"
    assert max(sum(pixel) for count, pixel in colors) > 120, "Level background is too dark"
    run("xdotool", "type", "a")
    time.sleep(0.02)
    screenshot("hit.png")
    hit = Image.open(output / "hit.png").convert("RGB")
    assert ImageChops.difference(walking, hit).getbbox(), "Typing did not hit the monster"
    process.terminate()
    process.wait(timeout=3)
    print("PASS: maze with Doom level textures moves slowly; monsters walk and typing hits")

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

    process = start("queued-after-failure")
    run("xdotool", "type", "wrong")
    run("xdotool", "key", "Return")
    wait_for(lambda: "wrong" in trace_lines(), timeout=0.8)
    run("xdotool", "type", "--delay", "5", "doom-tes")
    run("xdotool", "key", "Return")
    wait_for(lambda: "denied" in trace_lines())
    time.sleep(0.65)
    screenshot("queued-denied.png")
    red, green, blue = Image.open(output / "queued-denied.png").getpixel((10, 10))[:3]
    assert red > green * 4 and red > blue * 4, "Queued edit must occur after the failure screen appears"
    run("xdotool", "type", "t")
    wait_for(lambda: "match" in trace_lines())
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: typing on the failure screen preserves the queued retry without another Enter")

    process = start("fingerprint-restart")
    for completed_scans in range(1, 4):
        wait_for(lambda: trace_lines().count("scan-timeout") == completed_scans)
        assert trace_lines().count("empty") == completed_scans
        assert process.poll() is None, "Fingerprint timeout unlocked the screen"
        time.sleep(0.1)
        if completed_scans < 3:
            run("xdotool", "key", "Return")
            wait_for(lambda: trace_lines().count("empty") == completed_scans + 1)
            run("xdotool", "key", "Return")
            assert trace_lines().count("empty") == completed_scans + 1, "Enter started overlapping scans"
    process.terminate()
    process.wait(timeout=3)
    print("PASS: consecutive empty Enter presses restart timed-out scans without overlapping workers")

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
    if desktop and desktop.poll() is None:
        desktop.terminate()
        desktop.wait(timeout=3)
    if compositor and compositor.poll() is None:
        compositor.terminate()
        compositor.wait(timeout=3)
    if compositor_log:
        compositor_log.close()
    server.terminate()
    server.wait(timeout=3)
