#!/usr/bin/env python3
"""Check source selection in an isolated installation directory."""
import contextlib
import io
import runpy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]


class InstallTests(unittest.TestCase):
    def install(self, user_root, *arguments):
        # Inject Path.home inside Python; the real HOME environment stays intact.
        with patch.object(Path, "home", return_value=user_root), \
             patch.object(sys, "argv", ["install-local.py", *map(str, arguments)]), \
             contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(str(root / "tools/install-local.py"), run_name="__main__")

    def test_wad_path_only_and_explicit_switch_to_png(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "installation"
            wad = Path(temporary) / "my game data.wad"
            wad.write_bytes(b"caller-owned WAD")
            self.install(destination, "--wad", wad)
            self.assertEqual((destination / ".config/doom-lock/wad").read_text(), str(wad) + "\n")
            self.assertFalse((destination / ".local/share/doom-lock/sprites").exists())
            self.assertFalse(any(destination.rglob("*.wad")))
            self.assertEqual((destination / ".local/bin/i3lock-doom").read_bytes(),
                             (root / "build/i3lock-doom").read_bytes())
            self.install(destination, "--without-assets")
            self.assertTrue((destination / ".config/doom-lock/wad").exists())
            pngs = Path(temporary) / "pngs"
            pngs.mkdir()
            (pngs / "manifest.json").write_text("[]\n")
            (pngs / "sample.png").write_bytes(b"generated fixture")
            self.install(destination, "--assets", pngs)
            self.assertFalse((destination / ".config/doom-lock/wad").exists())
            self.assertEqual((destination / ".local/share/doom-lock/sprites/sample.png").read_bytes(),
                             b"generated fixture")

    def test_missing_wad_rejected_before_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "installation"
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                self.install(destination, "--wad", Path(temporary) / "absent.wad")
            self.assertEqual(error.exception.code, 2)
            self.assertFalse(destination.exists())

    def test_code_only_install_needs_no_artwork(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "installation"
            self.install(destination, "--without-assets")
            self.assertTrue((destination / ".local/bin/lock-screen.sh").is_file())
            self.assertFalse((destination / ".local/share/doom-lock/sprites").exists())
            self.assertFalse((destination / ".config/doom-lock/wad").exists())


if __name__ == "__main__":
    unittest.main()
