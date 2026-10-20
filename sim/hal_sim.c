#include "hal.h"
#include <string.h>
void hal_wifi_get(hal_wifi_t *o) { o->connected = true; strcpy(o->ssid, "WAGNERNET"); o->rssi = -58; }
void hal_batt_get(hal_batt_t *o) { o->percent = 82; o->charging = true; o->usb = true; }
void hal_sfx(hal_sfx_t s) { (void)s; }
void hal_music(bool on) { (void)on; }
int32_t hal_kv_get(const char *k, int32_t d) { if (!strcmp(k, "hs_tetris")) return 24310; return d; }
void hal_kv_set(const char *k, int32_t v) { (void)k; (void)v; }
bool hal_boot_pressed(void) { return false; }
bool hal_time_hm(int *h, int *m) { *h = 19; *m = 4; return true; }
#include <stdio.h>
hal_ota_t sim_ota = { .state = OTA_AVAILABLE, .current = "0.1.0", .available = "0.2.0" };
void hal_ota_get(hal_ota_t *o) { *o = sim_ota; }
void hal_ota_check(void) {}
void hal_ota_install(void) {}
void hal_ota_ack(void) {}
