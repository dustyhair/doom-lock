#!/usr/bin/env python3
"""Build against user-local headers and the installed runtime libraries."""
import argparse
import os
import shlex
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--without-wad", action="store_true", help="Build PNG support only")
arguments = parser.parse_args()
build = root / "build"
build.mkdir(exist_ok=True)
(build / "config.h").write_text('''#define I3LOCK_VERSION "2.16-doom"
#define SYSCONFDIR "/etc"
#define HAVE_STRNDUP 1
#define HAVE_EXPLICIT_BZERO 1
''' + f'#define DOOM_WAD_ASSETS {int(not arguments.without_wad)}\n')
headers = root / ".build-deps/root/usr/include"
if not headers.exists():
    headers = Path("/usr/include")
if any(not (headers / name).is_file() for name in ["ev.h", "security/pam_appl.h", "xcb/shape.h"]):
    raise SystemExit("Missing development headers. Install build dependencies or run: python3 tools/bootstrap-deps.py")
libraries = ["ev.so.4", "pam.so.0", "cairo.so.2", "xcb.so.1", "xcb-xkb.so.1",
             "xcb-xinerama.so.0", "xcb-randr.so.0", "xcb-image.so.0",
             "xcb-util.so.1", "xcb-xrm.so.0", "xcb-shape.so.0", "xkbcommon.so.0", "xkbcommon-x11.so.0"]
command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-D_GNU_SOURCE", "-O2", "-Wall", "-Wextra",
           "-fno-strict-aliasing",
           "-Wno-unused-parameter", "-Wno-missing-field-initializers", "-pthread",
           *shlex.split(os.environ.get("CFLAGS", "")),
           "-I" + str(build), "-I" + str(root / "include"), "-I" + str(headers),
           "-I" + str(headers / "cairo"), "-I/usr/include/cairo",
           *[str(root / name) for name in ["auth.c", "hud.c", "dpi.c", "i3lock.c", "randr.c", "unlock_indicator.c", "xcb.c", "doom.c", "melt.c", "level.c", "assets.c"]],
           "-o", str(build / "i3lock-doom"), "-lm", "-lrt",
           *["-l:lib" + library for library in libraries]]
subprocess.run(command, check=True)
print(build / "i3lock-doom")
