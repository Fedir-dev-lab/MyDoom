#!/usr/bin/env bash
# Builds a portable release and packs it into MyDoom-release-linux-x86_64.tar.gz
# Usage: ./make-release.sh          full  : SDL3 inside the executable (runs anywhere)
#        ./make-release.sh --light  light : uses the player's installed SDL3 (smallest)
set -euo pipefail

BUNDLED=ON; SUFFIX=""
if [[ "${1:-}" == "--light" ]]; then BUNDLED=OFF; SUFFIX="-light"; fi

SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="${TMPDIR:-/tmp}/MyDoom-build$SUFFIX"     # build cache lives outside the project
DIST="$SRC/dist/MyDoom"
ARCHIVE="$SRC/MyDoom-release$SUFFIX-linux-$(uname -m).tar.gz"

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DNATIVE_ARCH=OFF -DSDL_BUNDLED=$BUNDLED
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$SRC/dist"
cmake --install "$BUILD" --prefix "$DIST"

tar -C "$SRC/dist" -czf "$ARCHIVE" MyDoom

# The game on its own: players who already have the engine only need this file.
cp "$DIST/game.mdpk" "$SRC/MyDoom-game.mdpk"

echo
echo "Done:"
du -sh "$DIST/MyDoomEngine" "$DIST/game.mdpk" "$ARCHIVE"
echo
echo "  Engine + game : $ARCHIVE"
echo "  Game only     : $SRC/MyDoom-game.mdpk   (run: MyDoomEngine MyDoom-game.mdpk)"
echo "  Try it        : $DIST/MyDoomEngine"
