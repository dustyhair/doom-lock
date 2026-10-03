Doom lock screen
================

This local fork of i3lock adds Doom II monster animations, hits while typing,
a death screen for incorrect passwords, and a BFG kill for fingerprint unlock.
The camera slowly backs through a generated maze, using matching wall, floor,
and ceiling textures from actual Doom II levels. Monsters follow the route at
their original relative sizes, with perspective scaling and wall clipping.
Successful unlock melts the
scene away to reveal the live desktop. Password and fingerprint verification
run independently.

Load content directly from your own Doom II-compatible WAD with
`python3 tools/install-local.py --wad /path/to/DOOM2.WAD`, or use extracted PNGs.
The WAD is decoded in memory when locking. No game artwork is embedded in the
binary or included in the source repository. WAD support can be disabled with
`-Dwad_assets=false` in Meson or `--without-wad` in the local build script.

See [DOOM-LOCK.md](DOOM-LOCK.md) for the local build, installation, and restore
instructions, including dependency setup for a fresh checkout. Meson builds this
fork as `i3lock-doom` and never installs system i3lock PAM files.

The original i3lock documentation follows as an upstream reference. Use
DOOM-LOCK.md to build and run this fork.

i3lock - improved screen locker
===============================
[i3lock](https://i3wm.org/i3lock/) is a simple screen locker like slock.
After starting it, you will see a white screen (you can configure the
color/an image). You can return to your screen by entering your password.

Many little improvements have been made to i3lock over time:

- i3lock forks, so you can combine it with an alias to suspend to RAM
  (run "i3lock && echo mem > /sys/power/state" to get a locked screen
   after waking up your computer from suspend to RAM)

- You can specify either a background color or a PNG image which will be
  displayed while your screen is locked. Note that i3lock is not an image
manipulation software. If you need to resize the image to fill the screen
or similar, use existing tooling to do this before passing it to i3lock.

- You can specify whether i3lock should bell upon a wrong password.

- i3lock uses PAM and therefore is compatible with LDAP etc.
  On OpenBSD i3lock uses the bsd_auth(3) framework.

Install
-------

See [the i3lock home page](https://i3wm.org/i3lock/).

Requirements
------------
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libxcb-xinerama
- libxcb-randr
- libev
- libx11-dev
- libx11-xcb-dev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0
- libxcb-image
- libxcb-xrm

Running i3lock
-------------

To test i3lock, you can directly run the `i3lock` command. To get out of it,
enter your password and press enter.

For a more permanent setup, we strongly recommend using `xss-lock` so that the
screen is locked *before* your laptop suspends:

```
xss-lock --transfer-sleep-lock -- i3lock --nofork
```

On OpenBSD the `i3lock` binary needs to be setgid `auth` to call the
authentication helpers, e.g. `/usr/libexec/auth/login_passwd`.

Building i3lock
---------------
We recommend you use the provided package from your distribution. Do not build
i3lock unless you have a reason to do so.

First install the dependencies listed in requirements section, then run these
commands (might need to be adapted to your OS):
```
rm -rf build/
mkdir -p build && cd build/

meson setup -Dprefix=/usr
ninja
```

Upstream
--------
Please submit pull requests to https://github.com/i3/i3lock
