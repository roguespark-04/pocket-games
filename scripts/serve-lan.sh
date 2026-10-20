#!/usr/bin/env bash
# LAN fallback: serve dist/ over HTTP on port 8070. Build the firmware with
#   CONFIG_PG_OTA_MANIFEST_URL="http://<this-ip>:8070/manifest.json" and CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
# and run release.sh with BASE_URL http://<this-ip>:8070 . Plain HTTP: trusted home network only.
cd "$(dirname "$0")/../dist" && exec python3 -m http.server 8070
