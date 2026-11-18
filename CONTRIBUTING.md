# Contributing

Thank you for helping with the Burnout 3 native port. Bug reports, fixes, features, mods, documentation and testing
on hardware we don't have are all welcome. This page says how to build, what a change must pass, the few hard rules,
and how to send your work.

By contributing you agree that your contribution is released under the project's MIT licence (see
[LICENSING.md](LICENSING.md) for what that covers), and that you will follow the [code of conduct](CODE_OF_CONDUCT.md).

## Building

Everything builds from this repository alone: the generated game code (`gen/`) is committed, so you do not need the
decompilation. Machine-specific paths go in a git-ignored `local.env` (copy [`local.env.example`](local.env.example));
without one, the defaults are inside or beside the repository, or your system's packages. The full guide is
[docs/BUILDING.md](docs/BUILDING.md).

To play or to run anything in the game you need **your own copy of the game** (NTSC-U, SLUS-21050): a disc image
(`.iso` / `.bin`) or the disc's files in a folder. Put the folder at `data/game/` (git-ignored) or point `--game` at
it. The unit tests do not need it.

**Linux** (and WSL). Packages: `g++` (GCC 13 or newer, C++20), `make`, `python3`, `pkg-config`, `zlib1g-dev`,
`libvulkan-dev` and SDL 3 (`libsdl3-dev` where your distribution has it; otherwise unpack SDL 3.4 and set
`SDL3_PREFIX` in `local.env`).

```sh
make                 # the game and the launcher: build/native_play, build/libb3render.so, build/b3launcher
make test            # every unit test
build/b3launcher     # or: build/native_play --game <disc folder or .iso>
make modtool         # the mod kit's b3modtool
make render          # the asset viewers and renderer tools (b3view, b3replay, ...)
```

The first build compiles about 5,000 generated files: expect tens of minutes on a laptop. `make CXX="ccache g++"`
makes later rebuilds much faster.

**macOS** (Apple Silicon, or an Intel Mac: [docs/mac.md](docs/mac.md) section 11). Xcode command line tools, then `brew install sdl3 pkg-config make` (the Makefile needs GNU
make 4, installed as `gmake`). `tools/mac/native.sh <disc folder>` builds and starts the game;
`tools/mac/build.sh` and `tools/mac/test.sh` build and run the Mac set of unit tests. See [docs/mac.md](docs/mac.md).

**Windows**. Built from WSL with a pinned MSYS2 MinGW-w64 toolchain whose hashes are checked before every build
(`tools/toolchain/*.sha256`): set `WIN`, `SDL3_MINGW` and `MSYS2_PKGS` in `local.env`, then
`make native && RENDER=1 tools/native/build_win.sh`. See [docs/BUILDING.md](docs/BUILDING.md#windows).

## Checks a change must pass

**Every change: `make test`.** It builds and runs the unit, ABI and PS2-exactness tests and the generator's self-tests,
and must end without a failure. GitHub Actions runs it on Linux for every push and pull request
([.github/workflows/ci.yml](.github/workflows/ci.yml)). Without the game files some tests skip parts of themselves
(they print `skipped`): the libm comparisons against the retail executable (`test_libm`, `test_libm_more`,
`test_libm_cost`, `test_overrides`, parts of `test_native_platform`), the SPU2 reverb check (`test_spu2_mixer`), the
disc-image check (`test_discfs`), the save-stats and real-disc checks (`test_save_stats`, `test_launcher`,
`test_replay_file`, `test_showroom`); and the decomp's own tests are not built without a decomp snapshot. With your disc
in `data/game` (or `GAME` in `local.env`) they run.

**Anything that runs in the game** (the platform layer, the renderer, the game code, settings, mods): the in-game
checks. They play the real game headless from boot, so they need your own disc and a memory card folder with a
profile (`SAVES` in `local.env`):

- **Scenarios**: `STRICT=1 tools/native/race_check.sh build/native_play build/rc_race`, and the other scenarios with
  `SCENARIO=<name>` (the race modes `pause`, `photo`, `music`, `takedown`, `timeattack`, `roadrage`, `crash`,
  `worldtour`; the front-end tours `multiplayer`, `partycrash`, `online`, `ddrewards`, `ddrecords`, `ddsettings`,
  `crashnav`, `carselect`, `crashresults`, `nfsdemo`; details in the script's header). Each run ends with `race_check: PASS ...`. `STRICT=1` halts if any game function
  would run as the original PS2 code.
- **Diagnostic builds with the fallback**: the default build, like every release, has no way to run original PS2 code:
  a call to a function with no native version stops the game (`native_play: STOPPED: the game called <address>
  <name>, ...`; `fallback: none (compiled out)` at the end of the log). To investigate one, build with the EE (R5900)
  fallback compiled in: `make NATIVE_FALLBACK=1 build/native_play` (Windows: `NATIVE_FALLBACK=1
  tools/native/build_win.sh`). That build runs such a function as the original code and lists it at the end of the
  run (`fallback: N functions ran retail in the mini ...`), and it is the one the exploration options need
  (`--explore-retail` / `EXPLORE_RETAIL=`, `--explore-movies`, `--explore-return`) and the per-call check
  `B3NATIVE_DIFF`. Never ship it; a plain `make` switches back (the setting is stamped, so native_play relinks).
- **Golden compare**: record the build before your change and the build with it, then compare them item by item
  (time trace, DMA / SIF / audio logs, guest RAM, screenshots):
  ```sh
  tools/golden/baseline.sh <native_play before> build/golden-old
  tools/golden/baseline.sh <native_play after>  build/golden-new
  tools/golden/compare.sh build/golden-old build/golden-new     # last line: compare: PASS
  ```
  A refactor or a new feature at its default setting must give identical results.
- **Saves**: `tools/native/save_check.sh` when you touch the memory card or the save files.

If you don't have the disc or the time for these, say so in the pull request: a maintainer runs them before merging.
Never paste their outputs' game-derived files (screenshots, captures, RAM dumps) into an issue or pull request; the
verdict lines and hashes are enough.

## Hard rules

- **No game data, ever.** No disc images, game files, executables, assets, extracted textures or sounds, captures,
  RAM dumps, BIOS or SDKs in the repository, in issues or in pull requests. Mods follow the same rule: they ship
  patches and their own work, never the game's files.
- **Defaults never change gameplay.** Remaster features never change physics, AI or gameplay by default; a feature
  that would is opt-in, and with every feature at its default the in-game checks above must stay identical.
- **Replays stay deterministic.** The simulation is deterministic, and replay files, ghosts and the checks depend on
  it: no host time, randomness, thread timing or uninitialised memory may reach the game's state. A new input path
  goes through the pad recording, so a replay reproduces it.
- **The decomp is the source of the game code.** `gen/` is generated by `tools/decomp2host` from a committed decomp
  snapshot; don't edit it by hand. Fixes to the game code itself belong in the decompilation project.
- Verify any binary you download (toolchains, libraries) against a published hash before running it.

## Commits and pull requests

- Work on a branch of your own and open a pull request against `main`. Keep a pull request to one topic.
- Commit messages: a short summary line that names the area first, then a blank line and what changed and why, for
  example `Launcher: the Replays page sorts by date`. Mention the issue it fixes, if any.
- Only commit your own changes (`git commit -- <paths>`); keep build outputs and local files out (`build/`,
  `local.env`).
- Follow the code around you: C++20, the existing naming and comment style, comments that say why. New behaviour gets
  a unit test where it can have one.
- The pull request template asks what changed and which checks you ran, with their last lines of output.

For contributors: the maintainer's checkout uses the hooks in `.githooks/` (`git config core.hooksPath .githooks`): a
`pre-push` that only lets pushes go to the project's own GitHub remotes, and a `commit-msg` that drops a co-author
trailer. They are optional; you do not need to install them to work in a fork, and the pre-push hook would refuse
your fork's remote if you did.

## Documentation

All documentation is in [`docs/`](docs/): building ([BUILDING.md](docs/BUILDING.md)), the PC settings
([pc-settings.md](docs/pc-settings.md)), the remaster features ([remaster-features.md](docs/remaster-features.md)),
modding ([docs/modding/](docs/modding/README.md)), the platform specifications ([docs/specs/](docs/specs/)) and
design notes. [docs/known-issues.md](docs/known-issues.md) lists the known problems; open work is on the
[issue tracker](https://github.com/u0rhs/burnout3-native/issues). A change that alters behaviour updates the page
that describes it.

Comments and docs often carry a short tag such as `(mac5)`, `textsize1` or `modkit1`: the name of the piece of work
that wrote or last changed that code, so a change can be traced to its design notes and checks. They are labels only;
new code does not need one.

## Reporting bugs

Open an issue with the bug report template. The most useful thing you can attach is the **bug report zip**: in the
game press **F10**, or use **Make a bug report** on the launcher's Play page. It holds the logs and the **pad
recordings** of your last runs (a recording replays exactly what happened, from the boot), your settings, the
version and a short system description; no game data, and your save only if you choose to include it. If you
recorded the session as a **replay file** (`.b3replay`, PC SETTINGS > INTERFACE > REPLAYS), attach that too.

Please include the version line (in the launcher's window title and on its Play page), your operating system and graphics
card, what you did and what happened. Logs can contain folder paths with your user name; edit them out if you prefer.

## Modding

The mod kit and its documentation are in [docs/modding/](docs/modding/README.md): the mod format, `b3modtool`, the
file-format reference and tutorials. Contributions there are welcome: corrections to the file-format pages, new
tutorials, `b3modtool` commands and tests. Example mods in [`examples/mods/`](examples/mods/) must contain only your
own work (the hello-texture example shows how a mod makes its file from the player's own disc). The mod SDK headers
and templates are MIT, so mods built with them may use any licence.

Questions about making a mod: open an issue with the mod / SDK question template.
