#!/usr/bin/env bash
# Regenerate the documentation screenshots (docs/public/screenshots/*.jpg) from opencmr2.wasm, run headless
# by gasm-run on your own disc. They are renders of the port, nothing else: never put the game's own files
# in docs/public/. Never opens a window (and never uses macOS screencapture).
#
#   tools/screenshots.sh [disc image or extracted CD dir] [opencmr2.wasm]
# Defaults: $OPENCMR2_DATA, else ~/Downloads/CMR2.ISO, else ./cd; build-gasm/opencmr2.wasm (./play builds
# it). GASM_RUN=<gasm-run> uses another runner. Needs sips (macOS) for the JPEGs. The shots are scripted
# input on fresh settings (storage in memory), at 1024 x 768 (the game's high-resolution art).
set -euo pipefail
cd "$(dirname "$0")/.."
DISC=${1:-${OPENCMR2_DATA:-}}
if [ -z "$DISC" ]; then if [ -e "$HOME/Downloads/CMR2.ISO" ]; then DISC=$HOME/Downloads/CMR2.ISO; else DISC=./cd; fi; fi
WASM=${2:-build-gasm/opencmr2.wasm}
OUT=docs/public/screenshots
[ -f "$WASM" ] || { echo "no $WASM: build it first (./play builds it)" >&2; exit 1; }
command -v sips >/dev/null || { echo "sips not found (macOS); convert the PNGs yourself" >&2; exit 1; }
RUN=${GASM_RUN:-"$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"}
if [ -d "$DISC" ]; then ASSET=(--asset-dir "$DISC"); else ASSET=(--asset "cd=$DISC"); fi
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$WASM" "$tmp/opencmr2.wasm"   # a snapshot: other builds may replace build-gasm/opencmr2.wasm meanwhile

shot() {   # name frames input [gasm-run options...]
  local name=$1 frames=$2 input=$3; shift 3
  "$RUN" "$tmp/opencmr2.wasm" "${ASSET[@]}" --headless "$frames" --window 1024x768 --param mode=1024x768x32 \
    --input "$input" --screenshot-filtered "$tmp/$name.png" "$@" >/dev/null 2>&1
  # the headless drawable is 1280 x 720 whatever --window says: the 4:3 frame is its middle 960 x 720
  sips -c 720 960 "$tmp/$name.png" >/dev/null
  sips -s format jpeg -s formatOptions 82 "$tmp/$name.png" --out "$OUT/$name.jpg" >/dev/null
  echo "-> $OUT/$name.jpg ($(($(wc -c < "$OUT/$name.jpg") / 1024)) KB)"
}

# the way in: the two logo movies skipped, the language chosen, the main menu
IN='1900:KEY(Enter),2100:KEY(Enter),2700:KEY(Enter)'
# Rally, Time Trial, the defaults through to the service area, then Race
TT="$IN,3050:KEY(Enter),3150:KEY(ArrowDown),3250:KEY(Enter)"
for f in 3400 3550 3700 3850 4000 4150 4300 4450 4600 4750 4900; do TT="$TT,$f:KEY(Enter)"; done
RACE="$TT,7000:KEY(ArrowRight),7030:KEY(ArrowRight),7060:KEY(ArrowRight),7100:KEY(Enter)"

shot menu 3000 "$IN"
shot service 7000 "$TT"
shot start-lights 7400 "$RACE"
shot stage-start 7900 "$RACE"
# left at the main menu the game plays its demo runs (after 30.5 s), one after another
shot demo-1 7200 "$IN"
shot demo-2 13500 "$IN"
shot demo-3 25500 "$IN"
