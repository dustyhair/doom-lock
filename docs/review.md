# Publication review

This review covered authentication and input handling, the WAD/PNG loader,
scene and melt lifetimes, maze projection, local installation, build settings,
and the files and history intended for publication. It combined source review,
GCC's static analyzer, native sanitizers, and isolated runtime tests.

## Fixes

| Finding | Resolution |
| --- | --- |
| Linux drops password memory locks across the daemon fork | Re-lock the input buffer and both worker snapshots in the child before verification |
| An early Enter could start a thread before daemonization | Queue verification until the daemon fork completes |
| PAM conversation allocation failures could leave responses allocated | Erase and free every response on all error paths; reject invalid message counts and styles |
| A long Compose expansion could exceed the fixed UTF-8 input buffer | Check both the source length and destination capacity before copying |
| PAM, X11, and rendering state were coupled through implementation details | Move PAM handles, workers, and conversations into `auth.c`; pass display text into the scene through `doom_ui_t` |
| Monster metadata was repeated across loader, renderer, and extractor | Use one catalogue with compile-time frame-capacity checks |
| Partial scene loading left retained image references until process exit | Give the scene and level explicit cleanup functions used on load failure and exit |
| Default PNG installation could retain an older WAD selection | Clear the saved WAD path whenever PNGs are selected |
| A missing install binary was discovered after installation had started | Validate the executable during argument parsing; support an explicit Meson binary path |
| Three event watchers used unchecked heap allocations | Give them stack storage for the lifetime of the event loop |
| Repeated image options and a failed keymap replacement leaked memory | Release the old option strings and unsuccessful keymap |
| Hex colors could accept an invalid suffix | Validate all six digits before copying |
| A missing X11 atom reply could dereference a missing error | Handle both a protocol error and a disconnected server |
| Inherited CI and contribution instructions described upstream i3lock | Replace them with this fork's build, test, and reporting workflow |

C modules use the existing formatter configuration. The project keeps the
upstream directory layout; [architecture.md](architecture.md) records module
ownership and the threading and resource rules for future changes.

## Verification

- GCC static analysis with `-fanalyzer -Werror`.
- GCC and Clang Meson builds on Ubuntu 24.04, with WAD loading on and off.
- PAM conversation cleanup under ASan/UBSan and a separate native post-fork memory-lock check.
- Supported and rejected PAM policies, isolated installer source selection, and launcher fallback tests.
- 269 WAD images, including the optional HUD font and stone texture, and all 14 monster origins compared pixel-for-pixel with local PNG extraction.
- 23 malformed or incomplete WAD cases under ASan/UBSan, duplicate-lump precedence, standalone PWAD loading, and missing optional UI graphics.
- Five maze styles, each with a 23-minute simulated tour, all monster projections, and changing viewport dimensions under ASan/UBSan.
- Isolated X11 authentication tests for password and fingerprint success, retries, queued edits, Compose input, full input buffers, failure opacity, BFG effects, live desktop reveal, input grabs, and daemonization.
- Compositor tests with Picom and private, generated-content checks that require no game WAD.
- Publication audit of tracked files and reachable history for raw artwork, WADs, build artifacts, known token patterns, and private-key headers.

The demo GIFs use simulated authentication and a synthetic desktop. This pass
does not replace a live check of a particular reader or PAM configuration, and
it is not an independent security audit.

## Current scope

Linux X11 with the supported Debian/Ubuntu PAM alternative block is the
installation target. Password-only common-auth layouts, Wayland, Doom I WADs,
and partial mod overlays are outside the current installer/content support.
Additional factors elsewhere in a service stay in that service; the helper does
not rewrite them. Rendering uses the CPU raycaster, with no shader dependency.
