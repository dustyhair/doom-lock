#!/usr/bin/env python3
"""Copy the system policy for a password check that skips the initial scan."""
import re
import shutil
import sys
from pathlib import Path


def prepare(destination):
    source = Path("/etc/pam.d")
    destination.mkdir(parents=True, exist_ok=True)
    for config in source.iterdir():
        if config.is_file():
            shutil.copyfile(config, destination / config.name)
    common = (source / "common-auth").read_text()
    auth_lines = [line for line in common.splitlines()
                  if re.match(r"\s*auth\s", line)]
    if not auth_lines or "pam_fprintd.so" not in auth_lines[0]:
        raise RuntimeError("Expected fingerprint to be the first common-auth rule")
    # Removing the first rule preserves the destinations of all later jumps.
    filtered = common.replace(auth_lines[0] + "\n", "", 1)
    if "pam_fprintd.so" in "\n".join(line for line in filtered.splitlines()
                                         if not line.lstrip().startswith("#")):
        raise RuntimeError("Unexpected additional fingerprint rule")
    (destination / "common-auth").write_text(filtered)
    shutil.copyfile(source / "i3lock", destination / "i3lock-password")
    print(f"Prepared password policy in {destination}")


if __name__ == "__main__":
    prepare(Path(sys.argv[1]))
