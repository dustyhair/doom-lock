#!/usr/bin/env python3
"""Check WAD selection and fallback without running any real screen locker."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

root = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.destination = Path(self.temporary.name) / "installation with spaces"
        binary = self.destination / ".local/bin"
        share = self.destination / ".local/share/doom-lock"
        binary.mkdir(parents=True)
        share.mkdir(parents=True)
        self.log = self.destination / "commands.log"
        (share / "prepare-pam.py").write_text(
            'import os\nraise SystemExit(int(os.environ.get("doom_test_policy_status", "0")))\n')
        scripts = {
            "i3lock-doom": 'printf "doom:%s\\n" "$DOOM_LOCK_WAD" >> "$doom_test_log"\nexit "${doom_test_status:-0}"\n',
            "i3lock": 'printf "fallback:%s\\n" "$*" >> "$doom_test_log"\n',
            "xdotool": 'printf "input:%s\\n" "$*" >> "$doom_test_log"\n',
        }
        for name, body in scripts.items():
            path = binary / name
            path.write_text("#!/bin/sh\n" + body)
            path.chmod(0o755)
        # Redirect only the test copy's home lookup to a private variable.
        # The real HOME is preserved and all locker commands are stubbed.
        self.launcher = self.destination / "launcher.sh"
        self.launcher.write_text((root / "tools/lock-screen.sh").read_text().replace("$HOME", "$doom_test_home"))
        self.environment = dict(os.environ, doom_test_home=str(self.destination),
                                doom_test_log=str(self.log), PATH=str(binary) + ":" + os.environ["PATH"])
        self.environment.pop("DOOM_LOCK_WAD", None)
        self.environment.pop("DOOM_LOCK_ASSETS", None)

    def select_wad(self, name="game data.wad"):
        wad = self.destination / name
        wad.write_bytes(b"generated WAD fixture")
        config = self.destination / ".config/doom-lock"
        config.mkdir(parents=True, exist_ok=True)
        (config / "wad").write_text(str(wad) + "\n")
        return wad

    def run_launcher(self, **variables):
        subprocess.run(["sh", str(self.launcher)], env=dict(self.environment, **variables), check=True)
        return self.log.read_text().splitlines()

    def test_saved_path_is_literal_and_needs_no_pngs(self):
        wad = self.select_wad("game $(touch injected).wad")
        self.assertEqual(self.run_launcher(), ["doom:" + str(wad)])
        self.assertFalse((root / "injected").exists())

    def test_environment_overrides_saved_path(self):
        self.select_wad()
        self.assertEqual(self.run_launcher(DOOM_LOCK_WAD="/caller chosen/game.wad"),
                         ["doom:/caller chosen/game.wad"])

    def test_png_mode_remains_available(self):
        pngs = self.destination / ".local/share/doom-lock/sprites"
        pngs.mkdir()
        (pngs / "manifest.json").write_text("[]\n")
        self.assertEqual(self.run_launcher(), ["doom:"])

    def test_loader_failure_falls_back(self):
        wad = self.select_wad()
        self.assertEqual(self.run_launcher(doom_test_status="1"),
                         ["doom:" + str(wad), "fallback:-c 000000", "input:key Return"])

    def test_policy_failure_falls_back(self):
        self.select_wad()
        self.assertEqual(self.run_launcher(doom_test_policy_status="1"),
                         ["fallback:-c 000000", "input:key Return"])

    def test_unconfigured_install_falls_back(self):
        self.assertEqual(self.run_launcher(), ["fallback:-c 000000", "input:key Return"])


if __name__ == "__main__":
    unittest.main()
