# Doom lock screen

This local fork of i3lock 2.16 adds 14 Doom II monster death animations to the
existing i3 lock menu. Sprite images come from the locally installed DOOM2.WAD.
Extracted game assets are ignored by Git.

One of 14 monsters is randomly selected for each lock. It walks while waiting;
flying monsters float using their original movement sprites. Each password
character triggers the monster's original pain frame and a brief hit flash.
Password success plays its death animation. Fingerprint success fires an original
BFG projectile that flies into the living monster's body, explodes with a green
flash, and sends it through a fast death animation. The screen stays
locked until authentication succeeds and the final animation finishes.
A wrong password shows a persistent red YOU DIED screen with Doom's dead player
face. Typing starts another attempt with a new monster.
Fingerprint verification starts automatically once the window is mapped and the
daemon has forked. Enter with a typed password starts a separate password check
immediately, while the fingerprint scan continues independently. Enter with no
password starts a fingerprint check if one is not already running.

Fingerprint PAM uses `/etc/pam.d/i3lock`, which includes the existing login stack.
For password PAM, the launcher validates the current system policy and copies it
into `~/.local/share/doom-lock/pam` on every lock. It accepts the `pam-auth-update`
alternative block with an initial fingerprint rule, Unix or SSS password rules,
the deny/permit fallback, and optional capability rules. Each provider must use
`[success=N default=ignore]` with its success jump targeting the permit rule.
It removes only that optional fingerprint alternative. Required fingerprint
rules, other authentication layouts, and additional factors cause the launcher
to fall back to the original locker. Validation finishes before the existing
local policy copy changes. The copy preserves password rules, relative jump
destinations, and the system's failure delay, which is three seconds here.
No system PAM file is edited.

The workers have separate PAM handles and memory-locked password snapshots.
The X11 thread renders and accepts input while verification runs. Each snapshot
is erased when its check finishes. Either successful PAM check allows an exit;
failed fingerprint scans cannot override a password result or clear typed input.
The visual effect distinguishes a fingerprint success from a password success
by whether PAM requested a hidden password during that verification. This affects
only the animation, never the authentication result.

Installed files:

- `~/.local/bin/lock-screen.sh`, the launcher used by the Lock menu
- `~/.local/bin/i3lock-doom`, the custom executable
- `~/.local/share/doom-lock/sprites`, the local sprite images
- `~/.local/share/doom-lock/pam`, the refreshed policy for password verification
- `~/.local/share/doom-lock/prepare-pam.py`, the policy preparation helper
- `~/.local/share/doom-lock/lock-screen.sh.before-doom-*`, the original launcher backup

Source is in `~/Development/side_projects/doom-lock`. The custom locker and its
user-local header build target Linux. No system executable or PAM file is replaced.
If the custom binary or sprites cannot start, the launcher uses the original
system i3lock and its fingerprint startup shortcut.

## Rebuild and install

The local build targets Debian/Ubuntu Linux. It requires Python 3, a C compiler,
`apt`, `dpkg-deb`, and the installed runtime libraries for PAM, Cairo, libev,
XCB, and xkbcommon. Sprite extraction needs Pillow, available as `python3-pil`.
The tests also need `Xvfb`, `xdotool`, and ImageMagick's `import` command, available
in the `xvfb`, `xdotool`, and `imagemagick` packages.

Run the dependency bootstrap in a fresh checkout. It downloads development
packages with `apt download` and extracts them into `.build-deps/root`. It uses
your configured package indexes and requires network access. It does not install
system packages or require root. Archives remain in `.build-deps/packages`.
The local build uses these headers and the installed runtime libraries.

```sh
cd ~/Development/side_projects/doom-lock
python3 tools/bootstrap-deps.py
python3 tools/extract-sprites.py \
  "$HOME/Games/Heroic/DOOM + DOOM II/dosdoom/base/doom2/DOOM2.WAD" assets
python3 tools/build-local.py
python3 tests/test-pam.py
python3 tests/test-lock.py
python3 tools/install-local.py
```

The PAM policy tests check supported alternatives and reject unsafe layouts
before destination files change. The animation and input tests use their own
Xvfb display and a test-only PAM stub. They cover immediate password verification
during a ten-second scan, Backspace, Ctrl+U, queued input, incorrect passwords,
fingerprint success and timeout, BFG flash, death screen and retry, Escape,
walking and hit animations, multi-message PAM conversations, repeated restarts
after fingerprint timeouts, queued edits after the failure screen appears, and
the daemon fork used by the installed launcher. They do not verify the physical
sensor or the user's actual credentials.

Meson is also supported when development libraries are installed through your
distribution. `meson setup build/meson` and `meson compile -C build/meson` produce
`build/meson/i3lock-doom`. To use that binary with the local installer, copy it to
`build/i3lock-doom` first. `meson install` installs only `i3lock-doom`; it does not
install an `i3lock` binary, system PAM policy, or the upstream manual. The launcher,
sprites, and password policy still require the local installation above.

To use the original locker again, copy the saved original launcher over
`~/.local/bin/lock-screen.sh`. The installed Doom binary and sprites can stay.

Upstream sources:

- https://github.com/i3/i3lock/tree/2.16
- https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/info.c
