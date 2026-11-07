#!/usr/bin/env bash
# Makes MyDoom-clean.zip of the project WITHOUT saves, build output, or old archives.
# Usage: ./make-clean-zip.sh
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
NAME="$(basename "$SRC")"
OUT="$SRC/MyDoom-clean.zip"
rm -f "$OUT"
cd "$SRC/.."
zip -qr "$OUT" "$NAME" \
    -x "$NAME/saves/*" "$NAME/userdata/*" "*.dat" \
       "$NAME/build*/*" "$NAME/dist/*" "$NAME/dist-win/*" \
       "$NAME/.cache/*" "$NAME/*.zip" "$NAME/*.tar.gz" "$NAME/*.mdpk"
echo "Done: $OUT"
echo "Saves inside archive (must be 0): $(unzip -l "$OUT" | grep -c -E '\.dat$|/saves/' || true)"
