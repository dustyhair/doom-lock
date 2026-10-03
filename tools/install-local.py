#!/usr/bin/env python3
"""Install the binary, sprites, and the existing lock-menu launcher locally."""
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

root = Path(__file__).resolve().parents[1]
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
shutil.copytree(root / "assets", share / "sprites", dirs_exist_ok=True)
shutil.copy2(root / "tools/lock-screen.sh", launcher)
launcher.chmod(0o755)
print(f"Installed {launcher}")
