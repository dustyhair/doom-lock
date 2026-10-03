#!/usr/bin/env python3
"""Audit tracked files and reachable history for game files, artifacts, and tokens."""
import re
import subprocess
from pathlib import PurePosixPath

FORBIDDEN_SUFFIXES = {".wad", ".pk3", ".png", ".jpg", ".jpeg", ".mp4", ".so", ".o",
                      ".deb", ".exe", ".tar", ".zip", ".pem", ".key"}
FORBIDDEN_ROOTS = {"assets", "build", ".build-deps"}
TOKENS = re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|"
                   rb"AKIA[A-Z0-9]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)")


def git(*arguments):
    return subprocess.check_output(["git", *arguments])


def check_name(name):
    path = PurePosixPath(name)
    if (path.parts[0] in FORBIDDEN_ROOTS or path.suffix.lower() in FORBIDDEN_SUFFIXES
            or path.name == ".env"):
        raise RuntimeError(f"Excluded content is tracked: {name}")
    if path.suffix.lower() == ".gif" and path.parent != PurePosixPath("docs/demo"):
        raise RuntimeError(f"GIF outside the documented demo folder: {name}")


def check_blob(name, content):
    if name.lower().endswith(".gif") and content.startswith((b"GIF87a", b"GIF89a")):
        return
    if b"\0" in content:
        raise RuntimeError(f"Unexpected binary content: {name}")
    if TOKENS.search(content):
        # Report location only; never print matched credentials.
        raise RuntimeError(f"Possible credential or private key: {name}")


def main():
    tracked = git("ls-files", "-z").decode().split("\0")[:-1]
    for name in tracked:
        check_name(name)
        with open(name, "rb") as source:
            check_blob(name, source.read())
    objects = git("rev-list", "--objects", "HEAD").decode().splitlines()
    blob_count = 0
    process = subprocess.Popen(["git", "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for entry in objects:
            identifier, separator, name = entry.partition(" ")
            if not separator or not name:
                continue
            process.stdin.write(identifier.encode() + b"\n")
            process.stdin.flush()
            metadata = process.stdout.readline().decode().split()
            size = int(metadata[2])
            content = process.stdout.read(size)
            assert process.stdout.read(1) == b"\n"
            if metadata[1] != "blob":
                continue
            check_name(name)
            check_blob(name, content)
            blob_count += 1
    finally:
        process.stdin.close()
        process.wait()
    print(f"PASS: {len(tracked)} tracked files and {blob_count} historical blobs; no raw game content, build artifacts, or token matches")


if __name__ == "__main__":
    main()
