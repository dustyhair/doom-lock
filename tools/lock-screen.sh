#!/bin/sh

doom_locker="$HOME/.local/bin/i3lock-doom"
doom_assets="$HOME/.local/share/doom-lock/sprites"

if [ -x "$doom_locker" ] && [ -f "$doom_assets/manifest.json" ]; then
    if python3 "$HOME/.local/share/doom-lock/prepare-pam.py" "$HOME/.local/share/doom-lock/pam" >/dev/null; then
        if DOOM_LOCK_ASSETS="$doom_assets" DOOM_LOCK_PAM_DIR="$HOME/.local/share/doom-lock/pam" "$doom_locker" -c 080808; then
            exit 0
        fi
    fi
fi

# i3lock forks only after its window is visible and the input grab is active.
if ! command -v i3lock >/dev/null 2>&1; then
    exec xflock4
fi

i3lock -c 000000 || exit $?

# The original locker needs Enter to start fingerprint verification.
if command -v xdotool >/dev/null 2>&1; then
    xdotool key Return
fi
