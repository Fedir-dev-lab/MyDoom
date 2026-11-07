#!/usr/bin/env bash
# Builds MyDoom-windows.zip (MyDoomEngine.exe + game.mdpk) on Linux.
# Needs: sudo apt install mingw-w64 cmake ninja-build zip
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
BUILD="$TMP/MyDoom-build-win"
OUT="$SRC/dist-win/MyDoom"
ZIP="$SRC/MyDoom-windows.zip"

# game.mdpk: same format on every system, so pack it with a native mdpack
g++ -std=c++20 -I"$SRC/src" "$SRC/tools/mdpack.cpp" \
    "$SRC/src/game/GamePack.cpp" -o "$TMP/mdpack-native"
"$TMP/mdpack-native" "$SRC/assets" "$TMP/game.mdpk"

cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$SRC/cmake/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPREBUILT_GAME_PACK="$TMP/game.mdpk"
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$SRC/dist-win" "$ZIP"
cmake --install "$BUILD" --prefix "$OUT"
(cd "$SRC/dist-win" && zip -qr "$ZIP" MyDoom)

echo
echo "Done: $ZIP"
du -sh "$ZIP"
echo
echo "DLLs the .exe depends on (must be Windows system DLLs only):"
x86_64-w64-mingw32-objdump -p "$OUT/MyDoomEngine.exe" | grep "DLL Name"
