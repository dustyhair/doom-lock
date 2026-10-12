#!/usr/bin/env python3
"""Exercise Xinerama initialization/count changes with ASan/UBSan and Xvfb."""
import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = root / "build/test-results"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
binary = output / "test-randr"
subprocess.run([
    os.environ.get("CC", "cc"), "-std=c11", "-D_GNU_SOURCE", "-g", "-O1",
    "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-I" + str(root / "include"), "-I" + str(headers),
    str(root / "tests/test-randr.c"), str(root / "randr.c"), "-o", str(binary),
    "-l:libxcb.so.1", "-l:libxcb-randr.so.0", "-l:libxcb-xinerama.so.0",
], check=True)
read_fd, write_fd = os.pipe()
server = subprocess.Popen([
    "Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1280x1024x24",
    "-screen", "1", "640x480x24", "-extension", "RANDR", "+xinerama", "-ac",
], pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.close(write_fd)
try:
    with os.fdopen(read_fd) as pipe:
        display = ":" + pipe.readline().strip()
    assert display != ":", "Xvfb did not start"
    environment = dict(os.environ, DISPLAY=display)
    environment.pop("XAUTHORITY", None)
    environment.pop("LD_PRELOAD", None)
    subprocess.run([str(binary)], env=environment, check=True)
finally:
    server.terminate()
    server.wait(timeout=5)
