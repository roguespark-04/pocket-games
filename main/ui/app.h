#pragma once
#include "lvgl.h"
#include "registry.h"
void app_init(void);              /* build home screen */
void app_go_home(void);
void app_launch(const game_t *g);
void app_open_panel(void);
void app_set_sleep(bool sleeping);/* called by power handling */
void app_power_hint(bool show);   /* "Hold to power off…" overlay */
void app_refresh_status(void);    /* call ~1 Hz */
void app_preview_set_page(int i); /* host previews */
void app_ota_tick(void);         /* call ~5 Hz: update prompt, progress, rollback notice */
void app_open_settings(void);
void app_home_button(void);       /* BOOT press */
