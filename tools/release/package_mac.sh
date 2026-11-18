#!/bin/bash
# package_mac.sh -- the Mac test build for testers, as one disk image (macOS on Apple Silicon, or on an Intel Mac:
# macintel1, the CPU of the Mac it runs on):
#   build/release/burnout3-native-<version>-macos-<arm64|x86_64>.dmg ($REL, default build/release): a compressed (UDZO) image
#   whose window holds the standard drag-to-install pair:
#     Burnout 3 Native.app
#       Contents/Info.plist  Contents/PkgInfo
#       Contents/MacOS/      b3launcher (the app's executable: players always start through the launcher)
#                            native_play  libb3render.so  libSDL3.0.dylib  b3modtool
#                            (the launcher starts native_play from its own folder; native_play loads libb3render.so
#                            from its own; SDL3 is found beside them through @rpath = @executable_path / @loader_path)
#       Contents/Resources/  b3.icns  b3-icon.png  b3_car_tools.zip (the Blender add-on, tools/blender/make_addon_zip.py)
#                            README-TESTERS.txt  KNOWN-ISSUES.txt  VERSION.txt
#                            LICENSES/ (THIRD-PARTY-NOTICES.txt and the license texts: SDL3, stb_truetype, dr_libs,
#                                       minimp3, stb_vorbis, Lua, libchdr with LZMA SDK / miniz / zstd)
#       Contents/_CodeSignature/ (ad-hoc: codesign -s -, so Apple Silicon runs it (Intel does not need it); not notarized)
#     Applications -> /Applications
#   No developer tools, no game data, no saves. The user data folder is ~/Library/Application Support/b3port/native.
#
#   tools/release/package_mac.sh          builds first (Apple clang, Homebrew SDL3, GNU make 4 as gmake: brew install
#                                         sdl3 pkg-config make) from the committed native bundle (NATIVE_FROM=bundle,
#                                         as tools/mac/native.sh): native_play, libb3render.so, b3launcher, b3modtool
#                                         into build/mac-release (BUILD_DIR; JOBS, default all cores)
#   OUT=<build dir> tools/release/package_mac.sh
#                                         packages a finished build of those four (the same SDL3 it linked: SDL3_DIR)
#   SMOKE=1                               then opens the image, copies the app out of it as a player would, and runs it
#                                         from there: the signature check, the launcher's screenshot mode (offscreen,
#                                         every walk check PASS, SDL3 loaded from inside the app) and, with a disc
#                                         (B3_GAME_DIR, and SAVES: a memory card folder with a profile), the strict race
#                                         check (tools/native/race_check.sh STRICT=1: no fallback to retail code, VU0
#                                         all generated) with the app's own native_play
#   DRY_RUN=1                             anywhere (also Linux): no build, placeholder programs; writes the bundle's
#                                         folders, Info.plist and texts into $REL/stage-mac, runs the privacy scan,
#                                         and prints the Mac-only commands (install_name_tool, codesign, hdiutil, ...)
#                                         instead of running them
# Other settings: SDL3_DIR=<prefix> (default: pkg-config's / brew --prefix sdl3; its lib/libSDL3.0.dylib is the one
# bundled), MACOS_MIN=<version> (the deployment target; default: the bundled SDL3's own minimum, which the app cannot
# go below -- Homebrew's SDL3 is built for the macOS its bottle is for), BUNDLE_ID (default io.github.u0rhs.b3port),
# DSYM=0 (no dSYMs), ZIP=1 (also a .zip of the app), MAC_SIGN_IDENTITY (empty = ad-hoc, the default; a codesign
# identity is used as given, no notarization step exists here).
# The version is the one compiled into the programs (tools/release/version_info.sh's string; all three must carry the
# same); a "-dirty" build (uncommitted changes) is refused (ALLOW_DIRTY=1: a test package, it says so).
# The copies are stripped (strip -S -x: no debug map, no object paths); the symbols go to $REL/debug/<name>/*.dSYM (a
# crash report's "#k +0x..." offsets: keep them, or rebuild that commit, while testers use the build).
# macOS ships bash 3.2 as /bin/bash: this script keeps to it (no associative arrays, no ${x,,}, empty arrays as
# ${a[@]+"${a[@]}"}), and to the BSD tools (no GNU sed -i, realpath, timeout or sha256sum).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd -P)
PORT=$(cd "$HERE/../.." && pwd -P)
REL=${REL:-$PORT/build/release}
DRY=${DRY_RUN:-0}
APPNAME="Burnout 3 Native"
BUNDLE_ID=${BUNDLE_ID:-io.github.u0rhs.b3port}
die() { echo "package_mac: $*" >&2; exit 1; }
say() { echo "package_mac: $*"; }
run() { if [ "$DRY" = 1 ]; then printf '  [dry run] %s\n' "$*"; else "$@"; fi; }   # a Mac-only step
runq() { if [ "$DRY" = 1 ]; then printf '  [dry run] %s\n' "$*"; else "$@" > /dev/null; fi; }   # ... its output not shown
first() { sed -n 1p; }   # the first line (not head -1: the writer's SIGPIPE would fail a pipefail pipeline)
case "$REL" in /tmp/*) die "REL under /tmp (RAM): use the build folder";; esac
# macintel1: the package is for the CPU of the Mac it is built on (arm64: Apple Silicon; x86_64: an Intel Mac); the
# build, the bundled SDL3 and the checks below all follow it. DRY_RUN=1 takes MAC_ARCH (default arm64).
if [ "$DRY" = 1 ]; then ARCH=${MAC_ARCH:-arm64}; else ARCH=$(uname -m); fi
case "$ARCH" in arm64) CPU_NAME="Apple Silicon"; HARDWARE="A Mac with Apple Silicon (M1 or later). This download is not for Intel Macs";;
                x86_64) CPU_NAME="Intel"; HARDWARE="An Intel Mac (x86-64). This download is not for Apple Silicon Macs";;
                *) die "unknown CPU $ARCH (arm64 or x86_64)";; esac
if [ "$DRY" != 1 ]; then
    [ "$(uname -s)" = Darwin ] || die "macOS only. DRY_RUN=1 checks the layout anywhere"
    if [ "$ARCH" = x86_64 ] && [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)" = 1 ]; then
        die "a Rosetta shell on Apple Silicon: run arch -arm64 bash $0 (an Intel package is built on an Intel Mac)"
    fi
    for t in clang++ otool install_name_tool lipo dsymutil codesign strip hdiutil sips iconutil ditto plutil python3; do
        command -v "$t" > /dev/null 2>&1 || die "no $t (the Xcode command line tools: xcode-select --install)"
    done
fi
mkdir -p "$REL"
REL=$(cd "$REL" && pwd -P)

# ---- SDL3: the prefix the build links and the dylib the app carries ----
SDLV=""
if [ "$DRY" = 1 ]; then
    SDL3_DIR=${SDL3_DIR:-}
    SDLNAME=libSDL3.0.dylib
else
    if [ -z "${SDL3_DIR:-}" ]; then
        if command -v pkg-config > /dev/null 2>&1 && pkg-config --exists sdl3 2>/dev/null; then SDL3_DIR=$(pkg-config --variable=prefix sdl3)
        elif command -v brew > /dev/null 2>&1; then SDL3_DIR=$(brew --prefix sdl3 2>/dev/null || true)
        fi
    fi
    [ -n "${SDL3_DIR:-}" ] && [ -f "$SDL3_DIR/include/SDL3/SDL.h" ] || die "SDL3 not found (brew install sdl3 pkg-config, or SDL3_DIR=<prefix>)"
    SDL3_DIR=$(cd -P "$SDL3_DIR" && pwd -P)
    SDLLIB=$SDL3_DIR/lib/libSDL3.0.dylib
    [ -f "$SDLLIB" ] || die "no $SDLLIB"
    SDL_ID=$(otool -D "$SDLLIB" | sed -n 2p)   # its install name (Homebrew: /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib)
    SDLNAME=$(basename "${SDL_ID:-libSDL3.0.dylib}")
    SDLV=$(sed -n 's/^Version: *//p' "$SDL3_DIR/lib/pkgconfig/sdl3.pc" 2>/dev/null | first || true)
fi
# the minimum macOS a Mach-O file states (LC_BUILD_VERSION minos; older files: LC_VERSION_MIN_MACOSX)
minos() { otool -l "$1" | awk '/cmd LC_BUILD_VERSION/ {b = 1} b && $1 == "minos" {print $2; exit}
                               /cmd LC_VERSION_MIN_MACOSX/ {v = 1} v && $1 == "version" {print $2; exit}'; }
vmax() { printf '%s\n' "$@" | awk -F. 'NF {k = sprintf("%05d%05d%05d", $1, $2, $3); if (k > m) {m = k; v = $0}} END {print v}'; }
if [ "$DRY" = 1 ]; then SDLMIN=${MACOS_MIN:-14.0}; else SDLMIN=$(minos "$SDLLIB"); fi
[ -n "$SDLMIN" ] || die "no minimum macOS version in $SDLLIB"
MACOS_MIN=${MACOS_MIN:-$SDLMIN}
[ "$(vmax "$MACOS_MIN" "$SDLMIN")" = "$MACOS_MIN" ] || say "note: MACOS_MIN=$MACOS_MIN is below the bundled SDL3's $SDLMIN: the app needs $SDLMIN"

# ---- build (or take OUT=) ----
PROGS="b3launcher native_play libb3render.so b3modtool"   # b3modtool: the mod kit's asset tool
if [ "$DRY" = 1 ]; then
    OUT=${OUT:-}
elif [ -z "${OUT:-}" ]; then
    OUT=${BUILD_DIR:-$PORT/build/mac-release}
    mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd -P)
    case "$OUT" in "$PORT"/*) B=${OUT#"$PORT"/};; *) die "BUILD_DIR must be inside the repo";; esac   # the Makefile's BUILD: relative
    if [ -z "${MAKE:-}" ]; then if command -v gmake > /dev/null 2>&1; then MAKE=gmake; else MAKE=make; fi; fi
    case "$("$MAKE" --version 2>/dev/null | first)" in *"GNU Make "[4-9]*) ;; *) die "$MAKE is not GNU make 4 or newer (brew install make: gmake)";; esac
    # objects of another deployment target / SDL3 / compiler are not reused (make cannot see them change)
    # -ffile-prefix-map: no build folder (__FILE__, debug info) in the programs (the privacy scan below)
    FLAGS="-O2 -g -ffile-prefix-map=$PORT/="
    STAMP="$MACOS_MIN|$SDL3_DIR|$FLAGS|$(clang++ --version 2>&1 | first)"
    if [ "$(cat "$OUT/.release-stamp" 2>/dev/null || true)" != "$STAMP" ] && [ -n "$(find "$OUT" -name '*.o' -type f -print -quit)" ]; then
        say "the release settings changed: rebuilding $OUT's objects"
        find "$OUT" -name '*.o' -type f -delete
        for f in $PROGS; do rm -f "$OUT/$f"; done
    fi
    printf '%s' "$STAMP" > "$OUT/.release-stamp"
    export MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN
    [ -f "$PORT/gen/native/manifest.tsv" ] && [ -f "$PORT/gen/native/table/functions.h" ] || die "gen/native (the committed native bundle) is missing"
    say "building into $OUT ($MAKE NATIVE_FROM=bundle, macOS $MACOS_MIN, the first build compiles ~4,900 units) ..."
    MK=("$MAKE" -C "$PORT" --no-print-directory -j"${JOBS:-$(sysctl -n hw.ncpu)}" BUILD="$B" CXX=clang++ CXXFLAGS="$FLAGS" NATIVE_FROM=bundle
        SDL3_CFLAGS="-I$SDL3_DIR/include" SDL3_LIBS="-L$SDL3_DIR/lib -lSDL3")
    T0=$(date +%s)
    "${MK[@]}" "$B/native_play" "$B/libb3render.so" "$B/b3launcher" "$B/b3modtool" > "$OUT/release-build.log" 2>&1 \
        || { grep -E -m 20 'error|Error|undefined|ld:' "$OUT/release-build.log" >&2 || true; die "the build failed ($OUT/release-build.log)"; }
    say "built in $(( $(date +%s) - T0 )) s"
fi
if [ "$DRY" != 1 ]; then for f in $PROGS; do [ -f "$OUT/$f" ] || die "no $OUT/$f (OUT=<a build of the four programs>)"; done; fi

# ---- the version the build carries (version.h: "<base>+<commit>[-dirty]") ----
BASE=$(tr -d ' \r\n' < "$PORT/VERSION")
BASE_RE=$(printf '%s' "$BASE" | sed 's/[.]/[.]/g')   # (not ${BASE//./\\.}: bash 3.2 keeps the backslashes)
# the full stamp first ("<base>+<commit>"): a bare "<base>" can also be other text in a program (1.2.0: the memory
# card's "Sony PS2 Memory Card Format 1.2.0.0" in the launcher's save import)
stamp() {
    s=$(grep -a -o -E "$BASE_RE\+[0-9a-f]+(-dirty)?" "$1" | first || true)
    [ -n "$s" ] || s=$(grep -a -o -m1 -E "$BASE_RE" "$1" | first || true)
    printf '%s' "$s"
}
if [ "$DRY" = 1 ]; then
    VER=$("$PORT/tools/release/version_info.sh" --print)
else
    VER=$(stamp "$OUT/native_play")
    [ -n "$VER" ] || die "no version string ($BASE...) in $OUT/native_play"
    for f in b3launcher libb3render.so; do
        V=$(stamp "$OUT/$f")
        [ "$V" = "$VER" ] || die "native_play is $VER but $f is ${V:-unstamped}: build them together"
    done
fi
case "$VER" in
    *-dirty) [ "${ALLOW_DIRTY:-0}" = 1 ] || [ "$DRY" = 1 ] || die "$VER was built from a tree with uncommitted changes (commit first; ALLOW_DIRTY=1: a test package)"
             echo "package_mac: WARNING: $VER was built from a tree with uncommitted changes" >&2;;
esac
HEAD=$(git -C "$PORT" rev-parse --short=8 HEAD 2>/dev/null || true)
case "$VER" in *"+$HEAD"*) ;; *) echo "package_mac: note: $VER is not this checkout's commit ($HEAD)" >&2;; esac
SHORTV=$(echo "$BASE" | sed -E 's/^([0-9]+(\.[0-9]+)*).*/\1/')   # CFBundleShortVersionString: numbers only (0.1.0)
case "$SHORTV" in [0-9]*) ;; *) SHORTV=0.0.0;; esac
NAME=burnout3-native-$(echo "$VER" | tr -c 'A-Za-z0-9._\n-' '-')-macos-$ARCH
say "$NAME (macOS $MACOS_MIN or newer, SDL ${SDLV:-3})"

# ---- stage: the image's folder, the app in it ----
ST=$REL/stage-mac/$NAME
APP=$ST/$APPNAME.app
C=$APP/Contents
M=$C/MacOS
R=$C/Resources
DBG=$REL/debug/$NAME
rm -rf "$REL/stage-mac" "$DBG"
mkdir -p "$M" "$R/LICENSES" "$DBG"
ln -s /Applications "$ST/Applications"
for f in $PROGS; do
    if [ "$DRY" = 1 ]; then printf 'placeholder for %s (dry run)\n' "$f" > "$M/$f"; continue; fi
    # (from the repo: the programs name their objects relative to it)
    if [ "${DSYM:-1}" = 1 ]; then (cd "$PORT" && dsymutil "$OUT/$f" -o "$DBG/$f.dSYM" > /dev/null 2>&1) || echo "package_mac: WARNING: dsymutil failed on $f (no symbols kept)" >&2; fi
    cp "$OUT/$f" "$M/$f"
    strip -S -x "$M/$f"
done
if [ "$DRY" = 1 ]; then printf 'placeholder for %s (dry run)\n' "$SDLNAME" > "$M/$SDLNAME"; else cp -L "$SDLLIB" "$M/$SDLNAME"; fi
chmod 755 "$M/b3launcher" "$M/native_play" "$M/b3modtool"
chmod 644 "$M/libb3render.so" "$M/$SDLNAME"

# install names: SDL3 and the renderer as @rpath/<name>, found beside the program that loads them; no absolute rpath
rpaths() { otool -l "$1" | awk '/cmd LC_RPATH/ {r = 1} r && $1 == "path" {print $2; r = 0}'; }
run install_name_tool -id "@rpath/$SDLNAME" "$M/$SDLNAME"
run install_name_tool -id "@rpath/libb3render.so" "$M/libb3render.so"
for f in b3launcher libb3render.so; do
    if [ "$DRY" = 1 ]; then OLD="<the SDL3 install name the link recorded>"
    else OLD=$(otool -L "$M/$f" | awk 'NR > 1 {print $1}' | grep -E '/libSDL3[^/]*\.dylib$' | first || true)
         [ -n "$OLD" ] || die "$f does not link SDL3 (otool -L)"
    fi
    run install_name_tool -change "$OLD" "@rpath/$SDLNAME" "$M/$f"
done
for f in b3launcher native_play libb3render.so b3modtool "$SDLNAME"; do
    if [ "$DRY" != 1 ]; then for p in $(rpaths "$M/$f"); do case "$p" in @*) ;; *) install_name_tool -delete_rpath "$p" "$M/$f";; esac; done; fi
done
# (outputs taken whole before matching: grep -q stopping early would fail a pipefail pipeline)
add_rpath() {   # add_rpath <rpath> <file>: unless it has it
    local have=""
    [ "$DRY" = 1 ] || have=$(rpaths "$2")
    case "
$have
" in *"
$1
"*) ;; *) run install_name_tool -add_rpath "$1" "$2";; esac
}
add_rpath @executable_path "$M/b3launcher"
add_rpath @loader_path "$M/libb3render.so"

# ---- the Mach-O checks: the CPU ($ARCH), what each loads (only macOS's own libraries and the app's), its minimum macOS ----
MIN=$MACOS_MIN
if [ "$DRY" = 1 ]; then
    say "[dry run] the checks: otool -L / -l of each program (only /usr/lib, /System/Library, @rpath/$SDLNAME,"
    say "[dry run]   @rpath/libb3render.so; rpaths only @executable_path / @loader_path; $ARCH; LC_BUILD_VERSION minos)"
else
    bad=""
    say "$VER -- what each program loads:"
    for f in b3launcher native_play libb3render.so b3modtool "$SDLNAME"; do
        a=$(lipo -info "$M/$f" 2>&1 || true)
        case " $a " in *" $ARCH "*|*" $ARCH"$'\n'*) ;; *) bad="$bad; $f is not $ARCH ($a)";; esac
        own=$(otool -D "$M/$f" | sed -n 2p)
        deps=$(otool -L "$M/$f" | awk 'NR > 1 {print $1}' | { if [ -n "$own" ]; then grep -vxF "$own"; else cat; fi; } || true)
        n=$(printf '%s\n' "$deps" | awk -v sdl="@rpath/$SDLNAME" 'NF && $0 !~ /^\/usr\/lib\// && $0 !~ /^\/System\/Library\// && $0 != sdl && $0 != "@rpath/libb3render.so"' | tr '\n' ' ')
        [ -z "$n" ] || bad="$bad; $f loads $n"
        r=$(rpaths "$M/$f" | grep -vxE '@executable_path|@loader_path' | tr '\n' ' ' || true)
        [ -z "$r" ] || bad="$bad; $f has the rpath $r"
        mo=$(minos "$M/$f"); MIN=$(vmax "$MIN" "${mo:-0}")
        printf '  %-16s macOS %-6s loads: %s\n' "$f" "${mo:-?}" "$(printf '%s\n' "$deps" | sed 's|^/System/Library/Frameworks/\([^/]*\)\.framework/.*|\1|' | tr '\n' ' ')"
    done
    hb=$(otool -L "$M"/* 2>/dev/null | grep -E '/opt/homebrew|/usr/local|/opt/local|Cellar' || true)
    [ -z "$hb" ] && [ -z "$bad" ] || die "the app would not run on a Mac without Homebrew${bad}${hb:+; $hb}"
    say "otool: no Homebrew (or other non-system) path in any program; the app needs macOS $MIN"
fi

# ---- Info.plist, PkgInfo, the icon ----
cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>	<string>en</string>
	<key>CFBundleDisplayName</key>	<string>$APPNAME</string>
	<key>CFBundleName</key>	<string>$APPNAME</string>
	<key>CFBundleExecutable</key>	<string>b3launcher</string>
	<key>CFBundleIdentifier</key>	<string>$BUNDLE_ID</string>
	<key>CFBundleIconFile</key>	<string>b3</string>
	<key>CFBundleInfoDictionaryVersion</key>	<string>6.0</string>
	<key>CFBundlePackageType</key>	<string>APPL</string>
	<key>CFBundleSignature</key>	<string>????</string>
	<key>CFBundleShortVersionString</key>	<string>$SHORTV</string>
	<key>CFBundleVersion</key>	<string>$SHORTV</string>
	<key>B3PortVersion</key>	<string>$VER</string>
	<key>LSMinimumSystemVersion</key>	<string>$MIN</string>
	<key>LSArchitecturePriority</key>	<array><string>$ARCH</string></array>
	<key>LSApplicationCategoryType</key>	<string>public.app-category.racing-games</string>
	<key>NSPrincipalClass</key>	<string>NSApplication</string>
	<key>NSHighResolutionCapable</key>	<true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key>	<true/>
	<key>GCSupportsControllerUserInteraction</key>	<true/>
	<key>GCSupportedGameControllers</key>
	<array>
		<dict><key>ProfileName</key>	<string>ExtendedGamepad</string></dict>
	</array>
	<key>NSHumanReadableCopyright</key>	<string>Burnout 3 native port $VER. No game data included.</string>
</dict>
</plist>
EOF
printf 'APPL????' > "$C/PkgInfo"
if [ "$DRY" = 1 ]; then
    python3 -I -c 'import plistlib, sys; d = plistlib.load(open(sys.argv[1], "rb")); print("package_mac: [dry run] Info.plist parses: %s %s, macOS %s, runs %s" % (d["CFBundleIdentifier"], d["CFBundleShortVersionString"], d["LSMinimumSystemVersion"], d["CFBundleExecutable"]))' "$C/Info.plist"
else
    plutil -lint "$C/Info.plist" > /dev/null || die "Info.plist does not parse (plutil -lint)"
fi
# the app icon: assets/icon/b3-icon.icns, made on any host by tools/release/make_icons.py (no sips / iconutil step); kept
# in step with the drawing by its --check
python3 -I "$PORT/tools/release/make_icons.py" --check > /dev/null || die "the icon files are out of date: run tools/release/make_icons.py"
install -m 644 "$PORT/assets/icon/b3-icon.icns" "$R/b3.icns"
[ -s "$R/b3.icns" ] || die "no assets/icon/b3-icon.icns"
install -m 644 "$PORT/assets/b3-icon.png" "$R/b3-icon.png"   # the launcher's header (Contents/Resources: main.cpp's icon list)
python3 -I "$PORT/tools/blender/make_addon_zip.py" "$R/b3_car_tools.zip" > /dev/null || die "make_addon_zip failed"
chmod 644 "$R/b3_car_tools.zip"

# ---- the texts: @VERSION@ / @SDL_VERSION@ / @MACOS_MIN@ filled in ----
text() { sed -e "s/@VERSION@/$VER/g" -e "s/@SDL_VERSION@/${SDLV:-3}/g" -e "s/@MACOS_MIN@/$MIN/g" -e "s/@MAC_CPU@/$CPU_NAME/g" \
              -e "s/@MAC_HARDWARE@/$HARDWARE/g" "$1" > "$2"; }
text "$HERE/README-TESTERS-mac.txt" "$R/README-TESTERS.txt"
text "$HERE/KNOWN-ISSUES.txt" "$R/KNOWN-ISSUES.txt.in"
PLAT='- Mac and Linux builds (Windows only for now).'
grep -qxF -- "$PLAT" "$R/KNOWN-ISSUES.txt.in" || die "KNOWN-ISSUES.txt: its 'Not in this build' platform line changed: update package_mac.sh"
# this package: the platform line goes (a Mac build is what it is); the Mac's own notes before "Not in this build"
awk -v plat="$PLAT" '
    $0 == "Not in this build" {
        print "Mac"
        print "- The Mac build has been played less than the Windows one, and photo mode'"'"'s depth of field has not run on a Mac"
        print "  yet (if it fails, it turns itself off)."
        print "- If the frame rate in the game is low, try SETTINGS > DISPLAY > FRAMES IN FLIGHT 2 (it applies at the next start)."
        print "- The app is not notarized by Apple: its first start needs Open Anyway (README-TESTERS.txt, HOW TO START)."
        print ""
    }
    $0 != plat {print}' "$R/KNOWN-ISSUES.txt.in" > "$R/KNOWN-ISSUES.txt"
rm -f "$R/KNOWN-ISSUES.txt.in"
text "$HERE/THIRD-PARTY-NOTICES-mac.txt" "$R/LICENSES/THIRD-PARTY-NOTICES.txt"
printf 'Burnout 3 native port %s\ntest build for macOS (%s, macOS %s or newer), packaged %s\n' "$VER" "$CPU_NAME" "$MIN" "$(date -u +%Y-%m-%d)" > "$R/VERSION.txt"
lic() { [ -f "$1" ] || die "no license file $1"; install -m 644 "$1" "$R/LICENSES/$2"; }
SDLLIC=""
for c in "$SDL3_DIR/LICENSE.txt" "$SDL3_DIR/share/licenses/SDL3/LICENSE.txt" "$SDL3_DIR/share/doc/SDL3/LICENSE.txt"; do
    [ -n "$SDL3_DIR" ] && [ -f "$c" ] && { SDLLIC=$c; break; }
done
if [ "$DRY" = 1 ] && [ -z "$SDLLIC" ]; then printf '(dry run: the SDL3 prefix'"'"'s LICENSE.txt)\n' > "$R/LICENSES/SDL3-LICENSE.txt"
else lic "${SDLLIC:-$SDL3_DIR/LICENSE.txt}" SDL3-LICENSE.txt; fi
awk '/This software is available under 2 licenses/{on=1} on{print} /^\*\//{if(on) exit}' "$PORT/src/b3launcher/third_party/stb_truetype.h" \
    > "$R/LICENSES/stb_truetype-LICENSE.txt"
[ -s "$R/LICENSES/stb_truetype-LICENSE.txt" ] || die "no license block in stb_truetype.h"
lic "$PORT/src/native/third_party/LICENSE-dr_libs.txt" dr_libs-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-minimp3.txt" minimp3-LICENSE.txt
lic "$PORT/src/native/third_party/LICENSE-stb_vorbis.txt" stb_vorbis-LICENSE.txt
lic "$PORT/src/third_party/lua-5.4.9/LICENSE.txt" Lua-LICENSE.txt   # lua6: script mods (src/native/script_mods.h)
# chd1: .chd disc images (src/native/third_party/libchdr: libchdr and the LZMA SDK, miniz and zstd decoders in it)
lic "$PORT/src/native/third_party/libchdr/LICENSE-libchdr.txt" libchdr-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-lzma.txt" lzma-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-miniz.txt" miniz-LICENSE.txt
lic "$PORT/src/native/third_party/libchdr/LICENSE-zstd.txt" zstd-LICENSE.txt
chmod 644 "$R"/*.txt "$R/b3-icon.png" "$R/b3.icns"

# ---- signing: ad-hoc (Apple Silicon runs only signed code; install_name_tool / strip voided the linker's signature).
# Inside out: the libraries and helper programs, then the bundle (the launcher, Info.plist, the resources sealed) ----
SIGN=${MAC_SIGN_IDENTITY:--}
for f in "$SDLNAME" libb3render.so native_play b3modtool; do run codesign --force --timestamp=none -s "$SIGN" "$M/$f"; done
run codesign --force --deep --timestamp=none -s "$SIGN" "$APP"
if [ "$DRY" != 1 ]; then
    codesign --verify --deep --strict --verbose=1 "$APP" 2>&1 | sed 's/^/  /' || die "codesign --verify failed"
    say "signed: $(codesign -dv "$APP" 2>&1 | grep -E '^(Signature|Identifier)=' | tr '\n' ' ')"
fi

# ---- privacy: no trace of the build machine's user (its account name or home folder) in anything packaged ----
privacy_scan() {   # $1: the staged folder
  local who home hits
  who=$(id -un 2>/dev/null || true); home=${HOME:-}
  hits=""
  if [ "${#who}" -ge 3 ]; then hits=$(grep -r -a -l -i -F -- "$who" "$1" 2>/dev/null || true); fi
  if [ -z "$hits" ] && [ "${#home}" -ge 6 ]; then hits=$(grep -r -a -l -F -- "$home" "$1" 2>/dev/null || true); fi
  [ -z "$hits" ] || die "the package would carry the build machine's user name or home folder (anonymous packages only):
$hits"
  echo "$(basename "$0" .sh): privacy scan: no user name or home folder of this machine in the package"
}
privacy_scan "$APP"

say "the app's layout:"
(cd "$ST" && find . -mindepth 1 | sort | while IFS= read -r p; do
    if [ -L "$p" ]; then printf '  %10s  %s -> %s\n' link "${p#./}" "$(readlink "$p")"
    elif [ -d "$p" ]; then printf '  %10s  %s/\n' "" "${p#./}"
    else printf '  %10s  %s\n' "$(wc -c < "$p" | tr -d ' ')" "${p#./}"; fi
done)

# ---- the disk image (UDZO: compressed, read-only), the app beside a link to /Applications ----
DMG=$REL/$NAME.dmg
if [ "$DRY" = 1 ]; then
    run hdiutil create -volname "$APPNAME" -srcfolder "$ST" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$REL/$NAME.tmp.dmg"
    say "[dry run] done: $ST (no image made)"
    exit 0
fi
rm -f "$DMG" "$REL/$NAME.tmp.dmg"
ok=0
for try in 1 2 3; do   # hdiutil now and then fails with "Resource busy" (a CI runner's disk arbitration): again
    if hdiutil create -quiet -volname "$APPNAME" -srcfolder "$ST" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$REL/$NAME.tmp.dmg"; then ok=1; break; fi
    echo "package_mac: hdiutil create failed (try $try of 3)" >&2; sleep 5
done
[ "$ok" = 1 ] || die "hdiutil create failed"
mv "$REL/$NAME.tmp.dmg" "$DMG"
hdiutil verify -quiet "$DMG" || die "hdiutil verify failed on $DMG"
say "$DMG ($(du -h "$DMG" | cut -f1)); symbols: $DBG"
if [ "${ZIP:-0}" = 1 ]; then
    ditto -c -k --keepParent "$APP" "$REL/$NAME.zip"
    say "$REL/$NAME.zip ($(du -h "$REL/$NAME.zip" | cut -f1))"
fi

# ---- SMOKE=1: the app as a player gets it -- out of the image, from its own folder ----
[ "${SMOKE:-0}" = 1 ] || exit 0
SM=$REL/smoke-mac
MNT=$SM/mnt
rm -rf "$SM"; mkdir -p "$MNT"
hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$MNT" "$DMG" || die "hdiutil attach failed"
detach() { hdiutil detach -quiet "$MNT" 2>/dev/null || hdiutil detach -quiet -force "$MNT" 2>/dev/null || true; }
trap detach EXIT
[ -d "$MNT/$APPNAME.app" ] || die "the image has no $APPNAME.app"
[ -L "$MNT/Applications" ] && [ "$(readlink "$MNT/Applications")" = /Applications ] || die "the image has no Applications link"
ditto "$MNT/$APPNAME.app" "$SM/$APPNAME.app"   # the drag to Applications (a folder of the build here)
detach; trap - EXIT
SAPP=$SM/$APPNAME.app
P=$SAPP/Contents/MacOS
say "smoke test in $SAPP"
codesign --verify --deep --strict "$SAPP" || die "the copied app's signature does not verify"
say "codesign --verify --deep --strict: the copy out of the image verifies"
# a run with a time limit (macOS has no timeout(1)): kills the exact PID it started
limited() {   # limited <seconds> <log> <command ...>
    local secs=$1 log=$2 pid t=0 rc=0
    shift 2
    "$@" > "$log" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$t" -ge "$secs" ]; then echo "package_mac: $(basename "$1") ran over ${secs} s: stopped" >&2; kill "$pid" 2>/dev/null || true; break; fi
        sleep 1; t=$((t + 1))
    done
    wait "$pid" || rc=$?
    return "$rc"
}
# the launcher: its screenshot mode (never START GAME), a scratch user data folder, the offscreen video driver;
# DYLD_PRINT_LIBRARIES: dyld names each library it loads (set here, not through env(1): macOS drops DYLD_ variables
# passing through its own protected programs)
launcher_run() {   # launcher_run <video driver or "">: run by limited, in its own background subshell (exec: the PID it watches)
    rm -rf "$SM/launcher-pref" "$SM/launcher-shots"; mkdir -p "$SM/launcher-pref" "$SM/launcher-shots"
    cd "$SM" || exit 1
    unset B3LAUNCHER_SHOT_START
    if [ -n "$1" ]; then export SDL_VIDEO_DRIVER=$1; fi
    export DYLD_PRINT_LIBRARIES=1
    exec "$P/b3launcher" --screenshot "$SM/launcher-shots" --pref "$SM/launcher-pref"
}
LLOG=$SM/launcher.log
rc=0; limited 600 "$LLOG" launcher_run offscreen || rc=$?
if [ "$rc" != 0 ] && grep -qE 'b3launcher: (SDL_Init|window):' "$LLOG"; then
    say "note: the offscreen video driver did not start ($(grep -m1 -E 'b3launcher: (SDL_Init|window):' "$LLOG")): again with hidden Cocoa windows"
    rc=0; limited 600 "$LLOG" launcher_run "" || rc=$?
fi
[ "$rc" = 0 ] || { tail -20 "$LLOG" >&2; die "b3launcher --screenshot failed (exit $rc, log $LLOG)"; }
n=$(find "$SM/launcher-shots" -name '*.png' | wc -l | tr -d ' ')
[ "$n" -gt 0 ] || die "b3launcher drew no pages ($LLOG)"
grep -q 'b3launcher: walk check PASS' "$LLOG" || die "no 'walk check PASS' line ($LLOG)"
F=$(grep -E '^b3launcher: .*(^|[^A-Za-z])FAIL([^A-Za-z]|$)' "$LLOG" || true)
[ -z "$F" ] || die "b3launcher checks failed:
$F"
NPASS=$(grep -cE '^b3launcher: .*(^|[^A-Za-z])PASS([^A-Za-z]|$)' "$LLOG" || true)
SDLLOAD=$(grep -E '^dyld\[[0-9]+\]: .*libSDL3' "$LLOG" | sed -E 's/^dyld\[[0-9]+\]: (<[0-9A-Fa-f-]+> )?//' | sort -u || true)
if [ -n "$SDLLOAD" ]; then
    [ "$SDLLOAD" = "$P/$SDLNAME" ] || die "b3launcher loaded SDL3 from $(echo "$SDLLOAD" | tr '\n' ' ') (not $P)"
    say "b3launcher --screenshot: $n pages drawn ($SM/launcher-shots), $NPASS PASS lines, no FAIL; SDL3 from $SDLLOAD"
else
    say "b3launcher --screenshot: $n pages drawn ($SM/launcher-shots), $NPASS PASS lines, no FAIL (dyld printed no library list: the otool check stands)"
fi
grep -v '^dyld\[' "$LLOG" > "$LLOG.tmp" && mv "$LLOG.tmp" "$LLOG"
# the game: the strict race check with the app's native_play (its libb3render.so and SDL3 from Contents/MacOS)
if [ -z "${B3_GAME_DIR:-}" ]; then
    say "smoke test PASS (no B3_GAME_DIR: the race check skipped -- B3_GAME_DIR=<disc folder> SAVES=<card folder> runs it)"
    exit 0
fi
{ STRICT=1 "$PORT/tools/native/race_check.sh" "$P/native_play" "$SM/race_check" || true; } | tail -1 | tee "$SM/race_check.verdict"
V=$(cat "$SM/race_check.verdict")
case "$V" in "race_check: PASS"*) ;; *) die "race check failed (log $SM/race_check/native_play.log)";; esac
FBN=$(echo "$V" | sed -nE 's/.*fallback: ([0-9]+) functions.*/\1/p')
[ -z "$FBN" ] || [ "$FBN" = 0 ] || die "the race fell back to retail code ($FBN functions)"
grep -qE 'vu0: [1-9][0-9]* launches: [0-9]+ generated, 0 interpreted, 0 without' "$SM/race_check.verdict" || die "VU0 not all generated: $V"
say "race check (STRICT=1): PASS, no fallback, VU0 all generated, from $P/native_play"
say "smoke test PASS"
