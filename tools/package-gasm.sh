#!/usr/bin/env bash
# Package opencmr2.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENCMR2_WASM=<file> picks the module
# (default build-gasm/opencmr2.wasm); it is always bundled as opencmr2.wasm, which is also gasm's storage
# namespace for the saves (the same as the browser player's, "opencmr2"). Produces in <out dir>:
#   macos-universal  Colin McRae Rally 2.0 (gasm).app, opencmr2-gasm-<version>-macos-universal.zip
#   linux-<arch>     opencmr2-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   opencmr2-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/, the icon is drawn by tools/gasm-bundle/icon.py (tools/make-icns.sh on macOS).
# No AOT .cwasm: gasm-run --compile only targets the host it runs on, so it can't be made for the other
# platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# ANGLE (libEGL, libGLESv2, SwiftShader + its ICD file) for gasm:gl, from the runner dir
# (tools/fetch-gasm-runner.sh), with its notices. gasm-run finds it next to itself (Linux, Windows); the .app keeps it in Resources/angle/ (--gl-lib).
angle() {
  local dest=$1 f n=0
  for f in "$RUNDIR"/*EGL* "$RUNDIR"/*GLESv2* "$RUNDIR"/*swiftshader* "$RUNDIR"/ANGLE-NOTICES.txt; do
    if [ -f "$f" ]; then cp "$f" "$dest/"; n=$((n + 1)); fi
  done
  if [ "$n" -lt 3 ]; then echo "no ANGLE in $RUNDIR (tools/fetch-gasm-runner.sh fetches it)" >&2; exit 1; fi
}
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENCMR2_WASM:-$ROOT/build-gasm/opencmr2.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$ROOT/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="opencmr2-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" >"$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" >"$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi >"$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER, %APPDATA% and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start data_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "Colin McRae Rally 2.0 (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (on macOS 15 and later: open it once, then System Settings, Privacy &
Security, Open Anyway), or run: xattr -dr com.apple.quarantine "Colin McRae Rally 2.0 (gasm).app"'
      data_how='The first time, the app asks for your game data: choose the image of your Colin McRae Rally
2.0 CD (.iso), or a folder with the CD files (data1.hdr, data1.cab, Game, FrontEnd).'
      change='Hold Option while opening the app to choose other game data, or delete
  ~/Library/Application Support/OpenCMR2/data-location
From Terminal: "Colin McRae Rally 2.0 (gasm).app/Contents/MacOS/OpenCMR2" --help'
      saves='~/Library/Application Support/gasm/opencmr2/'
      logs='~/Library/Logs/OpenCMR2/gasm.log' ;;
    linux-*)
      start='Run ./opencmr2.sh (from a terminal or your file manager). ./opencmr2.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and Ubuntu) and a
Vulkan capable graphics driver.'
      data_how='Give it your game data once: ./opencmr2.sh /path/to/CMR2.iso (an image of the CD), or a
folder with the CD files (data1.hdr, data1.cab, Game, FrontEnd). Without an argument it opens a chooser
(zenity or kdialog) if one is installed.'
      change='./opencmr2.sh --change-data, or give it another folder as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/opencmr2/data-location
./opencmr2.sh --help lists the options; anything after the data goes to gasm-run.'
      saves='~/.local/share/gasm/opencmr2/'
      logs='the terminal, or ~/.local/state/opencmr2/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenCMR2.cmd. (If Windows SmartScreen warns about gasm-run.exe or the script:
More info, Run anyway.)'
      data_how='The first time, it asks for your game data: the image of the CD (.iso), or a folder with
the CD files (data1.hdr, data1.cab, Game, FrontEnd): the CD drive itself works. From a command prompt:
OpenCMR2.cmd D:\'
      change='OpenCMR2.cmd --change-data, or delete %APPDATA%\OpenCMR2\data-location.
OpenCMR2.cmd --help lists the options; anything after the data goes to gasm-run.'
      saves='%APPDATA%\gasm\opencmr2\'
      logs='the console window, and %LOCALAPPDATA%\OpenCMR2\gasm.log' ;;
  esac
  cat >"$1" <<TXT
OpenCMR2 $VERSION for gasm ($PLATFORM)
https://opencmr2.emdzej.pl

OpenCMR2 is a faithful, function-by-function reimplementation of Colin McRae Rally 2.0 (Codemasters,
2000), ported from the Windows game (version 1.11). This bundle runs it as a WebAssembly module
(opencmr2.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included (https://gasm.emdzej.pl). It is
the same game as the browser player.

See https://opencmr2.emdzej.pl/guide/status for what works.

You need your own copy of Colin McRae Rally 2.0: the PC CD (an image of it, or its files; version 1.11
is on the CD). None of the original game's files are included.

START
$start

YOUR GAME DATA
$data_how
The choice is remembered (the folder must still be there next time).

CHANGE THE GAME DATA
$change

LAUNCH OPTIONS
Anything after the data goes to gasm-run: --window 1280x960, --fullscreen, --mute.
Full guide: https://opencmr2.emdzej.pl/guide/

SAVES (profiles, options)
$saves

LOG
$logs

LICENSES
OpenCMR2 is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. ANGLE and SwiftShader (the OpenGL ES libraries) are under their own
licenses (ANGLE-NOTICES.txt). Colin McRae Rally 2.0 is (c) 2000 Codemasters. OpenCMR2 contains no
original code or assets and is not affiliated with Codemasters.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/Colin McRae Rally 2.0 (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  # ANGLE stays together in Resources/angle/ (the launcher passes --gl-lib): gasm-run reads SwiftShader's
  # ICD file from the same folder, and Frameworks/ may hold only code (codesign --strict).
  mkdir -p "$APP/Contents/Resources/angle"
  angle "$APP/Contents/Resources/angle"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenCMR2"
  chmod +x "$APP/Contents/MacOS/OpenCMR2" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/opencmr2.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/opencmr2.icns"
  cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Colin McRae Rally 2.0 (gasm)</string>
  <key>CFBundleDisplayName</key><string>Colin McRae Rally 2.0 (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.opencmr2.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenCMR2 $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenCMR2</string>
  <key>CFBundleIconFile</key><string>opencmr2</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenCMR2 contributors, GPL-3.0; gasm-run MIT. Colin McRae Rally 2.0 is (c) 2000 Codemasters.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    for f in "$APP/Contents/Resources/angle"/*.dylib; do codesign --force --sign - "$f"; done
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  angle "$D"
  cp "$WASM" "$D/opencmr2.wasm"
  fill "$SRC/opencmr2.sh" "$D/opencmr2.sh"
  cp "$SRC/opencmr2-gasm.desktop" "$D/"
  python3 "$ROOT/tools/gasm-bundle/icon.py" 256 "$D/opencmr2.png"
  chmod 755 "$D/gasm-run" "$D/opencmr2.sh"; chmod 644 "$D/opencmr2.wasm" "$D/opencmr2-gasm.desktop" "$D/opencmr2.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  angle "$D"
  cp "$WASM" "$D/opencmr2.wasm"
  fill "$SRC/OpenCMR2.cmd" "$D/OpenCMR2.cmd"; fill "$SRC/opencmr2.ps1" "$D/opencmr2.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenCMR2.cmd opencmr2.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
