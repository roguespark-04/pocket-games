#include "lvgl.h"
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#define N 466
static uint16_t fb[N * N];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    uint16_t *s = (uint16_t *)px; int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) fb[y * N + x] = s[(y - a->y1) * w + (x - a->x1)];
    lv_display_flush_ready(d);
}
static void save(const char *path) {
    lv_refr_now(NULL);
    FILE *f = fopen(path, "wb"); fwrite(fb, 2, N * N, f); fclose(f);
}
void tetris_preview_state(void);
extern const game_t GAME_TETRIS;
int main(void) {
    lv_init();
    lv_display_t *d = lv_display_create(N, N);
    static uint8_t buf[N * 24 * 2]; /* 24-line strips, like device */
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    app_init();
    save("raw_home.rgb565");
    app_open_panel(); save("raw_panel.rgb565");
    lv_obj_add_flag(lv_obj_get_child(lv_layer_top(), 0), LV_OBJ_FLAG_HIDDEN);
    app_launch(&GAME_TETRIS); tetris_preview_state();
    /* drop paused overlay for mid-game shot */
    lv_obj_t *scr = lv_screen_active(); lv_obj_delete(lv_obj_get_child(scr, -1));
    save("raw_tetris.rgb565");
    app_power_hint(true); save("raw_power.rgb565");
    return 0;
}
