#!/usr/bin/env python3
"""Check long maze tours and viewport changes with native memory sanitizers."""
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = root / "build/test-results"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
binary = output / "test-level"
subprocess.run([
    "cc", "-std=c11", "-D_GNU_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-I" + str(root / "include"), "-I" + str(headers),
    "-I" + str(headers / "cairo"), "-I/usr/include/cairo",
    str(root / "tests/test-level.c"), str(root / "level.c"),
    "-o", str(binary), "-l:libcairo.so.2", "-lm",
], check=True)
for seed in [1, 17, 99]:
    subprocess.run([str(binary), str(root / "assets"), str(seed)], check=True)
    print(f"PASS: maze seed {seed}, 23-minute tour and viewport changes under ASan/UBSan")
