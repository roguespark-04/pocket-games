/* Hardware abstraction used by the UI. Implemented by hal_esp.c on device and sim/hal_sim.c on host. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { bool connected; char ssid[33]; int rssi; } hal_wifi_t;
typedef struct { int percent; bool charging; bool usb; } hal_batt_t;

typedef enum { SFX_MOVE, SFX_ROTATE, SFX_LOCK, SFX_CLEAR, SFX_TETRIS, SFX_LEVEL, SFX_GAMEOVER, SFX_HOLD } hal_sfx_t;

void hal_wifi_get(hal_wifi_t *out);
void hal_batt_get(hal_batt_t *out);
void hal_sfx(hal_sfx_t s);
void hal_music(bool on);          /* theme tune loop */
int32_t hal_kv_get(const char *key, int32_t def);   /* NVS */
void hal_kv_set(const char *key, int32_t val);
bool hal_boot_pressed(void);      /* BOOT button level (true = down) */
bool hal_time_hm(int *h, int *m); /* false until clock is set */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define heap_caps_calloc_psram(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM)
#define heap_caps_free_psram(p) heap_caps_free(p)
#else
#include <stdlib.h>
#define heap_caps_calloc_psram(n) calloc(1, (n))
#define heap_caps_free_psram(p) free(p)
#endif

/* ---- Firmware updates (never auto-installed) ---- */
typedef enum { OTA_IDLE, OTA_NO_WIFI, OTA_CHECKING, OTA_UPTODATE, OTA_AVAILABLE, OTA_INSTALLING, OTA_RESTARTING, OTA_FAILED } hal_ota_state_t;
typedef struct {
    hal_ota_state_t state;
    char current[32], available[16];
    int progress;             /* 0..100 while installing */
    char error[40];
    bool prompt;              /* boot check found an update; UI should prompt once */
    bool rolled_back;         /* last update failed to start and was reverted; UI should tell the user once */
    char failed_version[16];
} hal_ota_t;
void hal_ota_get(hal_ota_t *o);
void hal_ota_check(void);         /* async */
void hal_ota_install(void);       /* async, user-initiated only */
void hal_ota_ack(void);           /* clears prompt + rolled_back flags */

void hal_set_brightness(int pct);   /* 10..100, live */
void hal_set_volume(int pct);       /* 0..100, live */
void hal_set_sleep_after(int seconds); /* 0 = never; live */
