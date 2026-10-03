#!/usr/bin/env python3
"""Install locally using a user-supplied WAD or optional extracted PNG assets."""
import argparse
import os
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/i3lock-doom",
                        help="Executable to install, including a Meson build")
    sources = parser.add_mutually_exclusive_group()
    sources.add_argument("--wad", type=Path, help="Save a WAD path without copying artwork")
    sources.add_argument("--assets", type=Path, help="Install an extracted PNG folder")
    sources.add_argument("--without-assets", action="store_true", help="Install code only")
    args = parser.parse_args()
    args.binary = args.binary.expanduser().resolve()
    if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
        parser.error("--binary must name a built executable. Build the project first.")
    if args.wad:
        args.wad = args.wad.expanduser().resolve()
        if not args.wad.is_file() or any(character in str(args.wad) for character in "\r\n"):
            parser.error("--wad must name an existing file without newlines in its path")
    assets = None if args.wad or args.without_assets else args.assets or ROOT / "assets"
    if assets and not (assets / "manifest.json").is_file():
        parser.error("No extracted assets found. Choose --wad, --assets, or --without-assets.")
    return args, assets


def install(args, assets):
    bin_dir = Path.home() / ".local/bin"
    share = Path.home() / ".local/share/doom-lock"
    # Policy preparation validates before changing an existing installation.
    subprocess.run([sys.executable, str(ROOT / "tools/prepare-pam.py"), str(share / "pam")], check=True)
    bin_dir.mkdir(parents=True, exist_ok=True)
    share.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "tools/prepare-pam.py", share / "prepare-pam.py")
    launcher = bin_dir / "lock-screen.sh"
    if launcher.exists() and launcher.read_bytes() != (ROOT / "tools/lock-screen.sh").read_bytes():
        backup = share / ("lock-screen.sh.before-doom-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
        shutil.copy2(launcher, backup)
        print(f"Saved old launcher to {backup}")
    replacement = bin_dir / "i3lock-doom.new"
    shutil.copy2(args.binary, replacement)
    replacement.replace(bin_dir / "i3lock-doom")
    if assets:
        shutil.copytree(assets, share / "sprites", dirs_exist_ok=True)
    selection = Path.home() / ".config/doom-lock/wad"
    if args.wad:
        selection.parent.mkdir(parents=True, exist_ok=True)
        temporary = selection.with_name("wad.new")
        temporary.write_text(str(args.wad) + "\n")
        temporary.replace(selection)
        print(f"Using WAD in place: {args.wad}")
    elif assets:
        # Either explicit or default PNG installation overrides a saved WAD.
        selection.unlink(missing_ok=True)
    shutil.copy2(ROOT / "tools/lock-screen.sh", launcher)
    launcher.chmod(0o755)
    print(f"Installed {launcher}")


if __name__ == "__main__":
    install(*parse_arguments())
