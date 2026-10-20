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
