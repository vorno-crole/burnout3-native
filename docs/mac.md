# The Mac (Apple Silicon)

Intel Macs (x86-64): the game builds from source and plays at full speed, see section 11. There is no Intel download yet.

## Playing: the Mac app (download)

The ready-made Mac download is a disk image, `burnout3-native-<version>-macos-arm64.dmg`, with **Burnout 3
Native.app** in it: the launcher, the game and its renderer (Metal through SDL 3), SDL 3 itself and the mod tool, all
inside the app. It needs a Mac with Apple Silicon (M1 or later) and the macOS version its `README-TESTERS.txt` names.
No Homebrew and no developer tools are needed to play.

1. Open the `.dmg` and drag **Burnout 3 Native** onto the **Applications** folder beside it.
2. **The first start.** The app is ad-hoc signed but not notarized by Apple, so macOS blocks it the first time:
   - **macOS 15 (Sequoia) and newer:** open the app once (macOS says it cannot check it: click **Done**), then
     **System Settings > Privacy & Security**, scroll down to *"Burnout 3 Native.app" was blocked*, **Open Anyway**,
     and confirm.
   - **macOS 14 and earlier:** the same, or right-click (Control-click) the app > **Open** > **Open**.
   - **Any version:** once in Terminal, `xattr -dr com.apple.quarantine "/Applications/Burnout 3 Native.app"`.
3. The launcher opens: its **Game Data** page takes the folder with your disc's files, then **START GAME**.

Your saves, settings and logs are in `~/Library/Application Support/b3port/native/` (Finder: Go > Go to Folder...),
the same folder the build-it-yourself steps below use. Inside the app (right-click > Show Package Contents):
`Contents/MacOS` holds `b3launcher` (the app's own program), `native_play`, `libb3render.so`, `libSDL3.0.dylib` and
`b3modtool`; `Contents/Resources` holds `README-TESTERS.txt`, `KNOWN-ISSUES.txt`, `LICENSES/` and the Blender add-on
(`b3_car_tools.zip`). The player guide, [PLAYING.md](PLAYING.md), covers the rest. Section 10 below builds the same
app yourself.

## Testing from source

Two things to check on a Mac: that **b3view draws a track with Metal**, and that the **PS2-exactness unit tests
pass on arm64**. You only need the repo and your disc files. Nothing here writes to the disc folder, and nothing
made from it goes into git (`build/` is git-ignored).

## 1. One-time setup

1. Xcode command line tools: `xcode-select --install` (skip it if `clang++ --version` already works).
2. Homebrew (https://brew.sh), then: `brew install sdl3 pkg-config make`
   (`make` gives a current GNU make, `gmake`; macOS's own is from 2006.)
3. The repository: `git clone https://github.com/u0rhs/burnout3-native ../burnout3-native`
   (a clone you already have: see section 9, "Get the newest code"). Use a path without spaces.

## 2. Build, test, view

From the repo folder (`cd ../burnout3-native`):

```
tools/mac/build.sh  <disc folder>    # e.g. ../game -- the folder with SLUS_210.50, DATA, TRACKS, PVEH ...
tools/mac/test.sh                    # unit tests + one headless b3view picture; writes build/mac/report.txt
tools/mac/b3view.sh AS/C1_V1         # the window: free camera on a route
```

- `build.sh` checks the tools and prints what it found. It remembers the disc folder (`build/mac/disc`) and builds
  everything into `build/mac/` (log: `build/mac/build.log`). If you already copied the disc to `data/game`, the
  argument can be left out.
- `test.sh` runs 32 test programs (two to five minutes), renders `AS/C1_V1` once without a window, and prints the
  report.
- `b3view.sh` takes a route (`AS/C1_V1`, `EU/C4_V1`, ...), a whole region (`EU`), or a car (`PVEH/COMP/CAR1.BGV`).
  Controls: hold the right mouse button and move to look; W/A/S/D move; Q/E down/up; Shift is faster; the wheel
  changes speed; N/B next/previous stream unit; F fog; F11 fullscreen; Esc quits. The title bar shows fps, size and
  driver.

## 3. What to send back

1. `build/mac/report.txt`. It has the machine, compiler, SDL version, every test's result and the retail-comparison
   lines; no game data.
2. The picture named in the report (`build/mac/b3view-AS_C1_V1.png`), to compare with the same view rendered on
   Vulkan.
3. A screenshot of the b3view window, title bar included.
4. If something failed: `build/mac/build.log`, or the failing test's log `build/mac/mac-tests/<test>.log`.

## 4. What a result means

| Result | Meaning |
|---|---|
| All `exactness` rows PASS, with retail lines saying `bit-exact` | The PS2 arithmetic model gives the same bits on Apple Silicon as on the PC: physics stays PS2-identical on ARM. That covers the EE float rules (`b3fp`), VU0 (GtMath), the game's libm compared bit for bit with the retail code, and the FPCR round-toward-zero mode. |
| `test_fenv` FAIL | The CPU's float mode (FPCR round-toward-zero + flush-to-zero) does not behave as on x86 / qemu. Plain C float code would then drift from the PS2. The table-driven model (`b3fp::ref`, used by the faithful build) is not affected. |
| `test_ps2float` / `test_gtmath_*` / `test_libm*` FAIL | The model itself computes differently under Apple's clang (for example a fused multiply-add slipping in). That would be a real porting bug. Send the log. |
| `SKIP` on `test_libm_cost`, or `test_libm` with only ~29 checks | No `SLUS_210.50` was found in the disc folder, so the comparisons with the retail code did not run. Re-run `build.sh` with the right folder. |
| `other` rows FAIL | Not about physics: renderer decoders, HUD readers, audio DSP, the native backend and platform. |
| `NOT BUILT` | A compile or link error under Apple's toolchain: `build/mac/build.log` has it. |
| b3view picture fails, but the window works | Only the windowless Metal path is affected (`--offscreen`, never run on a Mac before). |
| The window says `metal` and shows the track | The Metal claim holds (shaders: SPIR-V -> MSL, `src/b3gfx/shaders_gen.h`). |

## 5. Keys for the native game (no controller needed)

The native game reads the keyboard as a PS2 DualShock 2 (`src/native/input_keys.*`). A connected pad still works
alongside it. The keys depend on whether you are **driving** (the race simulation runs) or in a **menu** (front end,
pause, results): native_play watches whether the game reads the player's accelerator (`GetAccelerator` 0x12FA40, called
only by the player car's driving / crashing controls) and logs each switch (`keys: driving` / `keys: menus`). So the arrows move through menus and Up never confirms there.

| Key | Driving | Menus | Game evidence |
|---|---|---|---|
| W / Up | accelerate (Cross; also stick up = Aftertouch while crashing) | up (D-pad) | GetAccelerator 0x12FA40 reads Cross [C]; GetAftertouch 0x12F720 the stick Y [C] |
| S / Down | brake / reverse (Square; also stick down) | down (D-pad) | GetBrake 0x12F970 reads Square [C] |
| A / D, Left / Right | steer (stick X + D-pad pressures, ramped to full in 0.12 s) | left / right | GetSteering 0x12F850 [C] |
| Space | boost (R1); hold it as you crash: Impact Time + Aftertouch | confirm (Cross) | 0x12F160 -> StartBurn; GetCrashingDrivingControls [C] |
| Enter (or keypad Enter) | Cross | confirm (Cross) | |
| Escape | pause (Start) | Start ("Press Start", resume) | 0x12FB10 Start [C] |
| Backspace | change camera (Triangle) | back (Triangle) | 0x12F330 [C call, L meaning] |
| C | change camera (Triangle) | back | |
| Q | look back (L1) | L1 | 0x12F240 [C call, L meaning] |
| E (or 3) | Crashbreaker (R2, Crash mode) | R2 | 0x12F610 -> DoCrashBreaker [C] |
| Left Shift, R | boost (R1) | R1 | |
| H, X | horn (Circle) | Circle | 0x12F6B0 [C] |
| 1 | music track skip (L2) [L] | L2 | 0x12F460 [L] |
| Z | Cross | Cross | |
| Tab | Select | Select | |
| V | -- | Square (car select Color, MULTIPLAYER Change Names) | the pages' Square prompts [C shots, fe1] |

Movies: Enter, Space or Escape (pad: Cross or Start) skips the movie that is playing (docs/remaster-features.md §2);
a corner hint shows the button for the device in use (SETTINGS > GAME > INTERFACE > VIDEO SKIP HINT turns it off).
The table is in `default_key_map()`: the default layout. Remapping (remap1, docs/pc-settings.md §7.9): SETTINGS >
CONTROLS > BINDINGS in the game, or the launcher's Controllers page, rebinds any PS2 button or stick direction per
player, for the keyboard (separately while driving and in menus) and for controllers; the choice is saved in
controls.ini in the user data folder and the prompts show the keys and buttons bound. Player 2 can have keys too (none
by default): they then drive a second controller port.

Button prompts (mac5, remaster-features §3): the game's button glyphs show the device used last -- key caps with
these keys after a key press, Xbox / Nintendo / generic buttons after a pad's button, the game's PlayStation glyphs for
a PlayStation pad and before any input. SETTINGS > GAME > INTERFACE > BUTTON PROMPTS fixes one; `B3_PROMPT_DEVICE=keyboard|
playstation|ps5|xbox|nintendo|generic` (ps5 = ps4: the PS4 / PS5 style) fixes it for a run (offscreen shots).

Two controllers (pad2, 2026-11-11; local 2-player: ROAD RAGE, TEAM CRUSH, DOUBLE IMPACT, PARTY CRASH, the
multiplayer races in split screen): the first gamepad connected is player 1 (port 1, together with the keyboard and the
mouse), the second is player 2 (port 2). Port 2 is plugged in while the second pad is connected: pulling it out is a
PS2 pad pulled out of port 2 (the game shows its own controller-removed handling), connecting it again plugs it back
in. A pad keeps its player while it stays connected: if player 1's pad goes, player 2's stays player 2 and the next pad
connected becomes player 1 (to make a lone remaining pad player 1, disconnect and reconnect it). A third pad and more
act as player 1 (as every pad did before) and take an empty seat when one frees up. With one gamepad (or none)
nothing changes: port 2 stays empty. The button prompts follow player 1's devices (the keyboard and player 1's pad);
photo mode and the PC SETTINGS page take player 1's pad; BOOST: Toggle applies to both players; with SETTINGS > CONTROLS > FEEDBACK > RUMBLE On
both pads rumble, each for its own car (off by default). The log says `[b3pad] gamepad: <name>, player 2 (port 2 connected)` and
`native_play: VBlank N: port 2: a gamepad plugged in (player 2)` / `pulled out`. `--keys2` (scripted port 2) and
`--replay-pad` take precedence: the window's second pad is then ignored, and a pad recording holds port 1 only.

Mouse (mac5, remaster-features §8): in the game's menus, pointing at a row selects it, a left click selects and
confirms it, a click on a prompt at the bottom (Select, Back, START Resume ...) presses that button, a right click is
Back (Triangle), the wheel moves up / down; on a page without a list (a message, the car or track pages) a left click
continues (Cross). The SETTINGS page takes the mouse too (rows, tabs, sliders, the wheel scrolls, right click closes).
The cursor shows when the mouse moves and hides after 3 s or when a key or a pad is used; the prompts keep the key caps
while the mouse is used. In a race the mouse does nothing (the pause menu takes it).

Typing a name (mac5): on the profile name keyboard (NEW PROFILE) letters, digits, space and punctuation type into the
name, Backspace deletes, Enter is Done; the arrows still move the on-screen keyboard's cursor and Escape / Tab keep their
buttons.

## 6. The native game (native_boot) on arm64: status (mac2, 2026-11-07)

Not a user step yet: boot2's native game does not draw a picture yet. Checked here, without a Mac:

- **Compiles for aarch64 with clang 21.** All of the platform tier (`src/native`, `tools/native/native_boot.cpp` /
  `native_check.cpp`, gameview, b3host). The generated game code (boot2's generation, 4,853 units): 4,844 compile.
  The other 9 rely on GCC's `-fpermissive` leniency, which clang does not have:
  - 5 have a `goto` that jumps past an initialisation;
  - 1 assigns through a `const&` (a T31 escape);
  - 1 assigns to a `volatile` ps2f field;
  - 1 takes the address of an rvalue;
  - `sce_libmc2`: `char* = strrchr(const char*, ...)`.
  These need generator or source fixes (gen1 / boot2's code). Until then, Homebrew's GCC (`brew install gcc`) is the
  fallback for the native build on the Mac.
- **Portability fixes:**
  - `gp<T> == T*` made unambiguous: a template, the operator GCC already picks;
  - clang's flags for the generated units;
  - an explicit 8 MB stack for the guest threads' host threads (macOS gives 512 KB);
  - zstd through pkg-config on macOS.
- **Runs under qemu-aarch64, identical to x86.** `native_boot --tickhash ... --polls 200` (now `--poll-point`; the tick hash was retired on 2026-11-11) with B3NATIVE_MAX_VBLANKS=600
  on boot2's code (3056978) and generation:
  - aarch64 (clang 21, qemu) and x86-64 (g++), both without the 8 units clang rejects, stop at the same missing
    function (`Prepare__20CB3FlashText2dObject...`, boot2's current frontier) after the same 68,772,071 dispatch calls;
  - per-function call counts and the stub log are identical.
  - Guest RAM differs only in two guest stacks (493 words), by host stack bytes that `ptr_arg` bounce windows copied
    in (host addresses: these differ between any two runs too).
- **The first run diverged, from a real bug.** In RW Audio's `func_002AD480` (src/libs/rwa/rwastreamnodety.cpp), retail
  passes a two-word stack object to a getter that writes both words (sp+0x38 / +0x3C). The C had one `RwUInt32`, so
  the second word lived in whatever host stack followed it. That worked on x86 by layout; on aarch64 the stream
  callback went to a wrong address and the boot called a null pointer. Found by bisecting call counts and guest RAM
  between the two builds, then boot2's `--oracle` (x86 SAME, aarch64 DIFF). Fixed (`slot[2]`, mac2).
  `-fsigned-char` was tested first and ruled out.
- `native_boot` reads the ELF from `B3_ELF=<disc>/SLUS_210.50` (its default path is this PC's).

## 7. Status before the first run (2026-11-07, branch mac1)

Done on Linux without a Mac:
- **arm64.** The 28 tests that need no zlib, built for aarch64 by clang 21 and run under qemu-aarch64, all pass,
  including the retail-ELF oracle: libm bit-exact, cost exact, and `test_fenv` with FPCR RZ+FZ, 0 of 5.2M operations
  differing from `b3fp::ref`. qemu emulates the AArch64 float rules in software, so the M1 can still differ: that is
  what `test.sh` settles.
- **libc++.** All 32 tests build and pass against LLVM libc++ (x86-64), the C++ library family Apple ships. The
  viewers build. Every Mac-built source compiles against libc++ 17 headers.
- **Fixes this needed:** `-msse2` only on x86 (Makefile); a NEON fast path and an FPCR `Ps2FloatEnv` (`include/b3host/`);
  macOS ucontext (`_XOPEN_SOURCE`) and `dladdr` for the image base (`src/b3host/`); `std::chrono::clock_cast`
  (not in libc++) replaced in `save_card`; the `PtrArg` snapshot order (`include/b3host/call.h`), a latent bug that
  only libc++ exposed.

Not checkable here: Apple clang / ld64 / Mach-O, macOS's ucontext on arm64, SDL3 with Cocoa + Metal, and the
windowless Metal path.

On Linux: `tools/mac/cross_check.sh [a64|lcx|lcx17|all]` repeats these checks (toolchain:
`tools/mac/toolchain-linux.sha256`; `RETAIL_ELF=<disc>/SLUS_210.50` for the oracle).

## 8. The M1 runs (2026-11-07) and their fixes (branch mac3)

On an M1 Max (Apple clang 21, SDL 3.4.18):
- every exactness test passed; the libm retail comparisons were bit-exact with exact costs; Ps2FloatEnv: 0 of 5.2M
  operations differed;
- b3view rendered on Metal: offscreen, and windowed at 3328x1800, about 298 fps.

Three failures, all fixed on mac3:
- **test_livelap.** The ring readers sized the shared memory with `lseek(SEEK_END)`, which macOS does not support on a
  POSIX shm object, so no reader could open. Now `fstat`. [C] It passed on the second M1 run (2,360 checks).
- **test_clock, NOT BUILT.** Its recipe (and test_gameinput's, b3clocksim's, b3latsim's) had no `-std`; Apple clang's
  default has no `std::clamp`. Reproduced with libc++ at gnu++14, including the same errors at clock.h 69:22 / 120:26.
  Now they compile at C++20. `tools/mac/test.sh` writes a NOT BUILT test's compiler errors into report.txt.
- **test_nonatomic, segfault (rc 139) in windows_and_pass_through_per_fiber** (test_nonatomic and the fibers were retired
  2026-11-11; the stack clamp below stays, now on the thread's own stack).
  - Cause: a `ptr_arg` window (336 bytes around a local near the top of a fiber stack) read past the stack's malloc'd
    block into an unmapped gap. That happened with both the old ucontext path and the AAPCS64 switch.
  - Fix: windows are clamped to the current stack. A running fiber publishes its range; other code uses the thread's
    own stack (pthread_getattr_np / pthread_get_stackaddr_np / the Windows TIB).
  - POSIX fiber stacks are now mmap'd between guard pages, so Linux faults the same way. Reproduced under qemu-aarch64
    before the fix; new test `window_at_the_ends_of_a_guarded_stack`.
  - AArch64 also switches fibers with its own AAPCS64 routine (x19-x30, d8-d15, FPCR) instead of the deprecated
    ucontext.

To rerun after the merge: `git pull`, `tools/mac/build.sh <disc folder>`, `tools/mac/test.sh`; send back
`build/mac/report.txt`.

## 9. The native game on the Mac: native_play (mac4, 2026-11-07)

The game's own code compiled for the Mac:
- the decomp's C++ (generated, committed in `gen/native`, so no decomp checkout is needed);
- the platform tier, the in-process renderer (Metal through SDL3) and the audio mixer.

It reads only the disc folder you give it.

**Get the newest code.** The repository's history was replaced by a fresh start more than once, so in a clone made
before that, `git pull` refuses ("unrelated histories"). Update it with:
```
git fetch origin
git reset --hard origin/main      # throws away local changes in this clone; build/ (not tracked) stays
```
or make a fresh clone (`git clone https://github.com/u0rhs/burnout3-native`).

**Run the game:**
```
tools/mac/native.sh <disc folder>
```
- The first build compiles about 4,900 files: roughly 10-20 minutes on an M1. Later runs only relink what changed.
- One-time setup is that of section 1 (`brew install sdl3 pkg-config make`).
- Options after the folder go to native_play: `--no-audio`, `--quit-after 300` (seconds), `--offscreen` (no window,
  for a log only).
- `SAVES=<folder> tools/mac/native.sh <disc folder>` inserts a memory card. The folder holds the game's saves; use a
  copy. Without it, there is no card, and the game says so at the start.
- `BUILD_ONLY=1` builds without running.

**Or with the launcher:** `LAUNCHER=1 tools/mac/native.sh` builds the launcher too (`build/mac/b3launcher`, beside
native_play) and opens it: its Game Data page takes the disc, and START GAME runs the game from the same folder. Its
other pages (settings, saves, mods, music, the showroom, replays, controllers) work as on Windows and Linux. The
launcher builds on macOS in CI, and a build of the .dmg has been installed and run on a real Mac.

**What you should see:** a window, the intro movies (skippable), the title screen, the menus, and every race mode,
World Tour included, with sound, as on the PC. Report anything that differs.

**Keys:** section 5 (WASD / arrows drive and move through menus, Space boost, Enter confirm, Escape pause / Start,
Backspace back). A connected controller also works.

**Send back:**
- `build/mac/native_play.log` and `build/mac/pads.txt` (the pad recording: the button / stick states per frame, no game
  data; with it the same run is replayed exactly on the PC, `REPLAY_PAD=build/mac/pads.txt` / `--replay-pad`), and
  `build/mac/native-build.log` if the build failed;
- a screenshot (Cmd+Shift+4) of each new screen, and of the last one if it stops;
- whether the sound played, and whether it felt smooth.

The log names any game function that runs as the original PS2 code (the fallback list at the end), and gives a
report if the game stops.

## 10. Building the Mac app yourself: tools/release/package_mac.sh

`tools/release/package_mac.sh` builds the download above from the source, on an Apple Silicon Mac (on an Intel Mac:
the Intel package, `-macos-x86_64.dmg`, section 11) with the one-time
setup of section 1 (the Xcode command line tools; `brew install sdl3 pkg-config make`):

```
tools/release/package_mac.sh                     # build + package: build/release/burnout3-native-<version>-macos-arm64.dmg
SMOKE=1 tools/release/package_mac.sh             # ... then the smoke test of the packaged app (no game data needed)
SMOKE=1 B3_GAME_DIR=<disc folder> SAVES=<card folder> tools/release/package_mac.sh
                                                 # ... plus the strict race from the app's own native_play
```

- **The build** (`build/mac-release/`, log `release-build.log`): native_play, libb3render.so, the launcher and
  b3modtool with Apple clang from the committed native bundle (`NATIVE_FROM=bundle`, as `tools/mac/native.sh`), for
  the minimum macOS of the SDL 3 it bundles (Homebrew's is built for the macOS its bottle is for; `MACOS_MIN` and
  `SDL3_DIR=<prefix>` choose others). The first build compiles about 4,900 units. `OUT=<folder>` packages a finished
  build instead.
- **The package:** the programs stripped (their dSYMs go to `build/release/debug/`, for a crash report's offsets),
  SDL 3 copied into the app and every reference to it rewritten to `@rpath/libSDL3.0.dylib` with `@executable_path` /
  `@loader_path` rpaths (`install_name_tool`), the `Info.plist` (bundle id, version, minimum macOS, high resolution,
  game controllers), the icon (`b3.icns`, the prebuilt `assets/icon/b3-icon.icns`), the readme, the known
  issues and the licenses, then an ad-hoc signature (`codesign -s -`: Apple Silicon runs only signed code) and a
  compressed disk image (`hdiutil`, UDZO) with the app beside a link to `/Applications`. There is no notarization
  step.
- **The checks it always makes:** every program built for the Mac's CPU (arm64; x86_64 on an Intel Mac); `otool -L` shows only macOS's own libraries and the app's own
  (no Homebrew path: the app runs on a Mac without Homebrew); no absolute rpath; all three programs carry the same
  version stamp (a build with uncommitted changes is refused; `ALLOW_DIRTY=1` makes a test package); the signature
  verifies; the privacy scan (no user name or home folder of the build machine in the app).
- **`SMOKE=1`:** opens the image, copies the app out of it as a player would, verifies its signature, runs the
  launcher's screenshot mode offscreen (every page drawn, every walk check PASS, SDL 3 loaded from inside the app)
  and, with `B3_GAME_DIR` and `SAVES`, `tools/native/race_check.sh` with `STRICT=1` on the app's own native_play (no
  fallback to retail code, VU0 all generated). Its files are in `build/release/smoke-mac/`.
- **`DRY_RUN=1`** runs anywhere (also on Linux): placeholder programs, the bundle's folders, `Info.plist` and texts in
  `build/release/stage-mac/`, the privacy scan, and the Mac-only commands printed instead of run.

**In CI** the GitHub Actions workflow `release-mac.yml` runs `SMOKE=1 tools/release/package_mac.sh` on a
`macos-latest` (Apple Silicon) runner, without game data, and keeps the `.dmg` as the `macos-dmg` artifact (the dSYMs
as `macos-dsym`, the logs and the launcher's pages as `macos-package-logs`). It runs by hand (`gh workflow run
release-mac.yml`, or the Actions tab) and on `v*` tags, not on every push.

## 11. Intel Macs (x86-64): status (macintel1, 2026-11-18)

The game builds from source and plays on an Intel Mac at full speed, with one Makefile fix (below).
`tools/release/package_mac.sh` makes an Intel `.dmg` on an Intel Mac (below). No Intel download is published yet.

**Base: 1.3.0** (rebased 2026-11-22; before that 1.2.0 on 2026-11-19). The table says which checks were run on which
release. Rows without a release were measured on 1.1.0.

**Test machine:** Core i7-9700K (AVX2, BMI1/2, FMA), Radeon RX 6600 XT (RDNA2, Metal 3), macOS 15.4, Apple clang 17
(Xcode 16.3), Homebrew under `/usr/local`, SDL 3.4.18 from Homebrew.

**Why it should mostly just work:**
- Every x86 path is already built and tested on Linux and Windows: `-msse2`, `Ps2FloatEnv` on MXCSR, and the x86
  `b3fp` paths.
- The Mac-specific code is the same whatever the CPU: `dladdr` for the image base, `pthread_get_stackaddr_np`,
  sysctl, `fstat` on shm, spawning programs.
- The asm in `src/libs/rw` is PS2 VU0 stand-ins (`b3host/eeasm.h`), not host asm. `b3compat/glibc_compat.h` only
  applies on Linux.
- The renderer draws with Metal through SDL 3's GPU API, not Vulkan, so MoltenVK is not involved. Its texture formats
  (RGBA8, R8, RGBA16F, D32F, with D24 / D16 checked through `SDL_GPUTextureSupportsFormat`) all exist on AMD GPUs
  under Metal. The 16-bit packed formats, which are missing on non-Apple GPUs, are not used.

**Setup:** section 1, plus `brew install make`. GNU make 4 is required (macOS's make 3.81 lacks `$(file ...)`). The
build scripts print "not arm64 (Rosetta shell?)" on Intel; it is harmless.

**A disc image works for the scripts too.** `build.sh`, `test.sh` and `b3view.sh` take a folder. Mount the `.iso`
read-only (`hdiutil attach -readonly <iso>`, or double-click it) and give them the mounted volume
(`/Volumes/SLUS_21050`). The game and the launcher read the `.iso` directly.

**Checked so far:**

| Check | Result |
|---|---|
| `test_ps2float` (Apple clang 17, x86-64, Makefile flags) | PASS, 249 checks |
| `test_fenv` (MXCSR round-toward-zero + flush-to-zero) | PASS, 18 checks |
| 63 of 4,961 `gen/native` units (every 80th file), `-fsyntax-only` with the Makefile's clang generated-unit flags | 0 errors |
| full build (`BUILD_ONLY=1 LAUNCHER=1 tools/mac/native.sh`), first try | FAIL: `func_003BB7A0` (below); 2,870 units compiled before it stopped |
| full build with the `-include functional` fix | PASS, 0 warnings: `native_play` (Mach-O x86_64), `libb3render.so`, `b3launcher`. `otool -L`: macOS's own libz / libSystem / libc++, plus Homebrew's SDL 3 for the renderer and the launcher |
| `tools/mac/build.sh <mounted ISO>` | PASS: SLUS_210.50 matches the retail sha256; every test and both viewers built |
| `tools/mac/test.sh`, exactness (13 tests, including the retail-code comparisons) | PASS, all of them. libm against the retail code: every function bit-exact with exact cost. Ps2FloatEnv (MXCSR): 0 of 5.2M operations differ from `b3fp::ref` |
| `tools/mac/test.sh`, other (46 tests) | 45 PASS, 1 SKIP: `test_livelap` needs `data/scene/race2.b3scene`, a file from the repo's data folder that isn't in this checkout, so it is not a Mac problem |
| `make test` (the full suite) | On 1.1.0 it stopped three times, each on a problem that shows on any Mac, not only Intel, and each now fixed (below): `test_mpeg2_decoder` NOT BUILT, `test_code_mods` 1 failure, `test_tune_browse` 1 failure |
| **1.2.0:** full build (`BUILD_ONLY=1 LAUNCHER=1 tools/mac/native.sh`) | PASS, in 201 s on top of the 1.1.0 objects |
| **1.2.0:** `make test` (the full suite, `-j8`) | PASS, exit 0: every C++ test (`test_livelap`'s scene checks skip as above), the compile-fail checks, `decomp2host` 147 cases, the icon check, the Blender add-on tests, `decl_check` 0 of 1,427. It took two more fixes (below): `test_native_platform` linked before the CHD unit was built (1.2.0, every platform), and `test_decomp2host.py`'s clang check failed because `g++` is Apple clang on macOS |
| **1.3.0:** full build (`BUILD_ONLY=1 LAUNCHER=1 tools/mac/native.sh`) | PASS, in 1,459 s (most units recompiled) |
| **1.3.0:** `make test` (the full suite, `-j8`) | PASS, exit 0, after two more fixes (below): `test_settings` read freed memory (1.3.0's `portable_mode_row` failed), and the one-click update learned the Intel package's name. `test_settings` 87 tests, 16,225 checks; `test_launcher_oneclick` 4 tests, 35 checks; `test_launcher_update` 12 tests, 153 checks |
| **1.2.0:** `SMOKE=1 B3_GAME_DIR=<mounted ISO> SAVES=<a copy> tools/release/package_mac.sh` (clean tree) | PASS: `burnout3-native-1.2.0-<commit>-macos-x86_64.dmg`, 22 MB, in 7.5 min (the objects of the 1.1.0 package reused). Every program x86_64, macOS 15.0; `otool`: no Homebrew path; the privacy scan is clean; the libchdr / LZMA / miniz / zstd licences are in the app. The smoke test: the copy out of the image verifies (`codesign --strict`); the launcher's screenshot mode drew 52 pages, 59 PASS and no FAIL (the offscreen video driver did not start, "Could not initialize OpenGL / GLES library", so it ran again with hidden Cocoa windows, as the script does); the strict race check with the app's own `native_play`: PASS, US/C3_V1 driven to VBlank 23,400, fallback none, VU0 9,922,532 launches all generated, 158 VBlanks/s unpaced |
| `b3view --offscreen AS/C1_V1` | PASS through both Vulkan and Metal, and the two pictures look the same (below) |
| `STRICT=1 tools/native/race_check.sh build/mac/native_play` (offscreen; `B3_GAME_DIR` the mounted ISO, `SAVES` a copy of a profile made on this Mac) | PASS: race US/C3_V1 reached and driven to VBlank 23,400. Fallback: none (compiled out). VU0: 9,922,532 launches, all generated, 0 interpreted. 122 VBlanks/s unpaced |
| `ALLOW_DIRTY=1 tools/release/package_mac.sh` on the Intel Mac | PASS: `burnout3-native-1.1.0-<commit>-dirty-macos-x86_64.dmg`, 21 MB, built in 27 min (6 jobs, low priority). Every program is x86_64 and needs macOS 15.0 (the bundled SDL 3.4.18's minimum). `otool`: no Homebrew path. Ad-hoc signature verifies; the privacy scan is clean |
| the game in a window (`tools/mac/native.sh <mounted ISO>`), first play test, about 5 minutes | PASS, "works great": intro, menus and several races. Driver **metal** (SDL reported `metal, vulkan` and chose Metal for the window), MSAA 4x, present immediate. A steady 59.9 VBlanks/s (paced at 59.94 Hz), with short catch-up stretches above 60 after loads. 7,071 native functions, 0 fallback calls. A DualSense was player 1 (PS5 prompts). Sound: 48 kHz s16 stereo |

**Found: generated units' macros against an older libc++.** `gen/native/units/func_003BB7A0.cpp` (dlmalloc code)
defines function-like macros such as `#define first(b) ((b)->fd)` and `last(b)`. At its end it includes
`b3host/native_rows.h`, which brings in `<functional>`. Xcode 16.3's libc++ (Apple clang 17) has `std::function` call
`__f_.first()`, which the macro rewrites, so the unit does not compile (`function.h:152: expected unqualified-id`). The
M1's newer Xcode has a libc++ that no longer calls `.first()` there, which is why the arm64 build never hit this. So
it depends on the Xcode version, not on the CPU. Only headers included after such a macro are affected. A rebuild that
carried on past errors (`MAKEFLAGS=-k`) compiled the other 4,960 units: this is the only one. Fix (Makefile, macOS
only): the generated units are compiled with `-include functional`, so `<functional>` is read before any unit's
macros. Linux and Windows builds are unchanged. A lasting fix would be in the generator (decomp2host): emit
`native_rows.h` before the unit's macros, or `#undef` them first.

**Found: `test_mpeg2_decoder` did not build under Apple clang.** `make test` (the full suite, not only the Mac
subset `tools/mac/test.sh` runs) stopped at it: its recipe, and `mpeg2_check`'s, had no `-std`, and Apple clang's
default standard has no `std::clamp` (`src/native/mpeg_dsp.h`, `mpeg2_decoder.cpp`). This is the same thing mac3 fixed
for `test_clock`. Fixed the same way: both recipes now take `$(REQFLAGS)` (C++20). It applies to Apple Silicon too; the
CI's macOS job only runs `mac-tests`, so it never built this test.

**Found: `test_code_mods` failed on any Mac.** `mod_hash_covers_every_library` makes a code mod whose `[code]` names
a library for `linux` and `windows` only. On macOS, `b3mod::host_system()` is `"macos"`, so the mod has no library for
the host and the check `!a.mods()[0].library.empty()` fails. It is not in the Mac subset either. Fixed in the test:
its mod also has `macos = bin/c.so` (the same file; the test only hashes it). 12 tests, 240 checks, 0 failures.

**Found: a use-after-free in the tuning console's browser (every platform).** `test_tune_browse`
`favourites_and_recent_persist` failed on the Mac: after CONFIRM on a favourite, the cursor sat on element 0 instead
of the favourite's element 2. `Browser::enter()` (`include/b3tune/browse.h`) passes `jump()` a reference to a row's
`Item`, and `jump()` calls `rows()`, which rebuilds `rows_` and frees the row it was given. AddressSanitizer:
heap-use-after-free, read at `browse.h:887`, freed at `:885`. glibc's allocator left the old bytes in place, so Linux
passed by luck; macOS's reused them. Fixed: `jump()` takes its `Item` by value. The test passes (11 tests, 24,946
checks), and under `-fsanitize=address` too.

**Found (1.2.0): `test_native_platform` could link before the CHD unit existed (every platform).** 1.2.0 adds CHD disc
images (`chd_libchdr.o`). `test_native_platform`'s rule names `$(CHD_OBJ)` as a prerequisite, but that rule comes
before `CHD_OBJ` is defined, and make expands a rule's prerequisites when it reads the line. So the prerequisite was
empty, while the recipe (expanded later) still linked the file. A parallel `make test` in a build folder without the
object linked the test first: `clang++: error: no such file or directory: '.../chd_libchdr.o'`. A build where another
target compiled the unit first passes by luck. Fixed: after `CHD_OBJ`'s definition, the Makefile adds
`$(BUILD)/test_native_platform: $(CHD_OBJ)`.

**Found: `test_decomp2host.py` assumed `g++` is GCC.** Its "clang check-compile flags" case checks that a compiler
reporting clang gets the clang flag, and that `g++` does not. It does that with a fake clang script and the real
`g++`, which on macOS is Apple clang (`g++ --version`: "Apple clang version 17.0.0"), so the second half failed. Fixed
in the test: a fake GCC script, as for clang, so the case no longer depends on the machine. 147 cases, 0 failures.

**Found (1.3.0): `test_settings` read freed memory twice (every platform).** `portable_mode_row` (new in 1.3.0) failed
on the Mac: `r = find(build_content(s, e))` keeps a pointer into the page that `build_content` returned, a temporary
freed at the end of that line, and the next line reads `r->value` from it. `music_folder_and_playlist` (there since
1.2.0) does the same in a range-for over `tab(build_content(...))->rows`. AddressSanitizer: heap-use-after-free in
both. glibc's allocator left the bytes in place, so Linux passed; macOS's reused them for the portable row (the
older one passed here by luck too). Fixed in the test: each page is kept in a variable first. 87 tests, 16,225
checks, 0 failures, and clean under `-fsanitize=address,undefined`.

**Found (1.3.0): the one-click update did not know the Intel package's name.** 1.2.1's one-click update
(`src/b3launcher/oneclick.cpp`) reads the version from the package's file name, and only knew `-win64.zip`,
`-linux-x86_64.tar.gz` and `-macos-arm64.dmg`. The Intel launcher picks `-macos-x86_64.dmg` (above), so it would have
refused that package. It knows the Intel name now, with a case in `test_launcher_oneclick`. Nothing changes until a
release has an Intel package.

**The GPU driver depends on what is installed.** The renderer lets SDL 3 choose (`b3gfx_sdl.cpp`, no
`SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING` unless `--driver` is given). On a Mac with Homebrew's `vulkan-loader` and
`molten-vk`, `b3view --offscreen` (SDL's offscreen video driver) got **Vulkan through MoltenVK** (1.4.2 here, through
`/usr/local/etc/vulkan/icd.d`). `--driver metal` gave Metal, through the cocoa video driver with no window. Both
pictures of `AS/C1_V1` look the same: 189,756 vertices, 3,908 draws, 180 textures, MSAA 4x. On a Mac without a Vulkan
loader (the M1, the `.dmg`), only Metal exists. Watch for this: an old MoltenVK (1.2.x, an older Homebrew bottle) has
known AMD bugs, among them `R8_UNORM` read as zero, which the renderer uses. The title bar and the log name the
driver in use.

**Which Intel Macs: what is known, and what is not.** Only one machine has been tested: an i7-9700K with a Radeon RX
6600 XT (RDNA2, the GPU family of the Mac Pro 2019's W6x00X modules). Everything else below comes from the code and
SDL 3's stated requirements, not from tests.

- **macOS 15 or newer, for now.** A package bundles the SDL 3 it was built with, and Homebrew's Intel SDL 3 is built
  for macOS 15, so the Intel app needs macOS 15 (Sequoia). That covers the Intel Macs Sequoia supports: MacBook Air 2020,
  MacBook Pro 2018 and later, Mac mini 2018 and later, iMac 2019 and later, iMac Pro, and Mac Pro 2019. Older macOS
  versions would need an SDL 3 built from source with a lower target (`SDL3_DIR=<prefix>`, `MACOS_MIN`).
- **CPU: any Intel Mac.** The build asks for nothing beyond x86-64 with SSE2 (no `-march`; `-msse2` is the only CPU
  flag), and the PS2 float model uses MXCSR, which every x86-64 CPU has. On the i7-9700K the offscreen race check ran at
  122 VBlanks/s unpaced, about twice real time, with the renderer included. How a 2-core or 4-core laptop CPU does,
  for example the MacBook Air 2020's, has not been measured.
- **GPU: no discrete GPU is required by the code. What it needs is a Metal "Mac2 family" GPU.** That is SDL 3's own
  rule for its Metal backend (`SDL_gpu.h`; the renderer does not set `METAL_ALLOW_MACFAMILY1`). The renderer uses no
  compute pipelines, only 2D textures, and the formats listed above. It checks MSAA sample counts
  (`SDL_GPUTextureSupportsSampleCount`) and D24 / D16 depth before using them. As far as Apple's tables go, the GPUs
  of the Intel Macs that run macOS 15 should all be Mac2: the integrated Intel UHD 630, Iris Plus and Iris Plus G4 / G7,
  and the AMD Radeon Pro (Polaris, Vega, Navi / RDNA1, RDNA2). So integrated graphics should run it. The open question
  is speed: none has been measured. On a weak GPU, SETTINGS > DISPLAY > QUALITY PRESET Low or PS2 original,
  ANTI-ALIASING off, and RENDER SCALE 75 % or 50 % are the settings to try. `KNOWN-ISSUES.txt` already suggests
  FRAMES IN FLIGHT 2 for a low frame rate.
- **MacBook Pros with two GPUs.** The renderer does not ask SDL for the low-power GPU
  (`SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN` is left at its default, false), so it should get the AMD GPU.
  The app's `Info.plist` sets `NSSupportsAutomaticGraphicsSwitching`, so macOS can go back to the integrated GPU when
  the game closes. Not tested on such a Mac: the launcher's log line `system gpu:` names the GPU in use.
- **eGPUs** (an AMD card in a Thunderbolt 3 enclosure): not tested. macOS's "Prefer External GPU" (the app's Get Info)
  is the usual way to send an app to one.
- **Hackintoshes:** what matters is a GPU that macOS has a Metal driver for. Modern macOS supports AMD Polaris
  (RX 400 / 500), Vega, RDNA1 (RX 5000) and RDNA2 (RX 6000, up to the 6900 XT). It has no drivers for RDNA3 and newer,
  for NVIDIA cards after Kepler, or for Intel Arc, so those cannot run it. The tested machine is a Hackintosh-class
  build (RX 6600 XT).
- **Vulkan:** if a Mac has Homebrew's `vulkan-loader` and `molten-vk`, SDL can see a Vulkan driver too. With a window,
  the game chose Metal (above). The app does not ship a Vulkan loader.

**The Intel app (macintel1).** `tools/release/package_mac.sh` (section 10) now packages for the CPU of the Mac it
runs on:
- `burnout3-native-<version>-macos-x86_64.dmg` on an Intel Mac, `-macos-arm64.dmg` on Apple Silicon as before;
- the `lipo` check, `LSArchitecturePriority`, `VERSION.txt`, the notices and the README's "what you need" line all
  follow `uname -m`;
- a Rosetta shell on Apple Silicon is refused (an Intel package is built on an Intel Mac);
- `DRY_RUN=1 MAC_ARCH=x86_64` checks the Intel layout anywhere.

The launcher's one-click update (`src/b3launcher/update.cpp` `package_asset()`) takes `-macos-x86_64.dmg` when it was
built for an Intel Mac (`__APPLE__ && __x86_64__`), and `-macos-arm64.dmg` otherwise. Every other build is unchanged.
`test_launcher_update` has a test for it (`mac_package_follows_the_cpu`). A release that offers both needs both
`.dmg`s and their lines in `SHA256SUMS`. A single universal (arm64 + x86-64) app would also need a universal SDL 3,
because Homebrew's is built for one CPU only.
