#!/usr/bin/env python3
"""Download Debian/Ubuntu development headers without installing packages."""
import shutil
import subprocess
import tempfile
from pathlib import Path


PACKAGES = [
    "libcairo2-dev", "libev-dev", "libfontconfig-dev", "libfreetype-dev",
    "libpam0g-dev", "libpixman-1-dev", "libpng-dev", "libxcb-image0-dev",
    "libxcb-randr0-dev", "libxcb-render0-dev", "libxcb-shm0-dev",
    "libxcb-util-dev", "libxcb-xinerama0-dev", "libxcb-xkb-dev",
    "libxcb-xrm-dev", "libxcb1-dev", "libxkbcommon-dev", "libxkbcommon-x11-dev",
]


def main():
    for command in ["apt", "dpkg-deb", "cc"]:
        if not shutil.which(command):
            raise SystemExit(f"Required tool is missing: {command}")
    dependencies = Path(__file__).resolve().parents[1] / ".build-deps"
    packages = dependencies / "packages"
    headers = dependencies / "root"
    packages.mkdir(parents=True, exist_ok=True)
    headers.mkdir(parents=True, exist_ok=True)
    # Extract only this download's versions, even if older archives are cached.
    with tempfile.TemporaryDirectory(prefix="download-", dir=dependencies) as temporary:
        subprocess.run(["apt", "download", *PACKAGES], cwd=temporary, check=True)
        for package in sorted(Path(temporary).glob("*.deb")):
            subprocess.run(["dpkg-deb", "-x", str(package), str(headers)], check=True)
            shutil.copy2(package, packages / package.name)
    print(f"Prepared local development files in {headers}")


if __name__ == "__main__":
    main()
