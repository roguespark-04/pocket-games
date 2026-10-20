#!/usr/bin/env bash
# Build a release and its manifest. Publishes nothing.
#   scripts/release.sh 0.2.0 [BASE_URL]
# BASE_URL defaults to the GitHub release download URL for that tag.
set -euo pipefail
cd "$(dirname "$0")/.."
VER="${1:?usage: release.sh X.Y.Z [base_url]}"
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must be X.Y.Z"; exit 1; }
REPO="${PG_REPO:-roguespark-04/pocket-games}"
BASE="${2:-https://github.com/$REPO/releases/download/v$VER}"
[ -n "${COMPANION_WIFI_PASSWORD:-}" ] || { echo "COMPANION_WIFI_PASSWORD not set"; exit 1; }
echo "$VER" > version.txt
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
idf.py build >/tmp/pocket-games-release.log 2>&1 || { tail -30 /tmp/pocket-games-release.log; exit 1; }
mkdir -p dist
BIN="pocket-games-$VER.bin"
cp build/pocket-games.bin "dist/$BIN"
cp build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/ota_data_initial.bin dist/ 2>/dev/null || true
SHA=$(sha256sum "dist/$BIN" | cut -d' ' -f1); SIZE=$(stat -c %s "dist/$BIN")
printf '{"version":"%s","url":"%s/%s","size":%s,"sha256":"%s"}\n' "$VER" "$BASE" "$BIN" "$SIZE" "$SHA" > dist/manifest.json
echo "Built dist/$BIN ($SIZE bytes) and dist/manifest.json"
echo "To publish on GitHub (needs approval):"
echo "  gh release create v$VER dist/$BIN dist/manifest.json --repo $REPO --title v$VER"
