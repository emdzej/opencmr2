#!/usr/bin/env bash
# Put opencmr2.wasm into the site's play page (docs/public/play/opencmr2.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/opencmr2.wasm.
#   docs/scripts/copy-wasm.sh [path/to/opencmr2.wasm]
# The module is the engine only: the game data always comes from the player's own copy of the game.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/opencmr2.wasm}"
[ -f "$src" ] || { echo "no $src: build opencmr2.wasm first (AGENTS.md, the build-gasm commands in CMakeLists.txt)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/opencmr2.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/opencmr2.wasm"
