#!/usr/bin/env python3
"""Install locally using a user-supplied WAD or optional extracted PNG assets."""
import argparse
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
sources = parser.add_mutually_exclusive_group()
sources.add_argument("--wad", type=Path, help="Save a WAD path without copying artwork")
sources.add_argument("--assets", type=Path, help="Install an extracted PNG folder")
sources.add_argument("--without-assets", action="store_true", help="Install code only")
arguments = parser.parse_args()
wad_path = arguments.wad.expanduser().resolve() if arguments.wad else None
if wad_path and (not wad_path.is_file() or "\n" in str(wad_path) or "\r" in str(wad_path)):
    parser.error("--wad must name an existing file without newlines in its path")
asset_folder = None if wad_path or arguments.without_assets else arguments.assets or root / "assets"
if asset_folder and not (asset_folder / "manifest.json").is_file():
    parser.error("No extracted assets found. Choose --wad, --assets, or --without-assets.")
bin_dir = Path.home() / ".local/bin"
share = Path.home() / ".local/share/doom-lock"
bin_dir.mkdir(parents=True, exist_ok=True)
share.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / "tools/prepare-pam.py"), str(share / "pam")], check=True)
shutil.copy2(root / "tools/prepare-pam.py", share / "prepare-pam.py")
launcher = bin_dir / "lock-screen.sh"
if launcher.exists() and launcher.read_bytes() != (root / "tools/lock-screen.sh").read_bytes():
    backup = share / ("lock-screen.sh.before-doom-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    shutil.copy2(launcher, backup)
    print(f"Saved old launcher to {backup}")
replacement = bin_dir / "i3lock-doom.new"
shutil.copy2(root / "build/i3lock-doom", replacement)
replacement.replace(bin_dir / "i3lock-doom")
if asset_folder:
    shutil.copytree(asset_folder, share / "sprites", dirs_exist_ok=True)
if wad_path:
    config = Path.home() / ".config/doom-lock"
    config.mkdir(parents=True, exist_ok=True)
    temporary = config / "wad.new"
    temporary.write_text(str(wad_path) + "\n")
    temporary.replace(config / "wad")
    print(f"Using WAD in place: {wad_path}")
elif arguments.assets:
    # Explicitly choosing PNGs should override a previous WAD installation.
    (Path.home() / ".config/doom-lock/wad").unlink(missing_ok=True)
shutil.copy2(root / "tools/lock-screen.sh", launcher)
launcher.chmod(0o755)
print(f"Installed {launcher}")
