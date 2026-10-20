# Pocket Games
Offline game launcher for the Waveshare ESP32-S3-Touch-AMOLED-1.75. ESP-IDF 5.5.1 + LVGL 9.4.

- `main/ui/registry.c`: add a game here (one line), implementing `game_t` (`registry.h`).
- `main/ui/app.c`: home carousel, pull-down status panel, power overlay.
- `main/ui/tetris.c`: Tetris (7-bag, wall kicks, ghost, hold on BOOT, guideline speed curve, level every 10 lines).
- `main/hal/`: device HAL (NVS, Wi-Fi WAGNERNET, AXP2101, ES8311 synth). `sim/`: host LVGL renderer for previews.

Build: `COMPANION_WIFI_PASSWORD=... idf.py build` (password goes to build/gen only; never committed).
Previews: `sim/build.sh && (cd sim && ./sim && python3 topng.py)` -> `previews/`.

Controls: Home: swipe left/right pages, tap icon to launch, swipe down from top for status. Tetris: drag/swipe left-right moves (one cell per 24 px),
tap rotates, swipe down hard-drops, BOOT holds, pause button for Resume / Music / Home. PWR: short = screen sleep/wake, hold 2 s = power off.
