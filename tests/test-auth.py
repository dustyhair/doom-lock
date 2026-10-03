#!/usr/bin/env python3
"""Check PAM response ownership and post-fork locking under ASan/UBSan."""
import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
output = root / "build/test-results"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
binary = output / "test-auth"
command = [
    os.environ.get("CC", "cc"), "-std=c11", "-D_GNU_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
    "-Wno-unused-parameter", "-fno-omit-frame-pointer",
    "-I" + str(root / "include"), "-I" + str(root / "build"), "-I" + str(headers),
    str(root / "tests/test-auth.c"), "-o", str(binary), "-pthread", "-l:libpam.so.0", "-l:libev.so.4",
]
subprocess.run(command + ["-fsanitize=address,undefined", "-DAUTH_TEST_SANITIZED"], check=True)
subprocess.run([str(binary)], check=True)
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
print("PASS: Linux drops snapshot locks at fork and auth_lock_memory restores them")
