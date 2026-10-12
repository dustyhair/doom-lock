#!/bin/sh

doom_locker="$HOME/.local/bin/i3lock-doom"
doom_assets="${DOOM_LOCK_ASSETS:-$HOME/.local/share/doom-lock/sprites}"
doom_wad="${DOOM_LOCK_WAD:-}"
if [ -z "$doom_wad" ] && [ -r "$HOME/.config/doom-lock/wad" ]; then
    IFS= read -r doom_wad < "$HOME/.config/doom-lock/wad" || :
fi

if [ -x "$doom_locker" ] && { [ -n "$doom_wad" ] || [ -f "$doom_assets/manifest.json" ]; }; then
    # Keep a private policy until the foreground locker has really exited.
    # Another shortcut/idle launch must never rewrite this session's policy.
    if pam_session=$(mktemp -d "$HOME/.local/share/doom-lock/pam-session.XXXXXX"); then
        if python3 "$HOME/.local/share/doom-lock/prepare-pam.py" "$pam_session/policy" >/dev/null; then
            DOOM_LOCK_WAD="$doom_wad" DOOM_LOCK_ASSETS="$doom_assets" DOOM_LOCK_PAM_DIR="$pam_session/policy" "$doom_locker" -n -c 080808 &
            doom_pid=$!
            # The child now owns the inherited xss-lock inhibitor and closes
            # it on MapNotify. Our waiting shell must release its own copy.
            case "${XSS_SLEEP_LOCK_FD:-}" in
                ''|*[!0-9]*) ;;
                *)
                    if [ "$XSS_SLEEP_LOCK_FD" -ge 3 ] 2>/dev/null; then
                        eval "exec ${XSS_SLEEP_LOCK_FD}>&-"
                    fi
                    ;;
            esac
            if wait "$doom_pid"; then
                rm -rf -- "$pam_session"
                exit 0
            fi
        fi
        rm -rf -- "$pam_session"
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
