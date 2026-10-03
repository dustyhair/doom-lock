#!/usr/bin/env python3
"""Check supported PAM alternatives and rejection before destination writes."""
import importlib.util
import tempfile
import unittest
from pathlib import Path


spec = importlib.util.spec_from_file_location(
    "prepare_pam", Path(__file__).resolve().parents[1] / "tools/prepare-pam.py")
pam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pam)

FINGERPRINT = "auth [success=3 default=ignore] pam_fprintd.so max-tries=1 timeout=10 # debug\n"
PASSWORD = ("auth [success=2 default=ignore] pam_unix.so nullok try_first_pass\n"
            "auth [success=1 default=ignore] pam_sss.so use_first_pass\n"
            "auth requisite pam_deny.so\n"
            "auth required pam_permit.so\n"
            "auth optional pam_cap.so\n")


class PolicyTests(unittest.TestCase):
    def test_supported_alternatives_preserve_password_rules(self):
        common = "# generated policy\n" + FINGERPRINT + PASSWORD
        self.assertEqual(pam.password_policy(common), "# generated policy\n" + PASSWORD)
        # pam-auth-update also generates a block with only the Unix provider.
        unix_only = ("auth [success=2 default=ignore] pam_fprintd.so\n"
                     "auth [default=ignore success=1] pam_unix.so\n"
                     "auth requisite pam_deny.so\n"
                     "auth required pam_permit.so")
        self.assertEqual(pam.password_policy(unix_only), unix_only.split("\n", 1)[1])

    def test_prepare_retains_service_and_nested_policy_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, destination = Path(temporary) / "source", Path(temporary) / "copy"
            source.mkdir()
            (source / "common-auth").write_text(FINGERPRINT + PASSWORD)
            (source / "i3lock").write_text("auth include login\n")
            (source / "login").write_text("auth optional pam_faildelay.so delay=3000000\n@include common-auth\n")
            pam.prepare(destination, source)
            self.assertEqual((destination / "common-auth").read_text(), PASSWORD)
            self.assertEqual((destination / "i3lock-password").read_bytes(), (source / "i3lock").read_bytes())
            self.assertEqual((destination / "login").read_bytes(), (source / "login").read_bytes())
            self.assertEqual((source / "common-auth").read_text(), FINGERPRINT + PASSWORD)

    def test_unsupported_policies_never_change_existing_copy(self):
        policies = {
            "required fingerprint": "auth required pam_fprintd.so\n" + PASSWORD,
            "requisite fingerprint": "auth requisite pam_fprintd.so\n" + PASSWORD,
            "comment mentions fingerprint": "auth requisite pam_deny.so # pam_fprintd.so\nauth required pam_permit.so\n",
            "fingerprint failure is required": FINGERPRINT.replace("default=ignore", "default=bad") + PASSWORD,
            "wrong success destination": FINGERPRINT.replace("success=3", "success=2") + PASSWORD,
            "second fingerprint": FINGERPRINT + PASSWORD + "auth optional pam_fprintd.so\n",
            "password required for another factor": FINGERPRINT + PASSWORD + "auth required pam_google_authenticator.so\n",
            "nested auth include": FINGERPRINT + "auth include another-policy\n" + PASSWORD,
            "missing denial": FINGERPRINT + PASSWORD.replace("auth requisite pam_deny.so\n", ""),
        }
        for label, common in policies.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                source, destination = Path(temporary) / "source", Path(temporary) / "copy"
                source.mkdir()
                destination.mkdir()
                (source / "common-auth").write_text(common)
                (source / "i3lock").write_text("auth include login\n")
                (destination / "common-auth").write_text("previous verified policy\n")
                (destination / "i3lock-password").write_text("previous service\n")
                with self.assertRaises(RuntimeError):
                    pam.prepare(destination, source)
                self.assertEqual({path.name: path.read_text() for path in destination.iterdir()}, {
                    "common-auth": "previous verified policy\n", "i3lock-password": "previous service\n"})


if __name__ == "__main__":
    unittest.main()
