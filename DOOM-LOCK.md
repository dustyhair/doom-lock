# Doom lock screen

This local fork of i3lock 2.16 adds a moving Doom maze and 14 Doom II monster
animations to the existing i3 lock menu. Content can come directly from a
user-supplied Doom II-compatible WAD or from an extracted PNG folder. WADs and
extracted game assets are ignored by Git; the executable contains no artwork.

The WAD loader reads the palette, sprite patches, patch origins, wall texture
definitions, and floor/ceiling flats into memory before the lock window opens.
It produces the same cropped frames as PNG extraction, then releases the WAD
buffer. There is no extraction cache or WAD I/O during animation. Both IWADs and
complete standalone PWADs are accepted. The file must contain all required Doom
II monster, BFG, face, and level texture resources; Doom I WADs and partial mod
PWADs are not supported. The maze geometry is still generated.

The camera backs through a generated maze of corridors and rooms at roughly
one tile every three seconds, with smooth turns. It faces opposite its direction
of travel. The monster follows the route while the camera retreats.
Each lock chooses one consistent style using wall, floor, ceiling, and lighting
combinations found together in Doom II's actual sectors and sidedefs:

| Map | Main wall | Floor | Ceiling | Sector light |
| --- | --- | --- | --- | --- |
| MAP01 | TEKGREN2 | FLOOR3_3 | GRNLITE1 | 160 |
| MAP02 | STONE4 | FLAT5_4 | FLAT5_4 | 144 |
| MAP05 | BIGBRIK1 | FLAT1 | FLAT10 | 144 |
| MAP14 | BSTONE1 | FLOOR5_4 | FLAT1_2 | 144 |
| MAP24 | SKIN2 | FLOOR7_1 | CEIL5_1 | 160 |

Matching detail textures mark the middle of room walls. The original texture
colors receive only neutral distance shading. MAP24's style includes an animated
slime basin confined to one rock-lined room with a dry border. The maze geometry
is generated, rather than copied from those maps.
A small CPU raycaster draws the scene at 416 pixels wide and scales it with
nearest-neighbor filtering. The camera redraws at 30 FPS, while monster sprite
frames keep their original slower pace. Movement uses elapsed time, preserving
the slow walk speed. Walls are rendered first so hidden floor and ceiling
pixels need no texture sampling or shading. It runs only within the lock screen
and does not change desktop idle settings. Camera movement pauses during
authentication success and the failure screen so the BFG and screen melts keep
a stable scene.

Monsters use the same perspective projection as the maze. Maze cells and ceiling
height represent 128 Doom units, floor textures repeat every 64 units, and the
camera stands 41 units above the floor. Sprite pixels retain their original world
size, so the Cyberdemon is naturally larger than a zombieman. Extracted patch
origins anchor movement, pain, and death frames to the same floor position.
The monster follows the camera's route at a distance of 2.4 tiles. Its position
and scale change through turns, and walls hide it where the corridor bends.
Flying monsters hover and settle to the floor during death. Before the final
kill, a monster hidden by a corner finishes coming around that corner, and the
view aims at it if needed. The BFG uses that projected body position as its target.

One of 14 monsters is randomly selected for each lock. It walks while waiting;
flying monsters float using their original movement sprites. Each password
character triggers the monster's original pain frame and a brief hit flash.
Password success plays its death animation. Fingerprint success fires an original
BFG projectile that flies into the living monster's body, explodes with a fading
green flash, and sends it through a fast death animation. The flash and blast
clear before a brief pause and the screen melt. The screen stays
locked until authentication succeeds and the final animation finishes.
After the monster dies, a Doom-style column melt uncovers the live desktop
behind the lock window. The strips start at staggered times, accelerate, and
fall away over about 1.2 seconds. Keyboard and pointer grabs remain active until
the melt finishes. This uses X11 SHAPE and works without a compositor. If SHAPE
is unavailable, it melts to black before unlocking.
The lock window requests no Picom/Compton shadow, so a rectangular shadow cannot
dim the desktop exposed by the falling strips.
A wrong password melts the current scene into a persistent red YOU DIED screen
with Doom's dead player face. The window stays fully opaque during this failure
transition. Typing cancels the failure melt and starts another attempt with a
new monster. Successful fingerprint verification can interrupt a failure melt.
Fingerprint verification starts immediately once the window is mapped and the
daemon has forked, even if typing has already started. Reader instructions and
scan errors appear below the password status, including which enrolled finger
to use. A failed scan automatically starts another attempt one second after PAM
returns, while preserving typed passwords and the death screen. Only one scan
can run at a time, and retries stop as soon as authentication succeeds.
Enter with a typed password starts a separate password check immediately, while
the fingerprint scan continues independently. Enter with no password starts a
fingerprint check if one is not already running.

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
No system PAM file is edited. A background fingerprint worker refuses hidden
password prompts from the system stack. It cannot fall back to blank Unix/SSS
password attempts or use the password being typed for the separate worker.

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
- `~/.config/doom-lock/wad`, the path to a WAD in its original location
- `~/.local/share/doom-lock/sprites`, optional extracted sprites and textures
- `~/.local/share/doom-lock/pam`, the refreshed policy for password verification
- `~/.local/share/doom-lock/prepare-pam.py`, the policy preparation helper
- `~/.local/share/doom-lock/lock-screen.sh.before-doom-*`, the original launcher backup

Source is in `~/Development/side_projects/doom-lock`. The custom locker and its
user-local header build target Linux. No system executable or PAM file is replaced.
If the custom binary or content cannot load, the launcher uses the original
system i3lock and its fingerprint startup shortcut.

## Rebuild and install

The local build targets Debian/Ubuntu Linux. It requires Python 3, a C compiler,
`apt`, `dpkg-deb`, and the installed runtime libraries for PAM, Cairo, libev,
XCB including SHAPE, and xkbcommon. Direct WAD loading needs no Pillow or game
assets at build time. Optional PNG extraction and asset tests need Pillow,
available as `python3-pil`.
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
python3 tools/build-local.py
python3 tests/test-pam.py
python3 tests/test-assets.py
python3 tools/install-local.py --wad /path/to/DOOM2.WAD
```

`--wad` saves an absolute path in `~/.config/doom-lock/wad`. It copies neither
the WAD nor extracted artwork. Your installed game can stay in its original
directory. `DOOM_LOCK_WAD=/path/to/DOOM2.WAD ~/.local/bin/lock-screen.sh`
overrides the saved path for one lock. WAD content takes precedence over
`DOOM_LOCK_ASSETS`. Unsupported or malformed content fails before input grabs;
the launcher then uses system i3lock.

To keep using extracted PNGs:

```sh
python3 tools/extract-sprites.py /path/to/DOOM2.WAD assets
python3 tests/test-level.py
python3 tests/test-lock.py
python3 tools/install-local.py --assets assets
```

The explicit `--assets` installation removes a saved WAD selection. Existing
local PNGs remain on disk when switching to WAD mode, but are not loaded.
`--without-assets` installs only the binary, launcher, and PAM helper/policy,
without changing an existing content selection. Use that option for a clean
installation whose user will configure their own WAD later.

WAD loading is enabled by default. `python3 tools/build-local.py --without-wad`
or Meson's `-Dwad_assets=false` compiles out the WAD parser while retaining PNG
support. Both builds use external content; neither embeds artwork. A source
archive created with `git archive` includes tracked code and excludes the local
ignored WADs, extracted assets, and build directory. Include `LICENSE` with
code distributions and let each user supply their own content.

The native asset tests generate synthetic artwork, check mirrored rotations,
palette and origin handling, duplicate-lump precedence, and reject malformed
directory, patch, flat, and texture data under ASan/UBSan. To compare every
decoded image against your own extraction, use:

```sh
python3 tests/test-assets.py --wad /path/to/DOOM2.WAD --assets assets
DOOM_TEST_WAD=/path/to/DOOM2.WAD python3 tests/test-level.py
DOOM_TEST_WAD=/path/to/DOOM2.WAD python3 tests/test-lock.py
python3 tests/test-install.py
python3 tests/test-launcher.py
```

The WAD lock tests deliberately point the PNG source at a nonexistent folder.
The installation tests use an isolated destination and verify that WAD mode
stores only the path and installs no artwork.

The PAM policy tests check supported alternatives and reject unsafe layouts
before destination files change. The animation and input tests use their own
Xvfb display and a test-only PAM stub. They cover immediate password verification
during a ten-second scan, Backspace, Ctrl+U, queued input, incorrect passwords,
fingerprint success and timeout, BFG flash, death screen and retry, Escape,
walking and hit animations, multi-message PAM conversations, repeated restarts
after fingerprint timeouts, queued edits after the failure screen appears, and
the daemon fork used by the installed launcher. They also inspect the lock's
bounding shape and input grabs, change the desktop during a success melt to
check the live reveal, test fingerprint success during a failure transition,
and check that the colorful maze moves independently of the monster animation.
They do not verify the physical sensor or the user's actual credentials.
The scan tests also check automatic rearming with partial password input,
success on a later automatic scan, no password fallback from the fingerprint
worker, and retries while the persistent failure screen is visible.

The maze tests compile the renderer with AddressSanitizer and
UndefinedBehaviorSanitizer. All five map styles each run 23 simulated minutes
through rooms, corners, dead ends, and shortcuts with changing viewport dimensions.
Every step checks that the camera stays out of walls, moves at walking speed,
and travels backward along its viewing axis. The tests also check monster path
placement, wall clipping, and a visible target for the final kill. All 14 monsters
and their death frames render in each style. Reference level and monster images
are written to `build/test-results/level-*.png` and `build/test-results/monster-*.png`.

If Picom is installed, run `DOOM_TEST_PICOM=1 python3 tests/test-lock.py` to repeat
the checks with compositor shadows and fading enabled on the isolated display.

`python3 tools/benchmark-level.py` measures CPU maze rendering on fixed tours
in all five map styles, excluding sprite drawing and X11 uploads. Add
`--compare b2dfd8b` to compare with the version before the 30 FPS optimization.
It reports per-frame times and final-frame checksums, and saves the results in
`build/performance/maze-benchmark/results.json`. This benchmark needs no desktop
access and does not lock the screen.

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
- https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/f_wipe.c
- https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/r_data.c
- https://www.x.org/releases/X11R7.7/doc/xextproto/shape.html
