# Contributing

Please report Doom lock issues in this repository. Include your Linux
distribution, X11 desktop, compiler, build command, and whether you use a WAD or
extracted PNGs. Describe what you expected and what happened. For authentication
issues, say whether password entry, fingerprint scanning, or both are affected.
Do not attach passwords, WADs, or extracted game artwork.

Keep changes focused. The [architecture](../docs/architecture.md) describes
module ownership, threading rules, and resource lifetimes. Preserve upstream
license notices and keep all game content external.

Use the checked-in `.clang-format` for C code. Run the focused tests for your
change and the container checks before submitting a pull request. Generated test
artwork is enough for CI. Authentication changes need failure cases as well as
success cases; a test must never use the contributor's real desktop or password.
