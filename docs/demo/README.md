# Demo recordings

These GIFs show the real locker on an isolated 1280x1024 Xvfb display. The desktop
is a solid-color test window; authentication comes from `tests/pam-stub.c`.
Passwords and fingerprint matches are simulated. Game resources remain in a
local, user-supplied WAD and are not copied into this folder.

The videos capture at 30 FPS. The published GIFs use 480-pixel width, 12 FPS,
and a reduced palette. The full recordings, palettes, and timing markers stay
in ignored `build/preview/`.

To regenerate after building, install FFmpeg along with the test dependencies,
then compile the test helpers by running the integration suite and record:

```sh
DOOM_TEST_WAD=/path/to/DOOM2.WAD python3 tests/test-lock.py
python3 tools/record-preview.py --wad /path/to/DOOM2.WAD --gifs
```

The command updates `maze.gif`, `password.gif`, and `fingerprint.gif`. Review
the resulting recordings before committing them. The code license does not
cover the Doom artwork visible in the recordings.
