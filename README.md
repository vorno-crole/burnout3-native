# 🍎 Intel Mac fork: Burnout 3 native on Intel Macs only

> [!IMPORTANT]
> **This repository is a fork made only for Intel Macs (x86-64).**
>
> **Using Windows, Linux or an Apple Silicon Mac (M1 or later)? This isn't the repository you want. Go to upstream:
> [u0rhs/burnout3-native](https://github.com/u0rhs/burnout3-native)** and its
> [downloads](https://github.com/u0rhs/burnout3-native/releases). Upstream is the real project, is kept up to date, and
> has the downloads for those platforms.

This fork's `mac-intel` branch is upstream's release 1.4.0 plus a few small changes, so the port also builds, passes its
tests and plays on **Intel Macs**. The changes have been offered to upstream; once upstream takes them, use upstream on
an Intel Mac too.

- **What the fork changes:** a build fix for Xcode 16's Apple clang, an Intel `.dmg` from the Mac packager (the
  launcher's updater then picks the package that matches your Mac's CPU), and one bug fix in the tuning console that
  affects every platform. Gameplay is unchanged.
- **Which Intel Macs:** macOS 15 or newer, with a GPU in Metal's Mac2 family. A discrete GPU is not required: Intel UHD
  630 and Iris Plus integrated graphics qualify, and so do AMD's Polaris, Vega, RDNA1 and RDNA2 cards.
  [docs/mac.md §11](docs/mac.md#11-intel-macs-x86-64-status-macintel1-2026-11-18) has the details and lists what
  hasn't been tested.
- **Getting it:** download the Intel `.dmg` from this fork's
  [Releases](https://github.com/vorno-crole/burnout3-native/releases) (the release notes cover installing it, and say
  which upstream version it is; it can be behind this branch), or build
  it on your Mac from this branch: `brew install sdl3 pkg-config make`, then
  `tools/mac/native.sh <your disc folder or mounted .iso>`. The build steps are in [docs/mac.md](docs/mac.md),
  sections 1 and 11.
- **Updates:** from 1.4.0-intel.1 on, the launcher's update check looks at this fork's releases and tells you when a
  new Intel build is out; its Download opens the release's page. One-click install is off in the Intel build, because
  the fork's releases aren't signed with upstream's key. 1.3.0-intel.1 and earlier check upstream instead, so get one
  newer Intel release from this page by hand. Each fork release's version and tag end in `-intel.N`.
- **Tested on:** one Mac, a Core i7-9700K with a Radeon RX 6600 XT, on macOS 15.4. On 1.4.0 it builds and the full
  test suite (`make test`) passes. Each Intel `.dmg` on the Releases page passed its smoke test, including the strict
  race check against the original game (its release notes list the checks). A play session at a full 60 fps passed
  on 1.1.0.

**Everything below this line is upstream's README.** It describes the project as a whole, including platforms this
fork is not for.

---

<div align="center">

<!-- Header image: change only this line (the app icon / logo) -->
<img src="docs/logo.png" alt="Burnout 3: Takedown" width="480">

### Burnout 3: Takedown, native on Windows, Linux and macOS

The PS2 game as a real PC program: widescreen, high frame rates, modern controllers and mods,<br>
with the PS2's physics unchanged. **No emulator, no PS2 code.**

<!-- badges:start -->
<p align="center">
  <a href="#how-it-works"><img alt="Game code: 100% native" src="https://img.shields.io/badge/game%20code-100%25%20native-2ea043?style=for-the-badge&labelColor=0d1117"></a>
  <a href="#how-it-works"><img alt="Emulation: none" src="https://img.shields.io/badge/emulation-none-a371f7?style=for-the-badge&labelColor=0d1117"></a>
  <a href="#how-it-works"><img alt="Byte-matched functions" src="https://img.shields.io/badge/byte--matched-4%2C655%20of%205%2C652%20functions-388bfd?style=for-the-badge&labelColor=0d1117"></a>
</p>
<!-- badges:end -->

<p align="center">
  <a href="#platforms"><img alt="Windows: Vulkan" src="https://img.shields.io/badge/Windows-Vulkan-0078d4?style=flat-square&labelColor=0d1117&logo=data:image/svg%2bxml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxNiAxNiI+PHBhdGggZmlsbD0iI2ZmZiIgZD0iTTEgMWg2LjV2Ni41SDF6TTguNSAxSDE1djYuNUg4LjV6TTEgOC41aDYuNVYxNUgxek04LjUgOC41SDE1VjE1SDguNXoiLz48L3N2Zz4="></a>
  <a href="#platforms"><img alt="Linux: Vulkan" src="https://img.shields.io/badge/Linux-Vulkan-f0b400?style=flat-square&labelColor=0d1117&logo=linux&logoColor=white"></a>
  <a href="#platforms"><img alt="macOS Apple Silicon: Metal" src="https://img.shields.io/badge/macOS%20Apple%20Silicon-Metal-a2aaad?style=flat-square&labelColor=0d1117&logo=apple&logoColor=white"></a>
  <a href="https://github.com/u0rhs/burnout3-native/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/u0rhs/burnout3-native/actions/workflows/ci.yml/badge.svg?branch=main"></a>
  <a href="LICENSE"><img alt="License: MIT (the port's own code)" src="https://img.shields.io/badge/license-MIT-2ea043?style=flat-square&labelColor=0d1117"></a>
</p>

<p align="center">
  <a href="https://github.com/u0rhs/burnout3-native/releases"><b>⬇️ Download</b></a> &nbsp;·&nbsp;
  <a href="#get-started"><b>Quick start</b></a> &nbsp;·&nbsp;
  <a href="#screenshots">Screenshots</a> &nbsp;·&nbsp;
  <a href="#features">Features</a> &nbsp;·&nbsp;
  <a href="#controls">Controls</a> &nbsp;·&nbsp;
  <a href="docs/PLAYING.md">Player guide</a> &nbsp;·&nbsp;
  <a href="#faq">FAQ</a> &nbsp;·&nbsp;
  <a href="#reporting-a-bug">Report a bug</a> &nbsp;·&nbsp;
  <a href="#modding">Modding</a> &nbsp;·&nbsp;
  <a href="#contributing">Contributing</a>
</p>

</div>

<!-- stats:start -->
| 🧬 **0** | 🎯 **4,655 / 5,652** | 🔁 **21 / 21** | ⚡ **Uncapped** |
|:-:|:-:|:-:|:-:|
| <sub>chips emulated: no EE, VU, IOP, GS, SPU2 or IPU; the game binary has no interpreter</sub> | <sub>game functions rebuilt byte for byte; the other 997 as complete C, audited against retail</sub> | <sub>test scenarios with no PS2 code at all, identical run to run</sub> | <sub>frame rate: set your own limit; the game's 60 Hz simulation never changes</sub> |
<!-- stats:end -->

> [!IMPORTANT]
> **You need your own copy of the game.** We don't supply the ISO or any game files: the port reads your own
> Burnout 3: Takedown disc (PS2, NTSC-U, SLUS-21050) as a disc image or a folder, and never changes it.

## What this is

Burnout 3: Takedown, the PS2 original, running as a **real PC program**. Its code comes from a matching
decompilation of the game, translated to C++ and built for Windows, Linux and macOS (Apple Silicon), with a platform
layer and a renderer of our own. Nothing pretends to be a PS2 and no PS2 code runs.

### The short version

| | On the PS2 | With this port |
|:--|:--|:--|
| **Physics, AI, takedowns** | The game's own | The same: the PS2's float behaviour is reproduced, and defaults never change gameplay |
| **Picture** | 4:3, fixed frame rate | Any window shape up to ultrawide, a frame rate limit of your choice, MSAA, anisotropic filtering, HD texture packs (and, as a small extra, somewhat sharper textures from your own Xbox disc) |
| **Controls** | The PS2 pad | Keyboard, mouse in the menus and any gamepad, with remapping, rumble and matching button prompts |
| **Extras** | | Launcher, photo mode, your own music in EA Trax, speed units, save files of your own, replays, a tuning console, mods |
| **Loading** | The PS2's load times | Fast loading (on by default) |
| **Online** | Gone with EA's servers | Coming in a future release |

> **Version 1.4.0.** Found a problem? Please [report it](#reporting-a-bug).

## Screenshots

<p align="center">
  <img src="docs/screenshots/race-traffic.jpg" alt="Driving down a mountain-town main street at 76 mph, HD textures" width="49%">
  <img src="docs/screenshots/mountain-boost.jpg" alt="An Instant Boost at 147 mph past the shops of a mountain town" width="49%">
  <img src="docs/screenshots/takedown.jpg" alt="Trading paint with a rival at 187 mph in Road Rage" width="49%">
  <img src="docs/screenshots/impact-time.jpg" alt="A car flipped beside a pole in Crash mode, Impact Time ready" width="49%">
  <img src="docs/screenshots/road-rage.jpg" alt="Road Rage at 200 mph: a Nudge into a rival" width="49%">
  <img src="docs/screenshots/photo-mode.jpg" alt="Photo mode: the on-screen display, a tilted camera and depth of field" width="49%">
</p>

<p align="center"><b>Photo mode</b>: pause, then press P (L3 + R3 on a gamepad) for a free camera, even in the middle of a crash, with field
of view, roll, depth of field and focus. Hide the display for a clean shot; F12 saves the picture without it.</p>
<p align="center">
  <img src="docs/screenshots/photo-mode-crash.jpg" alt="Photo mode in the middle of a pile-up, with the display showing the camera's settings" width="32%">
  <img src="docs/screenshots/photo-mode-telephoto.jpg" alt="Photo mode with a narrow field of view on three cars in a crash" width="32%">
  <img src="docs/screenshots/photo-mode-clean.jpg" alt="A photo mode shot with the display hidden: a car flipping through the air above a pile-up" width="32%">
</p>

<p align="center"><b>Ultrawide</b>: the view widens and the HUD stays at the screen's edges (21:9, then 32:9).</p>
<p align="center">
  <img src="docs/screenshots/ultrawide-21x9.jpg" alt="Road Rage at 21:9" width="98%">
  <img src="docs/screenshots/super-ultrawide-32x9.jpg" alt="The same moment at 32:9" width="98%">
</p>

<p align="center"><b>HD texture packs</b> made for PCSX2 work as they are (Panda_Venom's HD Remaster pack shown).</p>
<p align="center">
  <img src="docs/screenshots/hd-pack-compare.jpg" alt="The original textures next to the HD texture pack" width="98%">
</p>

<p align="center"><b>The launcher, the in-game settings and the tuning console</b></p>
<p align="center">
  <img src="docs/screenshots/launcher-settings.jpg" alt="The launcher's settings" width="49%">
  <img src="docs/screenshots/launcher-showroom.jpg" alt="The launcher's car showroom" width="49%">
  <img src="docs/screenshots/settings-graphics.jpg" alt="The in-game SETTINGS, graphics tab" width="49%">
  <img src="docs/screenshots/settings-extras.jpg" alt="The in-game SETTINGS, extras tab: everything the port adds, each with its own switch" width="49%">
  <img src="docs/screenshots/tuning-console.jpg" alt="The F9 tuning console over a race" width="49%">
</p>

<sub>Rendered by the port at 2560x1440 with 4x MSAA and the HD texture pack; the game's own assets come from the
player's disc and are shown here only to illustrate the port.</sub>

<a id="get-started"></a>
## Get started

You need two things: a download from the [Releases page](https://github.com/u0rhs/burnout3-native/releases) (a
Windows zip, a Linux tarball or a macOS `.dmg`), and your own disc as an `.iso` / `.bin` / `.chd` image or a folder of
its files ([how to make one](docs/PLAYING.md#2-making-a-disc-image-of-your-own-disc)). There is nothing to install,
compile or convert: the download is the finished game with its shaders built in, and the launcher only needs to know
where your disc is.

<details open>
<summary><b>🪟 Windows 10 / 11</b></summary>
<br>

1. Unzip the download into a folder of its own and keep its files together.
2. Start **`Burnout3Launcher.exe`**. Windows may warn about an unknown publisher (the build is not signed): choose
   **More info**, then **Run anyway**.
3. On the **Game Data** page choose your disc image or folder, once. Then press **START GAME** on the **Play** page.

</details>

<details>
<summary><b>🐧 Linux x86-64 (and Steam Deck)</b></summary>
<br>

1. Unpack the tarball into a folder of its own: `mkdir -p ~/Games && tar -xzf burnout3-native-*-linux-x86_64.tar.gz -C ~/Games`
2. Run **`./b3launcher`** from that folder. Optionally run `./add-to-menu.sh` once for an application-menu entry.
3. **Game Data** page, then **START GAME**.

It ships its own SDL 3; you need a Vulkan driver and glibc 2.35 or newer.
**Steam Deck:** in Desktop Mode add `b3launcher` to Steam as a non-Steam game (leave Proton off) and it runs in
Gaming Mode with the Deck's controls alone. This is set up but not yet tried on a Deck:
[the steps](docs/PLAYING.md#linux).

</details>

<details>
<summary><b>🍎 macOS (Apple Silicon)</b></summary>
<br>

1. Open the `.dmg` and drag **Burnout 3 Native** into **Applications**.
2. The app is not notarized by Apple, so the first start is blocked: open it once, then go to **System Settings >
   Privacy & Security** and press **Open Anyway**. ([All the variations](docs/PLAYING.md#mac-apple-silicon), and
   [building from source instead](docs/mac.md).)
3. **Game Data** page (choose the folder with your disc's files), then **START GAME**.

</details>

Stuck? Try the [FAQ](#faq), the [player guide](docs/PLAYING.md) or the [known issues](docs/known-issues.md).

<details>
<summary><b>Build from source</b></summary>
<br>

The generated game code is committed, so the repository builds on its own. Full steps:
[docs/BUILDING.md](docs/BUILDING.md).

```sh
make && build/b3launcher                       # Linux: the game and the launcher (GCC 13+, SDL 3, Vulkan)
tools/mac/native.sh <your disc folder>         # macOS, Apple Silicon or Intel (docs/mac.md)
RENDER=1 tools/native/build_win.sh             # Windows, cross-built from WSL with a pinned MSYS2 toolchain
make test                                      # the unit tests
make modtool                                   # b3modtool, the mod kit's tool
```

</details>

<a id="platforms"></a>
## Requirements and tested platforms

| | Windows | Linux | macOS |
|:--|:--|:--|:--|
| **System** | Windows 10 / 11, 64-bit | x86-64, glibc 2.35+ (Ubuntu 22.04+, Debian 12, Fedora 36+, SteamOS 3 ...) | Apple Silicon (M1 or later); Intel (x86-64) from source, macOS 15+ |
| **Graphics** | A GPU and driver with Vulkan 1.0 or newer | A driver with Vulkan 1.0 or newer; X11 or Wayland | Metal (built in) |
| **Package** | Zip with the launcher | Tarball with its own SDL 3 | `.dmg` with the launcher app |
| **Tested** | ✅ Played on a high-end desktop; the package passes its smoke test | ✅ The package passes its smoke test and was played on one desktop; other distributions and the Steam Deck: untested | 🚧 Played on an M1 when built from source; the `.dmg` is built by CI, and a build of it has been installed and run on a real Mac. Intel: played on one Intel Mac when built from source ([docs/mac.md §11](docs/mac.md#11-intel-macs-x86-64-status-macintel1-2026-11-18)) |

Windows on ARM and Linux on ARM are not supported. Intel Macs build from source, with no download yet. The exact
minimum CPU, memory and graphics card are **not measured**: a slow PC can fall behind real time (see
[known issues](docs/known-issues.md)), and the **Low** preset or safe mode helps. The download is small (about 23 MB
for the test builds); your disc stays where it is.

## Controls

Keyboard, mouse in the menus and any gamepad your system knows. A gamepad gets the layout of its own console
(**CONTROLLER LAYOUT: Auto**): a PlayStation controller the PS2 game's, every other controller the original Xbox
game's. Everything is remappable per player (SETTINGS > CONTROLS > BINDINGS, or the launcher's Controllers page); a
second gamepad is player 2.

| | Keyboard | Xbox layout | PlayStation layout |
|:--|:--|:--|:--|
| Accelerate / brake | W / S or Up / Down | Right / left trigger | Cross or R2 / Square or L2 |
| Steer | A / D or Left / Right | Left stick or D-pad | Left stick or D-pad |
| Boost | Space, Shift or R | A (RB too) | R1 |
| Crashbreaker | E | B | Press the right stick (R3) |
| Camera / look back | Backspace or C / Q | Y / X (LB too) | Triangle / L1 |
| Pause | Escape | Start / Menu | Start / Options |

On the Xbox layout A and B do one thing while you drive and another in the menus (confirm and back), as in the
original Xbox game. The full tables are in [docs/PLAYING.md](docs/PLAYING.md#5-controls).

Handy keys: **F11** fullscreen, **F12** screenshot, **F10** bug report, **P** photo mode (pad: L3 + R3), **F9** tuning
console (opt-in). All defaults, the menus' keys and two-player setup: [docs/PLAYING.md](docs/PLAYING.md#5-controls).

## Settings tour

Open **SETTINGS** from the game's main menu or pause menu, or the launcher's Settings page; changes apply at once
unless a row says "next start". With defaults the game plays like the PS2 original; the few settings that change what
the game sees are marked ⚙️ in the feature list and start off.

| Where | What you can do |
|:--|:--|
| **DISPLAY** | Fullscreen, vsync, frame rate limit, battery saver, HUD and menu layout for wide screens, split-screen layout, field of view 60 to 120, camera distance and height, interpolation |
| **GRAPHICS / EFFECTS** | Quality preset (PS2 original to Ultra), anti-aliasing up to 8x, texture filtering up to 16x, render scale, draw distance, shadows, night, bloom, motion blur |
| **CAR** | Custom paint and finish for both players, car detail, car reflections, garage camera |
| **CONTROLS** | Rumble, adaptive triggers, analog triggers, boost toggle, instant restart, remapping per player, gyro |
| **AUDIO** | Your music folder and playlist |
| **GAME** | Button prompts, FPS counter, language, speed units, ghosts, speedrun timer, replays, accessibility (reduced flashes, colour-blind filters, HUD size), fast loading |
| **EXTRAS** | What the port adds to the game itself, each with its own switch: Eliminator, race options, more rivals, more car series, traffic checking, mirror mode, party modes, race replays, and night and rain (experimental) |
| **Launcher** | HD texture pack, your music folder, mods, saves, game data, showroom, update check |

Details and every opt-in: [player guide, section 7](docs/PLAYING.md#7-settings-worth-knowing).
**Photo mode:** pause a race (even mid-crash), choose **PHOTO MODE** in the pause menu (or press **P**), and fly a free camera with the HUD off and optional depth
of field.


## Features

Everything on top of the faithful port is a setting, most of them on a **SETTINGS** page added to the game's own
main menu, in the game's fonts ([docs/pc-settings.md](docs/pc-settings.md)). **Defaults never change gameplay.** What the
port adds to the game itself (new modes, SELECT OPTIONS rows, car series, party modes) is on its **EXTRAS** page, each
part with its own switch, all off until you turn it on but ELIMINATOR, plus night and rain as experimental options ([player guide](docs/PLAYING.md#extras-settings--extras)).

<p align="center">
✅ in the build &nbsp;·&nbsp; 🚧 built, being checked &nbsp;·&nbsp; 📋 planned &nbsp;·&nbsp; ⚙️ opt-in: off by default, because it changes the game's input, what the game sees or how it plays (the gameplay ones are on the EXTRAS page and keep their races' results apart)
</p>

<details>
<summary><b>The full feature list</b> (graphics, launcher, controls, music, play, modding, online)</summary>
<br>

### 🎮 Graphics

| | Feature | |
|:-:|:--|:--|
| ✅ | **Uncapped frame rate** | A frame limit of your choice; frames interpolated between the game's fixed simulation steps, which never change |
| ✅ | **Widescreen and ultrawide** | HUD and menus anchored to the screen's edges; 4:3 movies with black or blurred sides |
| ✅ | **Field of view** | 60 to 120 (90 is the game's), with FOV correction |
| ✅ | **Whole-track draw distance** | No streaming pop-in; traffic fades in before the game's draw limit |
| ✅ | **Image quality** | MSAA up to 8x, anisotropic filtering up to 16x, car detail, shadows, motion blur, boost blur, bloom |
| ✅ | **HD texture packs** | PCSX2-format replacement packs, picked from a folder dialog; the launcher links to Panda_Venom's HD Remaster pack; converted textures cached on disk, so tracks load faster after the first time |
| ✅ | **Quality presets** | PS2 original, Low, Medium, High, Ultra |
| ✅ | **Photo mode** | Pause, then press P (L3 + R3 on a pad), even mid-crash: free camera, HUD off, depth of field, screenshots |
| ✅ | **Split-screen layout** | Two players top / bottom (the game's) or side by side, each view at its own shape with its own HUD |
| ✅ | **Car reflections that reach further** | REFLECTION DISTANCE up to 300 m, the roadside props and other cars in the paint, a sharper reflection |
| ✅ | **Sun and prop shadows** | Real shadows of the cars on the road, and optionally of lamp posts, signs and barriers |
| ✅ | **Glowing tunnel lights** | Tunnel fittings glow by day (TUNNEL LIGHTS), or as on the Xbox |
| ✅ | **Camera extras** | Look around your car in a race (right stick or mouse), CAMERA ROLL into corners, FINISH CAMERA set pieces, CRASH CAMERA off |
| ✅ | **Your own colours** | A paint panel on SELECT CAR (body, decals, wheels, boost glow, colour picker, Chrome finish); the car list's pictures drawn in your paint |
| ✅ | **Direct3D 12 on Windows** | GRAPHICS API Auto, Vulkan or D3D12 |
| ✅ | **HD packs in PNG and BC7** | PNG and BC7 replacement files load, with HD TEXTURE MEMORY to keep large packs in check |
| 🚧 | **Night and rain** ⚙️ | Lit lamps, signs and windows at night; rain, wet roads, puddles and spray. **Experimental: expect bugs, it isn't perfect yet.** Feedback, suggestions and help are very welcome (EXTRAS) |

### 🧭 Front end & launcher

| | Feature | |
|:-:|:--|:--|
| ✅ | **SETTINGS in the game** | Display, graphics, effects, camera, interface, music and saves, beside the game's own menus (plus QUIT GAME) |
| ✅ | **Launcher** | Play, Settings, HD Textures, Music, Mods, Game Data, Saves, Stats, Showroom, Replays, Controllers, Credits |
| ✅ | **Any input in the launcher** | Fully usable with the keyboard alone, a gamepad alone or the mouse alone |
| ✅ | **Fast loading** | Loading screens at full speed with the game's timeline unchanged (a crash event: 18.3 s → 3.1 s); picture and sound start together |
| ✅ | **Skip intro movies** | Start or Cross skips a movie, never automatically; a small hint shows how |
| ✅ | **Saves of your own** | Up to five `.sav` files, each with the game's three profiles and backups; import PS2 saves (`.psu`, `.max`, PCSX2 cards) |
| ✅ | **Stats and achievements** | Your career read from your save, and 13 local achievements on the launcher's Stats page |
| ✅ | **Car showroom** | Every car on your disc in 3D on a turntable, by series (traffic vehicles and your own cars too), with the game's own names, in your HD texture pack and mods' packs |
| ✅ | **PRESS START at once** | The title takes Start as soon as it shows; START AT can skip the films or go straight to your profile |
| ✅ | **Car stats** | GRIP and ACCELERATION beside the game's two on the car select |
| ✅ | **Skip the launcher** | Start the game directly next time; hold Shift or a controller's Back / Select to get the launcher back |
| ✅ | **Updates** | Each time it starts, the launcher asks GitHub if a newer release is out; **Install update** downloads it, checks its signature and installs it; can be turned off |
| ✅ | **Safe mode** | After two unclean exits the game starts with safe settings, and says so |
| ✅ | **Bug reports** | F10 makes one zip: logs, settings, system info and a recording that replays the session; your user name kept out of it |
| 📋 | **More than three profiles** | The game's profile list extended |

### 🕹️ Controls & accessibility

| | Feature | |
|:-:|:--|:--|
| ✅ | **Keyboard, mouse and gamepads** | The keyboard or any SDL gamepad as the PS2 pad; the mouse in the menus; type your profile name |
| ✅ | **Speed units** | MPH or KM/H on the speedometer, distance messages, records and stats, in the game's own words |
| ✅ | **Matching button prompts** | PlayStation, Xbox, Nintendo, generic or key caps, following the last device you used |
| ✅ | **Remapping** | Per player, pads and keyboard, in SETTINGS and the launcher; conflicts offer swap, clear or keep both |
| ✅ | **Local 2-player** | A second gamepad is player 2 |
| ✅ | **Rumble** ⚙️ | The game's own vibration on your pads, with a strength slider and trigger rumble on Xbox pads |
| ✅ | **DualSense adaptive triggers, gyro steering** ⚙️ | Triggers that resist, buzz while boosting and kick at a crash; steer by tilting or turning the pad, per player |
| ✅ | **Car series on LB / RB** | On the car select the shoulder buttons change the series, with their glyphs beside the arrows |
| ✅ | **HUD size** | The race HUD at 100, 115, 130 or 150 %, each part grown from its own corner and kept on screen |
| ✅ | **Manual gears** ⚙️ | The game's own manual gearbox, with SHIFT UP / DOWN keys and paddles (SELECT OPTIONS: TRANSMISSION) |
| ✅ | **Accessibility** | Reduced screen flashes, colour-blind HUD filters (deuteranopia, protanopia, tritanopia), boost toggle ⚙️ or hold |

### 🎵 Music & sound

| | Feature | |
|:-:|:--|:--|
| ✅ | **Your own music in EA Trax** | A folder of your songs: EA Trax only, yours only, or mixed; the game's radio plays them |
| ✅ | **Now-playing popup** | The EA Trax display shows your song's title, artist and album |
| ✅ | **Other music apps on Linux** ⚙️ | Whatever Spotify, a browser or any player is playing shows in the EA Trax popup (MPRIS) |
| ✅ | **Even loudness** | Your songs from different sources played at the same loudness |
| ✅ | **Cleaner sound** | SOUND OUTPUT Clean (the default) evens out loud moments instead of clipping them; Original keeps the PS2's sound |
| ✅ | **Takedown streak pitch** | Each takedown after your 10th since your last crash sounds higher, as in World Tour (off by default) |
| 🚧 | **Other music apps on Windows** ⚙️ | The same through Windows media sessions: built, its desktop check pending (macOS has no public interface for it) |

### 🏁 Play

| | Feature | |
|:-:|:--|:--|
| ✅ | **Replay files** | Whole sessions as tiny files (a 6.5-minute race: ~80 KB) that play back exactly |
| ✅ | **Time Attack ghosts** ⚙️ | Race your best lap |
| ✅ | **Speedrun timer** | Game time from the start signal in the HUD's own digits, with splits and bests per event and car |
| ✅ | **Instant restart** ⚙️ | F5 or Select + Start restarts the event through the game's own pause menu |
| ✅ | **Eliminator in Single Event** | World Tour's Eliminator on any track (on by default), and TIMED ELIMINATION ⚙️ (last car out every 24 s) |
| ✅ | **Race options** ⚙️ | Single Event's SELECT OPTIONS: traffic amount and pattern, laps, rivals and their cars, standing start with BOOST START, rival style, time limit, Burnout points |
| ✅ | **Up to eight cars** ⚙️ | RACE's RIVALS up to 7 |
| ✅ | **More cars to drive** ⚙️ | The Heavyweights in Single Event, every traffic vehicle (cars, taxis, the tuk tuk, buses, big rigs), your car mods as a CUSTOM series and the cut prototype cars as BETA |
| ✅ | **Mirror mode** ⚙️ | Single Event and two-player races on mirrored tracks |
| ✅ | **Crashbreaker everywhere** ⚙️ | Blow up your wreck in Race, Time Attack and Road Rage |
| ✅ | **Traffic checking** ⚙️ | Burnout Revenge's traffic checks (Revenge) or any traffic shoved aside (Extreme), with chains and shots |
| ✅ | **Deformation and loose parts** ⚙️ | How much cars crumple (None to 200 %); every loose part can come off in Road Rage |
| ✅ | **Party modes** ⚙️ | Crazy AI physics, moon jump, crash on demand, the AI driving your car, kamikaze traffic, tuk-tuks for everyone |
| ✅ | **Race replay** ⚙️ | Watch the race you just drove from its results page, with your choice of camera |
| ✅ | **Car viewer** ⚙️ | The game's own car viewer in YOUR GARAGE |
| ✅ | **HUD extras** | RPM meter, boost bar shake, rival names and distances over their cars, the rivals' turn signals (off by default) |

### 🛠️ Modding

| | Feature | |
|:-:|:--|:--|
| ✅ | **Mod loader** | Mod folders or `.b3mod` zips; load order, on / off, conflicts and requirements on the launcher's Mods page |
| ✅ | **Patch mods** | Small `.b3patch` deltas applied to the player's own disc: mods never carry the game's files |
| ✅ | **Per-mod HD textures** | A mod can bring its own HD texture pack |
| ✅ | **`b3modtool`** | Textures ↔ PNG, text ↔ text files, audio ↔ WAV, tuning ↔ text: lossless round trips on the whole disc |
| ✅ | **File-format reference** | All 775 files on the disc documented; the game's 705 tuning variables listed |
| ✅ | **Code-mod SDK** | A versioned C API: hook or replace any game function, add settings rows and overlays; templates for all three platforms |
| ✅ | **Lua scripting** | Sandboxed and deterministic, so replays still play back exactly: modes, rules and HUDs without a compiler |
| ✅ | **Tuning console** | The game's own tuning variables, live in a race (F9): tabs and sections, a your-car page, favourites and search; presets as mods; [what every value does](docs/modding/tuning-options.md) |
| ✅ | **Car and track export** | Every car and route to glTF / OBJ, with textures and paints |
| ✅ | **Car import** | Edited cars back into the game as patch mods; unedited, all 67 cars come back byte for byte |
| ✅ | **Track import** | Edited routes back as patch mods; unedited, all 37 routes come back byte for byte (visual only: cars drive on the original surface) |
| ✅ | **Blender round trip** | An add-on imports a car from your disc and exports a mod in a few clicks; routes go through Blender's own glTF add-on |
| ✅ | **Ten tutorials** | From replacing a texture to editing a track in Blender |
| ✅ | **Car mods as extra cars** | Car mods and car packs as cars of their own in a CUSTOM series (up to 200), never in place of the game's; car mods from older builds load too |
| 📋 | **More** | New events and crash junctions; sky import |

### 🌐 Online

| | Feature | |
|:-:|:--|:--|
| 🚧 | **Online play** | EA's servers are long gone; online play is coming in a future release |


</details>

## How it works

- 🧩 **Game code**: Criterion's code from the matching decompilation, translated to portable C++ (`gen/native`)
  and compiled ahead of time. No JIT and no interpreter in the game binary; the VU0 micro programs are native too. (The
  renderer has one small VU1 interpreter for the game's particle graphics program: smoke, dust, glass, fire.)
- 🖥️ **Platform layer**: what an operating system gives a game, written from our own specs
  ([docs/specs/](docs/specs/)): threads and timers on a virtual clock, files, pads, saves, and an MPEG-2 decoder and
  sound mixer of our own.
- 🎨 **Renderer**: reads each frame's scene from the game's memory and draws it with SDL3 GPU, in the same process:
  Vulkan on Windows and Linux, Metal on macOS.
- 🧮 **PS2 maths**: the Emotion Engine's own float behaviour is reproduced, so the physics gives the PS2's results; the
  game's data keeps its PS2 layout inside a 64-bit process.
- 🔁 **Proof**: 20 scripted scenarios cover every race mode and front-end path with no PS2 code allowed, and every
  change must leave their timeline, logs, memory and screenshots identical.

The full story, from the decompilation to the frame on your screen: **[docs/TECHNICAL.md](docs/TECHNICAL.md)**.
The checks a change must pass: [CONTRIBUTING.md](CONTRIBUTING.md#checks-a-change-must-pass).

## Modding

Full modding freedom, without ever sharing the game's own files: mods ship patches and their own files, applied to
each player's own disc.

- 🚀 **[Getting started](docs/modding/getting-started.md)**: the mod format, the tools and your first mod.
- 📚 **[Tutorials](docs/modding/tutorials/)**: ten step-by-step guides, from a new texture to a car or a track
  edited in Blender.
- 🧰 **[b3modtool](docs/modding/b3modtool.md)**: every asset to an editable file and back.
- 🧑‍💻 **[Code-mod SDK](docs/modding/sdk-reference.md)** and **[Lua](docs/modding/lua-reference.md)**: hooks,
  settings rows, overlays and per-frame events.
- 🗂️ **[File formats](docs/modding/file-formats/)**: every file on the disc, documented.

Everything else is in the **[modding guide](docs/modding/README.md)**.

## FAQ

<details>
<summary><b>Is this an emulator?</b></summary>
<br>

No. Nothing emulates a PS2 chip and no PS2 code runs: the game's own code was decompiled, translated to C++ and
compiled for your PC, like any other PC game.
</details>

<details>
<summary><b>How is this different from other PC versions?</b></summary>
<br>

Other PC versions are recompilations: a console release's program translated as it is. This port is built from a
decompilation of the PS2 game, its code rebuilt as source, so every function of the game runs as native code, and the
PC parts (renderer, sound, input, saves) are written for the PC.
</details>

<details>
<summary><b>What do I need to play?</b></summary>
<br>

Your own copy of Burnout 3: Takedown for the PS2, the North American release (NTSC-U, SLUS-21050), as a disc image
(`.iso`, `.bin` or `.chd`) or as the disc's files in a folder. Other regions do not run, but if you also own a European
disc, **IMPORT LANGUAGES** (Settings > GAME > LANGUAGE AND UNITS, in the launcher) copies its languages into the US game. We don't supply the
ISO or any game files; the [player guide](docs/PLAYING.md#2-making-a-disc-image-of-your-own-disc) shows how to make an
image of your own disc.
</details>

<details>
<summary><b>What PC do I need?</b></summary>
<br>

On Windows and Linux, any graphics card with a **Vulkan 1.0** driver (no newer Vulkan version is needed). On Windows,
**Direct3D 12** works too (GRAPHICS API in the settings; it is also used by itself when no working Vulkan device can be created). There is no
OpenGL path, so on Linux a GPU without any Vulkan driver cannot run it. On a
Mac, an Apple Silicon Mac (Metal). The minimum CPU and memory are not measured yet; see
[Requirements](#platforms) and the [player guide](docs/PLAYING.md#your-pc).
</details>

<details>
<summary><b>Was AI used to make it?</b></summary>
<br>

Yes, AI assistance was used in developing the port. Every change, however it was written, has to pass the same
checks: the 20 scripted scenarios must give byte-identical results, so the game still plays exactly as on the PS2.
</details>

<details>
<summary><b>Does it play like the original?</b></summary>
<br>

Yes. It is the game's own code, with the PS2's float behaviour reproduced, so physics and gameplay match the PS2.
Anything that would change what the game sees is marked ⚙️ in the feature list and stays off until you turn it on.
</details>

<details>
<summary><b>Do I have to compile anything, or wait for shaders?</b></summary>
<br>

No. A release download is the finished program, with its shaders built in: no first-run shader compilation, no
recompiling and no disc conversion. Building from source is only for people who want to change the code.
</details>

<details>
<summary><b>Where do I get HD textures?</b></summary>
<br>

The launcher's **HD Textures** page takes any PCSX2-format texture pack and links to Panda_Venom's HD Remaster pack
(see the credits below). The pack is never bundled with the port.
</details>

<details>
<summary><b>Can I play online?</b></summary>
<br>

Not yet. EA's servers are long gone, and online play is coming in a future release. Picking ONLINE shows
"Online play is coming soon."
</details>

<details>
<summary><b>Does it run on the Steam Deck?</b></summary>
<br>

The Linux build is set up for it: add `b3launcher` to Steam as a non-Steam game and it runs in Gaming Mode
([the steps](docs/PLAYING.md#linux)). It has not been tried on a Deck yet, so reports from Deck owners are very
welcome.
</details>

<details>
<summary><b>The window closes at once, or there is no picture</b></summary>
<br>

Most often the graphics driver has no Vulkan. On Windows install the newest driver from your graphics card maker; on
Linux check `vulkaninfo --summary` and install your distribution's Vulkan loader and driver (NVIDIA needs its own
driver, not nouveau). The [troubleshooting table](docs/PLAYING.md#common-problems) has the package names. Then
[make a bug report](#reporting-a-bug).
</details>

<details>
<summary><b>It says the disc is not the US game, or the folder is wrong</b></summary>
<br>

The port runs only the US disc (SLUS-21050). Point the launcher at the `.iso`, or at the folder that directly holds
`SLUS_210.50`, `DATA`, `TRACKS` and the rest. More messages and fixes: the
[troubleshooting table](docs/PLAYING.md#common-problems).
</details>

<details>
<summary><b>The race stutters or falls behind</b></summary>
<br>

Lower the **QUALITY PRESET**, set a **FRAME RATE LIMIT**, or start in **safe mode** from the launcher's Play page
(low settings, a window, no HD textures; after two unclean exits the game does it by itself). If it helps, send a bug
report: it tells us what your PC needed.
</details>

<details>
<summary><b>The Mac says the app is blocked or damaged</b></summary>
<br>

The app is not notarized, so macOS asks for one extra step the first time: **System Settings > Privacy & Security >
Open Anyway**, or run `xattr -dr com.apple.quarantine "/Applications/Burnout 3 Native.app"` once in Terminal. See the
[Mac steps](docs/PLAYING.md#mac-apple-silicon).
</details>

<details>
<summary><b>Where are my saves and settings?</b></summary>
<br>

In your user data folder (`%APPDATA%\b3port\native` on Windows, `~/.local/share/b3port/native` on Linux,
`~/Library/Application Support/b3port/native` on a Mac), never in the game folder, unless you turn on **portable
mode** (Windows and Linux: everything in an `appdata` folder beside the launcher, for a USB stick); the launcher's
**Saves** page opens it and can export a save or
import PS2 saves (`.psu`, `.max`, PCSX2 cards). It can also keep your saves in a folder you choose, such as a
OneDrive, Google Drive or Dropbox folder or a NAS, so two computers (a PC and a Mac) share them. Details:
[player guide, section 6](docs/PLAYING.md#6-saves).
</details>

<details>
<summary><b>Can I bring my PCSX2 saves?</b></summary>
<br>

Yes. On the launcher's **Saves** page press **Import into file N...** and choose PCSX2's memory card, usually
`Mcd001.ps2` in PCSX2's `memcards` folder (the import starts there when it finds it). Folder memory cards and `.psu` /
`.max` saves work too. In the game, SETTINGS > SAVES > IMPORT SAVE does the same.
</details>

<a id="reporting-a-bug"></a>
## Reporting a bug

1. Note what happened and roughly when (the mode, track or menu).
2. Press **F10** in the game, or use **Bug report...** on the launcher's Play page. You get one
   `bug-report-<date-time>.zip` on your Desktop, with the logs and a recording of your controls that replays the
   session exactly, your settings, the version and a short system description. It holds no game data and no save
   (unless you choose to include it), and your user name is taken out of the paths in it.
3. [Open an issue](https://github.com/u0rhs/burnout3-native/issues/new/choose) with your note and the zip. If you
   crashed, make the report afterwards: the crashed run's log is in it.

Nothing is ever sent by itself: you send the report. More: [player guide, section 9](docs/PLAYING.md#9-troubleshooting-and-bug-reports).

## What's next

- **Online play**, coming in a future release.
- **Night and rain**, finished and faster: they are in this version as experimental options (SETTINGS > EXTRAS), for
  your feedback.
- **More than three profiles** per save, **new events and crash junctions**, and **sky import** for mods.
- **Water reflections**, after the car reflections.
- The now-playing display for **other music apps on Windows** is built and waits for a desktop check.
- Your reports from other machines: Linux distributions, the Steam Deck and more Macs are the least tested.

The [known issues](docs/known-issues.md) list what is not right yet.



## Contributing

Bug reports, fixes, features, mods and documentation are all welcome. [CONTRIBUTING.md](CONTRIBUTING.md) explains
how to build on each platform, the checks a change must pass and how to report a bug; please also read the
[code of conduct](CODE_OF_CONDUCT.md). Every push and pull request is built and tested on Linux and macOS.

Two rules above all: **no game data** in the repository or in mods, and **defaults never change gameplay**.
Developer reading: [how it works](docs/TECHNICAL.md), [building](docs/BUILDING.md), the [modding guide](docs/modding/README.md).

<details>
<summary><b>Repository layout</b></summary>
<br>

| Path | Contents |
|---|---|
| `gen/native` | The generated game code (committed: the build needs no decompilation checkout) |
| `src/native` | The platform layer: kernel and threads, files, pads, memory card and saves, sound, movies, VU0 |
| `src/libs` | RenderWare, RW Audio, SCE and lg libraries as C |
| `src/b3replay`, `src/b3gfx`, `src/b3asset`, `src/gameview` | The renderer, the SDL3 GPU layer, the asset decoders, the scene capture |
| `src/b3settings`, `src/b3launcher`, `src/b3report` | the SETTINGS page, the launcher, bug reports and run logs |
| `src/b3mod`, `src/b3modtool`, `sdk/` | The mod loader, the mod kit's asset tool, the code-mod SDK |
| `include/b3host`, `include/gtmath`, `include/b3libm` | The host adapter, GtMath, the bit-exact PS2 libm |
| `tools/native`, `tools/golden` | `native_play`, the scenario runs, the golden recording and compare |
| `tools/release`, `tools/mac` | The Windows and Linux packages; the Mac build |
| `tools/modding`, `examples/mods` | Mod kit tools and examples |
| `tools/decomp2host` | The decompilation-to-C++ generator |
| `tests/` | Unit, ABI and exactness tests (`make test`) |
| `docs/` | The player guide, known issues, PC settings, modding, specifications, design notes |

</details>

## License

The port's own code (platform layer, renderer, launcher, tools, mod kit and SDK, tests, docs) is released under the
[MIT licence](LICENSE). It does not cover the game: no game data is in this repository, and `gen/` (code generated
from a decompilation of the game) and `src/libs/` remain the property of the game's owners. Third-party components
keep their own licences. Details: [LICENSING.md](LICENSING.md).

## Acknowledgements

Thank you to everyone whose work this port stands on.

- [Hidden Palace](https://hiddenpalace.org/) for preserving the Burnout prototype disc images. Their debug builds,
  link maps and symbols give the decomp its names, and through it this port's.
- [The Cutting Room Floor](https://tcrf.net/) for their research into those prototypes and what they contain.
- [Nahelam](https://github.com/Nahelam) ([PS2-Game-Mods](https://github.com/Nahelam/PS2-Game-Mods), [B3Mod](https://github.com/Nahelam/B3Mod))
  for the most extensive public reverse engineering of the retail game and its function names, and for the PS2 mods
  that inspired MIRROR MODE, CRASHBREAKER IN EVERY MODE, the PARTY MODES, and the race options: the rivals' names and
  distances, the rivals' cars, the standing start, the Road Rage time limit and rival style, Burnout points, traffic
  that changes each race, DEFORMATION, the Chrome finish and the crash camera switch. Also START AT, the idle pause
  and long-event switches, the Crashbreaker's full blast and the car viewer. Also the replay viewer. The ideas only,
  none of their code.
- [Takedawn](https://github.com/xenophobentx/takedawn) (xenophobentx), [sp00nznet/burnout3](https://github.com/sp00nznet/burnout3)
  with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp), and [burnout3-sw](https://github.com/antoxa2584x/burnout3-sw)
  for their research notes on the Xbox version (race clock, crash cameras, effects, HUD, track formats).
- [Panda_Venom](https://gbatemp.net/threads/burnout-3-takedown-usa-hd-remaster.621068/) for the Burnout 3: Takedown
  USA HD Remaster texture pack ([download](https://www.mediafire.com/file/gec85ndkk6b4wm4/SLUS-21050.7z/file),
  [support them on Ko-fi](https://ko-fi.com/pandavenom)), which the launcher's HD Textures page links to.
- [ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) and
  [Aero_'s PCSX2 patches](https://github.com/AeroWidescreen/PCSX2-Cheats) for locating the widescreen and HUD code.
- bryan (Braia), for the PCSX2 mods that inspired:
  - the HUD: the RPM meter, the boost bar shake and the takedown streak pitch;
  - the cameras: camera roll and the finish camera's set pieces;
  - play: manual gears, TIMED ELIMINATION, the traffic presets, traffic checking in two-player races and loose parts
    in Road Rage;
  - the car select: car stats, the rivals' turn signals, the paint picker and painted car pictures;
  - the reflections that reach further.

  The ideas only, none of their code.
- escape209, for the mod that put the Heavyweights in the garage, the idea behind the Heavyweights in Single Event.
- Fresh_Burnout3, for the traffic-checking edit behind TRAFFIC CHECK (2 PLAYERS) Extreme.
- KaylinMajeure, for the Traffic Checking mod restoring Burnout Revenge's code, the reference for
  TRAFFIC CHECK (2 PLAYERS) Revenge.
- Remco (SuperType1), for the Revenge-style orbit camera mod behind the orbit camera's feel (stopping at walls, its
  easing).
- [librw](https://github.com/aap/librw) (aap) as a reference for RenderWare's PS2 rasters and geometry packets.
- [Burn3](https://github.com/UwUCammie/Burn3) (UwUCammie), [PS2Recomp](https://github.com/ran-j/PS2Recomp) (ran-j) and
  [burnout3-pc](https://github.com/danielgomesvieira2000/burnout3-pc): other PC projects for the game.
- [PCSX2](https://pcsx2.net/) for its texture-pack and memory-card formats, which the port reads, and
  [mymcplus](https://github.com/thestr4ng3r/mymcplus) for PS2 save tooling.
- [splat](https://github.com/ethteck/splat) and [decomp.me](https://decomp.me) for the decompilation tooling.
- The PS2 homebrew documentation community ([ps2tek](https://psi-rockin.github.io/ps2tek/), [PS2SDK](https://github.com/ps2dev/ps2sdk))
  for hardware references.

<br>

<p align="center"><sub>Burnout 3: Takedown, its code, data, names and logo belong to their owners.</sub></p>
