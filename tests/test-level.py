#!/usr/bin/env python3
"""Check long maze tours and viewport changes with native memory sanitizers."""
import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = root / "build/test-results"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
binary = output / "test-level"
subprocess.run([
    "cc", "-std=c11", "-D_GNU_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
    "-Wno-unused-parameter", "-Wno-missing-field-initializers",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-I" + str(root / "include"), "-I" + str(root / "build"), "-I" + str(headers),
    "-I" + str(headers / "cairo"), "-I/usr/include/cairo",
    str(root / "tests/test-level.c"), str(root / "assets.c"),
    "-o", str(binary), "-l:libcairo.so.2", "-l:libev.so.4", "-l:libfontconfig.so.1", "-lm",
], check=True)
seen = set()
environment = dict(os.environ)
environment.pop("DOOM_LOCK_WAD", None)
if os.environ.get("DOOM_TEST_WAD"):
    environment["DOOM_LOCK_WAD"] = os.environ["DOOM_TEST_WAD"]
for seed in [1, 2, 3, 7, 16]:
    result = subprocess.run([str(binary), str(root / "assets"), str(seed),
                             str(output / f"level-{seed}.png"), str(output)],
                            check=True, stdout=subprocess.PIPE, text=True, env=environment)
    seen.add(result.stdout.strip())
    print(f"PASS: {result.stdout.strip()}, all 14 monster projections, backward travel, 23-minute tour under ASan/UBSan")
assert seen == {"MAP01", "MAP02", "MAP05", "MAP14", "MAP24"}, seen
