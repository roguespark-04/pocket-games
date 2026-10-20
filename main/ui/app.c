#include "app.h"
#include "theme.h"
#include "gesture.h"
#include "hal.h"
#include <stdio.h>

static lv_obj_t *home, *icon_tile, *name_lbl, *best_lbl, *time_lbl, *dots[8];
static lv_obj_t *panel, *wifi_val, *sig_val, *bat_val, *hs_val[8];
static lv_obj_t *power_ov;
static gesture_t home_g, panel_g;
static int page;
static const game_t *running;
static lv_obj_t *game_scr;

void fmt_thousands(char *b, int n, int32_t v) {
    char t[16]; snprintf(t, sizeof t, "%ld", (long)v);
    int l = 0; while (t[l]) l++;
    int o = 0; for (int i = 0; i < l && o < n - 1; i++) { if (i && (l - i) % 3 == 0) b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, txt); lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE); return l;
}
static lv_obj_t *plain(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o); lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); return o;
}
static lv_obj_t *grabber(lv_obj_t *p, int y) {
    lv_obj_t *g = plain(p); lv_obj_set_size(g, 40, 5);
    lv_obj_set_style_radius(g, 3, 0); lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g, C_SEP, 0); lv_obj_align(g, LV_ALIGN_TOP_MID, 0, y); return g;
}

/* ---------- Home ---------- */
static void show_page(void) {
    const game_t *g = GAMES[page];
    lv_obj_clean(icon_tile);
    lv_obj_set_style_bg_color(icon_tile, g->tint, 0);
    g->draw_icon(icon_tile);
    lv_label_set_text(name_lbl, g->name);
    char b[32], n[16]; int32_t hs = game_high_score(g);
    if (hs > 0) { fmt_thousands(n, sizeof n, hs); snprintf(b, sizeof b, "Best %s", n); } else snprintf(b, sizeof b, "Tap to play");
    lv_label_set_text(best_lbl, b);
    for (int i = 0; i < GAME_COUNT && i < 8; i++)
        lv_obj_set_style_bg_color(dots[i], i == page ? C_LABEL : C_SEP, 0);
}
static bool in_icon(lv_point_t p) {
    lv_area_t a; lv_obj_get_coords(icon_tile, &a);
    return p.x >= a.x1 - 12 && p.x <= a.x2 + 12 && p.y >= a.y1 - 12 && p.y <= a.y2 + 60; /* tile + its name */
}
static void home_cb(g_kind_t k, lv_point_t s, int arg, void *ud) {
    (void)arg; (void)ud;
    if (k == G_TAP && in_icon(s)) app_launch(GAMES[page]);
    else if (k == G_DOWN && s.y < 140) app_open_panel();
    else if (k == G_LEFT && page < GAME_COUNT - 1) { page++; show_page(); }
    else if (k == G_RIGHT && page > 0) { page--; show_page(); }
}
static void icon_press(lv_event_t *e) { /* instant pressed state, no animation */
    lv_indev_t *in = lv_indev_active(); lv_point_t p; if (!in) return; lv_indev_get_point(in, &p);
    bool down = lv_event_get_code(e) == LV_EVENT_PRESSED && in_icon(p);
    lv_obj_set_style_transform_scale(icon_tile, down ? 246 : 256, 0); /* .96 */
}

static void build_home(void) {
    home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(home, C_BG, 0);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    grabber(home, 14);
    time_lbl = label(home, F_BODY, C_LABEL, "--:--");
    lv_obj_align(time_lbl, LV_ALIGN_TOP_MID, 0, 32);

    icon_tile = plain(home);
    lv_obj_set_size(icon_tile, 144, 144);
    lv_obj_set_style_radius(icon_tile, 36, 0);
    lv_obj_set_style_bg_opa(icon_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(icon_tile, 72, 0); lv_obj_set_style_transform_pivot_y(icon_tile, 72, 0);
    lv_obj_align(icon_tile, LV_ALIGN_CENTER, 0, -28);

    name_lbl = label(home, F_TITLE, C_LABEL, "");
    lv_obj_align_to(name_lbl, icon_tile, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    lv_obj_align(name_lbl, LV_ALIGN_CENTER, 0, 82);
    best_lbl = label(home, F_CAPTION, C_LABEL2, "");
    lv_obj_align(best_lbl, LV_ALIGN_CENTER, 0, 118);

    int n = GAME_COUNT > 8 ? 8 : GAME_COUNT;
    for (int i = 0; i < n; i++) {
        dots[i] = plain(home); lv_obj_set_size(dots[i], 8, 8);
        lv_obj_set_style_radius(dots[i], 4, 0); lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
        lv_obj_align(dots[i], LV_ALIGN_BOTTOM_MID, (i - (n - 1) / 2.0f) * 16, -40);
        if (n < 2) lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* full-screen touch surface on top */
    lv_obj_t *touch = plain(home); lv_obj_set_size(touch, SCR, SCR);
    home_g = (gesture_t){ .cb = home_cb };
    gesture_attach(touch, &home_g);
    lv_obj_add_event_cb(touch, icon_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch, icon_press, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch, icon_press, LV_EVENT_PRESS_LOST, NULL);
    show_page();
}

/* ---------- Pull-down status panel ---------- */
static lv_obj_t *card(lv_obj_t *p, int y, int rows) {
    lv_obj_t *c = plain(p); lv_obj_set_size(c, 340, rows * 52);
    lv_obj_set_style_radius(c, 20, 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, C_SURFACE, 0); lv_obj_align(c, LV_ALIGN_TOP_MID, 0, y);
    for (int i = 1; i < rows; i++) {
        lv_obj_t *s = plain(c); lv_obj_set_size(s, 340 - 20, 1); lv_obj_set_pos(s, 20, i * 52);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s, C_SEP, 0);
    }
    return c;
}
static lv_obj_t *row(lv_obj_t *c, int i, const char *key) {
    lv_obj_t *k = label(c, F_BODY, C_LABEL, key); lv_obj_align(k, LV_ALIGN_TOP_LEFT, 20, i * 52 + 14);
    lv_obj_t *v = label(c, F_BODY, C_LABEL2, ""); lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -20, i * 52 + 14);
    return v;
}
static void close_panel(void) { if (panel) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); show_page(); }
static void panel_cb(g_kind_t k, lv_point_t s, int arg, void *ud) {
    (void)s; (void)arg; (void)ud;
    if (k == G_UP || k == G_TAP) close_panel();
}
static void build_panel(void) {
    panel = plain(lv_layer_top()); lv_obj_set_size(panel, SCR, SCR);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_t *t = label(panel, F_HEAD, C_LABEL, "Status"); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *c1 = card(panel, 84, 3);
    wifi_val = row(c1, 0, "Wi-Fi"); sig_val = row(c1, 1, "Signal"); bat_val = row(c1, 2, "Battery");
    lv_obj_t *h = label(panel, F_CAPTION, C_LABEL2, "High Scores"); lv_obj_align(h, LV_ALIGN_TOP_LEFT, 63 + 20, 84 + 156 + 24);
    int n = GAME_COUNT > 4 ? 4 : GAME_COUNT;
    lv_obj_t *c2 = card(panel, 84 + 156 + 52, n);
    for (int i = 0; i < n; i++) hs_val[i] = row(c2, i, GAMES[i]->name);
    grabber(panel, SCR - 34);
    lv_obj_t *touch = plain(panel); lv_obj_set_size(touch, SCR, SCR);
    panel_g = (gesture_t){ .cb = panel_cb }; gesture_attach(touch, &panel_g);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
static const char *sig_word(int rssi) {
    return rssi >= -55 ? "Excellent" : rssi >= -67 ? "Good" : rssi >= -78 ? "Fair" : "Weak";
}
void app_refresh_status(void) {
    int h, m; char b[48];
    if (time_lbl) { if (hal_time_hm(&h, &m)) { snprintf(b, sizeof b, "%d:%02d", (h + 11) % 12 + 1, m); lv_label_set_text(time_lbl, b); } }
    if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) return;
    hal_wifi_t w; hal_wifi_get(&w);
    lv_label_set_text(wifi_val, w.connected ? w.ssid : "Not Connected");
    lv_obj_set_style_text_color(wifi_val, C_LABEL2, 0);
    if (w.connected) { snprintf(b, sizeof b, "%s, %d dBm", sig_word(w.rssi), w.rssi); lv_label_set_text(sig_val, b); }
    else lv_label_set_text(sig_val, "-");
    hal_batt_t bt; hal_batt_get(&bt);
    snprintf(b, sizeof b, "%d%%%s", bt.percent, bt.charging ? ", Charging" : bt.usb ? ", Plugged In" : "");
    lv_label_set_text(bat_val, b);
    lv_obj_set_style_text_color(bat_val, bt.charging ? C_GREEN : (bt.percent <= 20 ? C_RED : C_LABEL2), 0);
    int n = GAME_COUNT > 4 ? 4 : GAME_COUNT;
    for (int i = 0; i < n; i++) { fmt_thousands(b, sizeof b, game_high_score(GAMES[i])); lv_label_set_text(hs_val[i], b); }
}
void app_open_panel(void) { lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN); app_refresh_status(); }

/* ---------- Navigation ---------- */
void app_launch(const game_t *g) {
    running = g;
    game_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(game_scr, C_BG, 0);
    lv_obj_remove_flag(game_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(game_scr);
    g->start(game_scr);
}
void app_go_home(void) {
    if (running) { running->stop(); running = NULL; }
    lv_screen_load(home);
    if (game_scr) { lv_obj_delete_async(game_scr); game_scr = NULL; }
    show_page();
}
void app_set_sleep(bool s) { if (running && running->on_sleep) running->on_sleep(s); }
void app_power_hint(bool show) {
    if (!power_ov) {
        power_ov = plain(lv_layer_sys()); lv_obj_set_size(power_ov, SCR, SCR);
        lv_obj_set_style_bg_opa(power_ov, LV_OPA_80, 0); lv_obj_set_style_bg_color(power_ov, C_BG, 0);
        lv_obj_t *l = label(power_ov, F_HEAD, C_LABEL, "Hold to power off..."); lv_obj_center(l);
    }
    if (show) lv_obj_remove_flag(power_ov, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(power_ov, LV_OBJ_FLAG_HIDDEN);
}
void app_preview_set_page(int i) { page = i; show_page(); }
void app_init(void) { build_home(); build_panel(); lv_screen_load(home); app_refresh_status(); }
