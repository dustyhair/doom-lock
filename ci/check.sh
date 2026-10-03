#!/bin/sh
# Run from the checkout root. No host display, PAM changes, or game files needed.
set -eu
wad="${DOOM_CI_WAD:-true}"
meson setup build/meson --buildtype=release -Dwad_assets="$wad" -Dwerror=true \
    -Dc_args='-Wextra -Wno-unused-parameter -Wno-missing-field-initializers'
meson compile -C build/meson
cp build/meson/config.h build/config.h
cp build/meson/i3lock-doom build/i3lock-doom
python3 tests/test-auth.py
python3 tests/test-pam.py
python3 tests/test-install.py
python3 tests/test-launcher.py
if [ "$wad" = true ]; then
    python3 tests/test-assets.py
    DOOM_TEST_WAD="$PWD/build/test-assets/generated.wad" python3 tests/test-level.py
    DOOM_TEST_WAD="$PWD/build/test-assets/generated.wad" python3 tests/test-lock.py
fi
