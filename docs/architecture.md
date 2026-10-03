# Architecture

Doom lock keeps i3lock's X11 lifecycle and keyboard handling. The additions are
small C modules with plain functions. One process owns one scene. There is no
plugin system or separate graphics engine.

| Module | Responsibility |
| --- | --- |
| `i3lock.c` | Input buffer, verification requests, retry timers, lock lifecycle, and authentication results |
| `auth.c` | Linux PAM handles, locked password snapshots, worker threads, and conversation messages |
| `unlock_indicator.c` | X11 frame composition and the adapter that passes UI text into the scene |
| `doom.c` | Monster animation, hits, BFG targeting, HUD layout, and success/failure transitions |
| `hud.c` | Optional bitmap-font ownership, tinted glyphs, text fitting, and framed panel drawing |
| `level.c` | Generated maze, camera route, CPU raycasting, and monster projection/occlusion |
| `assets.c` | Bounded WAD decoding and external PNG loading |
| `melt.c` | Captured frame strips, melt timing, and authenticated desktop reveal through X11 SHAPE |
| `xcb.c`, `randr.c`, `dpi.c` | Upstream X11 helpers, monitor layout, and scale handling |
| `include/monsters.def` | Shared monster names, WAD prefixes, animation sequences, and hovering flags |
| `tools/` | Build, content extraction, local installation, policy validation, recording, and benchmarking |
| `tests/` | Generated artwork, native sanitizer checks, and isolated X11/PAM integration tests |
| `ci/` | Reproducible build dependencies and the asset-free check command |

## Authentication and threads

The event-loop thread owns input and UI state. Only `auth.c` calls PAM.
Password and fingerprint channels have separate handles and one worker each.
`auth_submit` copies a password into the worker's snapshot before creating its
thread. The worker erases that snapshot before reporting completion through
`ev_async`. The completion callback joins the worker before reading its result.

A mutex protects fingerprint notices. The worker posts text; the event-loop
callback copies it and redraws. Workers do not access Cairo, X11, the typed input
buffer, or animation state. A fingerprint conversation rejects hidden password
prompts rather than attempting a blank password fallback.

No worker starts before i3lock's daemon fork. The child restores memory locks
for the input buffer and both snapshots because Linux drops them across fork.
A successful result stops scan retries and clears input. A failed scan preserves
typed input; a failed password triggers the death screen. The locker retains its
keyboard and pointer grabs throughout the success animation and melt.

The policy helper removes only the validated optional fingerprint alternative
from a copy of `common-auth`. It preserves the remaining relative jump targets
and all other service files. Authentication factors outside that validated block
remain part of the service. Broadening supported PAM layouts needs a separate
policy review and negative tests.

## Content and rendering

Startup loads all required images before opening the lock window or grabbing
input. `assets_image` returns an owned Cairo reference. `doom.c`, `level.c`, and
`hud.c` retain their references, then `assets_close` releases the WAD bytes and
loader cache. No content file reads occur during animation. Scene cleanup handles
partial loading failures as well as normal exit.

The catalogue in `include/monsters.def` is simple comma-separated `MONSTER` rows.
C includes it with a local macro, and the optional Python extractor reads its
fields with the standard CSV parser. Changing a monster's frames in one place
updates both content paths and the renderer.

The maze renders at 416 pixels wide and scales with nearest-neighbor filtering.
Its camera timer runs at 30 FPS. A separate 0.14-second timer advances sprite
animation. Camera motion uses elapsed time, while wall-first rendering avoids
sampling hidden floor and ceiling pixels. The renderer caches the maze frame
until the camera or viewport changes.

The optional HUD font uses `STCFN033` through `STCFN095`; the panel uses the
`GRNROCK` flat. `hud.c` prepares gold and green glyph variants once at startup,
then draws glyphs with nearest-neighbor filtering and bounded rectangles.
Missing UI graphics keep the system-font and plain-frame fallback.

`doom_draw` receives borrowed UI text, a UTF-8 character count, and a checking flag
through `doom_ui_t`. Password bytes stay in `i3lock.c`; its count accessor derives
the mask length from the current buffer so edits and clearing stay consistent.
`hud_password` draws original pixel runes chosen by a per-lock visual seed and
character index, independently of password values. Long masks show a bounded,
scrolling tail, and the cursor follows its fitted width. The runes also work
with the system-font fallback, without extra artwork.
Success and failure enter the scene through explicit
functions. `melt.c` only changes the window's bounding shape after authentication
has succeeded. Failure melts keep the full window opaque.

## Making changes

Keep native code at the root and public interfaces in `include/`, matching the
upstream layout. Add a module only when it has a clear owner and lifecycle.
Keep test-only authentication out of production launchers. Generated fixtures,
recordings, binaries, and downloaded headers belong under ignored `build/` or
`.build-deps/`. WADs and extracted content stay outside Git.

Run `tests/test-auth.py` for PAM conversation or worker changes,
`tests/test-pam.py` for policy changes, and `tests/test-lock.py` for authentication
UI or transition changes. Run `tests/test-assets.py` for decoder changes and
`tests/test-level.py` for maze or projection changes. The detailed commands and
optional real-WAD parity checks are in [DOOM-LOCK.md](../DOOM-LOCK.md).
