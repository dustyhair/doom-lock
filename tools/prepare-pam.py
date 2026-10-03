#!/usr/bin/env python3
"""Copy the system policy for a password check that skips the initial scan."""
import re
import sys
from pathlib import Path


def password_policy(common):
    """Accept the pam-auth-update fingerprint/password alternative block only."""
    lines = common.splitlines(keepends=True)
    rules = []
    for index, line in enumerate(lines):
        content = line.split("#", 1)[0].strip()
        if not content:
            continue
        match = re.fullmatch(r"auth\s+(\[[^\]]+\]|\w+)\s+(\S+)(?:\s+.*)?", content)
        if not match or "\\" in content:
            raise RuntimeError("Unsupported common-auth syntax")
        control, module = match.groups()
        rules.append((index, control, module))

    # Require password alternatives followed by the generated deny/permit pair.
    # A required/requisite fingerprint is never safe to remove for this fork.
    modules = [rule[2] for rule in rules]
    if len(rules) < 4 or modules[0] != "pam_fprintd.so" or modules.count("pam_fprintd.so") != 1:
        raise RuntimeError("Expected one initial optional fingerprint alternative")
    try:
        deny = modules.index("pam_deny.so")
    except ValueError as error:
        raise RuntimeError("Missing password failure rule") from error
    if (deny < 2 or deny + 1 >= len(rules)
            or rules[deny][1:] != ("requisite", "pam_deny.so")
            or rules[deny + 1][1:] != ("required", "pam_permit.so")):
        raise RuntimeError("Unsupported password fallback rules")
    for position, (_, control, module) in enumerate(rules[:deny]):
        expected = {"success": str(deny - position), "default": "ignore"}
        entries = control[1:-1].split() if control.startswith("[") else []
        if (len(entries) != 2 or set(entries) != {f"{key}={value}" for key, value in expected.items()}
                or (position > 0 and module not in {"pam_unix.so", "pam_sss.so"})):
            raise RuntimeError("Unsupported fingerprint/password control rules")
    if any(rule[1:] != ("optional", "pam_cap.so") for rule in rules[deny + 2:]):
        raise RuntimeError("Unsupported additional authentication rules")
    # Every remaining relative jump retains its original destination.
    del lines[rules[0][0]]
    return "".join(lines)


def prepare(destination, source=Path("/etc/pam.d")):
    # Read and validate everything before touching an existing policy copy.
    configs = {config.name: config.read_bytes() for config in source.iterdir() if config.is_file()}
    filtered = password_policy(configs["common-auth"].decode())
    password_service = configs["i3lock"]
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in configs.items():
        (destination / name).write_bytes(data)
    (destination / "common-auth").write_text(filtered)
    (destination / "i3lock-password").write_bytes(password_service)
    print(f"Prepared password policy in {destination}")


if __name__ == "__main__":
    prepare(Path(sys.argv[1]))
