# Building the port

Everything builds from this repo alone: the generated code (`gen/`) is committed, so the decomp is only needed to
regenerate it. Machine-specific paths go in a git-ignored **`local.env`** (copy `local.env.example`); with none, the
defaults are inside or beside the repo, or your system's packages. Nothing defaults to another person's machine.

## Linux (and WSL)

1. Packages: `g++` (C++20; GCC 13+), `make`, `python3`, `pkg-config`, `zlib1g-dev`, Vulkan headers/loader
   (`libvulkan-dev`), and **SDL 3** (`libsdl3-dev` on distros that ship it; else unpack SDL 3.4.x somewhere and set
   `SDL3_PREFIX` in `local.env` -- `tools/render/README.md` lists the exact Ubuntu packages and hashes used here).
2. Your own Burnout 3 disc (NTSC-U, **SLUS-21050**): copy its files into `data/game/` (git-ignored), or keep the
   `.iso` anywhere -- the game and launcher take either (`--game <folder or .iso>`).
3. Build:
   ```
   make          # the game and the launcher: build/native_play, build/libb3render.so, build/b3launcher
   make test     # every unit test (tests needing a save card or the disc skip without them)
   ```
   `make` runs one compiler per core by default (capped at one per ~2 GB of free memory; `make -jN` chooses). The
   **first build compiles ~5,000 generated files** (the game's code, `gen/native`) and links a ~1.3 GB debug binary:
   expect tens of minutes on a laptop, a few minutes on a big desktop; later builds only redo what changed.
   `make CXX="ccache g++"` makes rebuilds after a branch switch or `make clean` much faster.
4. Run: `build/b3launcher` (choose the disc on its Game Data page), or
   `build/native_play --game data/game` (or `--game /path/to/Burnout3.iso`).

Optional `local.env` entries (see `local.env.example`): `SAVES` (a memory card folder with a profile, used by
`tools/native/race_check.sh` and the save tests), `GAME` (the disc folder for the real-data tests), `DECOMP` (a decomp
snapshot, to regenerate `gen/` with `make native-bundle`).

Checks after a change (each prints PASS / FAIL): `make test`; `tools/native/race_check.sh build/native_play
build/race_check` (needs `SAVES`; `SCENARIO=pause|photo|timeattack|roadrage|...` and `STRICT=1`, see the script's
header); `tools/native/save_check.sh`.

## Windows

Built from WSL with the pinned MSYS2 MinGW-w64 toolchain (hashes in `tools/toolchain/toolchain.sha256`) and the MSYS2 SDL3
package (`tools/render/README.md`). Put the toolchain at `<WIN>/toolchain/mingw64`, where `WIN` (in `local.env`) is a
folder on the Windows side (default `build-win/` in the repo); set `SDL3_MINGW` / `MSYS2_PKGS` to the unpacked SDL3
package and the folder with its `.pkg.tar.zst`. Then:
```
RENDER=1 tools/native/build_win.sh        # native_play.exe, b3render.dll, Burnout3Launcher.exe into <WIN>/s0native
tools/release/package_win.sh              # the tester zip into <WIN>/release (a clean tree only)
```

## macOS

`make render` / `make play` pick up Homebrew's `sdl3` through pkg-config (`docs/mac.md`). Apple Silicon and Intel Macs
build the same way; the Intel status is `docs/mac.md` section 11.

## Rules

[CONTRIBUTING.md](../CONTRIBUTING.md) has the project rules (no game data in git, remaster features never change
gameplay by default, the checks to run, ...). Work on your own branch and open a pull request against `main`.
