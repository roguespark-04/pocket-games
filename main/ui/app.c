#include "app.h"
#include "theme.h"
#include "gesture.h"
#include "hal.h"
#include <stdio.h>

static lv_obj_t *set_row_card;
void app_open_settings(void);
static inline bool hit(lv_obj_t *o, lv_point_t p, int pad) {
    lv_area_t a; lv_obj_get_coords(o, &a);
    return p.x >= a.x1 - pad && p.x <= a.x2 + pad && p.y >= a.y1 - pad && p.y <= a.y2 + pad;
}
static lv_obj_t *home, *icon_tile, *name_lbl, *best_lbl, *time_lbl, *dots[8];
static lv_obj_t *panel, *wifi_val, *sig_val, *bat_val, *hs_val[8];
static lv_obj_t *power_ov;
static gesture_t home_g, panel_g;
static int page;
static const game_t *running;
void app_ota_tick(void);
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
    if (g->has_saved && g->has_saved()) snprintf(b, sizeof b, "Tap to resume");
    else if (hs > 0) { fmt_thousands(n, sizeof n, hs); snprintf(b, sizeof b, "Best %s", n); } else snprintf(b, sizeof b, "Tap to play");
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
        lv_obj_align(dots[i], LV_ALIGN_CENTER, (i - (n - 1) / 2.0f) * 16, 148);
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
#define ROW_H 48
#define CARD_W 320
static lv_obj_t *ver_val, *upd_lbl, *upd_btn, *upd_btn_lbl, *upd_card;
static lv_obj_t *card(lv_obj_t *p, int y, int rows) {
    lv_obj_t *c = plain(p); lv_obj_set_size(c, CARD_W, rows * ROW_H);
    lv_obj_set_style_radius(c, 20, 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, C_SURFACE, 0); lv_obj_align(c, LV_ALIGN_TOP_MID, 0, y);
    for (int i = 1; i < rows; i++) {
        lv_obj_t *s = plain(c); lv_obj_set_size(s, CARD_W - 20, 1); lv_obj_set_pos(s, 20, i * ROW_H);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s, C_SEP, 0);
    }
    return c;
}
static lv_obj_t *row(lv_obj_t *c, int i, const char *key) {
    lv_obj_t *k = label(c, F_BODY, C_LABEL, key); lv_obj_align(k, LV_ALIGN_TOP_LEFT, 20, i * ROW_H + 12);
    lv_obj_t *v = label(c, F_BODY, C_LABEL2, ""); lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -20, i * ROW_H + 12);
    return v;
}
static void close_panel(void) { if (panel) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); show_page(); }
static void panel_cb(g_kind_t k, lv_point_t s, int arg, void *ud) {
    (void)arg; (void)ud;
    if (k == G_UP) { close_panel(); return; }
    if (k != G_TAP) return;
    lv_area_t a; lv_obj_get_coords(set_row_card, &a);
    if (s.y >= a.y2 - ROW_H && s.y <= a.y2 && s.x >= a.x1 && s.x <= a.x2) { app_open_settings(); return; }
    lv_obj_get_coords(upd_card, &a);
    bool in_upd_row = s.y >= a.y2 - ROW_H && s.y <= a.y2 && s.x >= a.x1 && s.x <= a.x2; /* whole row is the target */
    if (!in_upd_row) return;
    hal_ota_t o; hal_ota_get(&o);
    if (o.state == OTA_AVAILABLE) hal_ota_install();
    else if (o.state != OTA_CHECKING && o.state != OTA_INSTALLING && o.state != OTA_RESTARTING) hal_ota_check();
    app_refresh_status();
}
static void build_panel(void) {
    panel = plain(lv_layer_top()); lv_obj_set_size(panel, SCR, SCR);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_t *t = label(panel, F_HEAD, C_LABEL, "Status"); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *c1 = card(panel, 62, 3); set_row_card = c1;
    wifi_val = row(c1, 0, "Wi-Fi"); bat_val = row(c1, 1, "Battery"); sig_val = NULL;
    lv_obj_t *sv = row(c1, 2, "Settings"); lv_label_set_text(sv, LV_SYMBOL_RIGHT); lv_obj_set_style_text_font(sv, F_CAPTION, 0);
    lv_obj_align(sv, LV_ALIGN_TOP_RIGHT, -20, 2 * ROW_H + 15);
    upd_card = card(panel, 62 + 3 * ROW_H + 12, 2);
    ver_val = row(upd_card, 0, "Version");
    upd_lbl = label(upd_card, F_BODY, C_BLUE, "Check for Updates");
    lv_obj_align(upd_lbl, LV_ALIGN_TOP_LEFT, 20, ROW_H + 12);
    upd_btn = plain(upd_card); lv_obj_set_size(upd_btn, 84, 32);
    lv_obj_set_style_radius(upd_btn, 16, 0); lv_obj_set_style_bg_opa(upd_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(upd_btn, C_BLUE, 0); lv_obj_align(upd_btn, LV_ALIGN_TOP_RIGHT, -12, ROW_H + 8);
    upd_btn_lbl = label(upd_btn, F_CAPTION, C_LABEL, "Install"); lv_obj_center(upd_btn_lbl);
    lv_obj_add_flag(upd_btn, LV_OBJ_FLAG_HIDDEN);
    int y3 = 62 + 5 * ROW_H + 12 + 10;
    lv_obj_t *h = label(panel, F_CAPTION, C_LABEL2, "High Scores"); lv_obj_align(h, LV_ALIGN_TOP_LEFT, (SCR - CARD_W) / 2 + 20, y3);
    int n = GAME_COUNT > 2 ? 2 : GAME_COUNT;
    lv_obj_t *c2 = card(panel, y3 + 22, n);
    for (int i = 0; i < n; i++) hs_val[i] = row(c2, i, GAMES[i]->name);
    grabber(panel, SCR - 18);
    lv_obj_t *touch = plain(panel); lv_obj_set_size(touch, SCR, SCR);
    panel_g = (gesture_t){ .cb = panel_cb }; gesture_attach(touch, &panel_g);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
static const char *sig_word(int rssi) {
    return rssi >= -55 ? "Excellent" : rssi >= -67 ? "Good" : rssi >= -78 ? "Fair" : "Weak";
}
static void refresh_update_row(void) {
    hal_ota_t o; hal_ota_get(&o); char b[48];
    lv_label_set_text(ver_val, o.current);
    lv_color_t c = C_LABEL2; bool btn = false;
    switch (o.state) {
    case OTA_CHECKING: snprintf(b, sizeof b, "Checking..."); break;
    case OTA_UPTODATE: snprintf(b, sizeof b, "Up to date"); break;
    case OTA_NO_WIFI: snprintf(b, sizeof b, "No Wi-Fi, tap to retry"); break;
    case OTA_AVAILABLE: snprintf(b, sizeof b, "%s available", o.available); c = C_LABEL; btn = true; break;
    case OTA_INSTALLING: snprintf(b, sizeof b, "Installing %d%%", o.progress); c = C_LABEL; break;
    case OTA_RESTARTING: snprintf(b, sizeof b, "Restarting..."); c = C_LABEL; break;
    case OTA_FAILED: snprintf(b, sizeof b, "%s", o.error); c = C_RED; break;
    default: snprintf(b, sizeof b, "Check for Updates"); c = C_BLUE; break;
    }
    if (o.state == OTA_IDLE && o.rolled_back) { snprintf(b, sizeof b, "%s didn't start", o.failed_version); c = C_ORANGE; }
    lv_label_set_text(upd_lbl, b); lv_obj_set_style_text_color(upd_lbl, c, 0);
    if (btn) lv_obj_remove_flag(upd_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(upd_btn, LV_OBJ_FLAG_HIDDEN);
}
void app_refresh_status(void) {
    int h, m; char b[48];
    if (time_lbl) { if (hal_time_hm(&h, &m)) { snprintf(b, sizeof b, "%d:%02d", (h + 11) % 12 + 1, m); lv_label_set_text(time_lbl, b); } }
    if (!panel || lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN)) return;
    hal_wifi_t w; hal_wifi_get(&w);
    if (w.connected) snprintf(b, sizeof b, "%s, %s", w.ssid, sig_word(w.rssi)); else snprintf(b, sizeof b, "Not Connected");
    lv_label_set_text(wifi_val, b);
    hal_batt_t bt; hal_batt_get(&bt);
    snprintf(b, sizeof b, "%d%%%s", bt.percent, bt.charging ? ", Charging" : bt.usb ? ", Plugged In" : "");
    lv_label_set_text(bat_val, b);
    lv_obj_set_style_text_color(bat_val, bt.charging ? C_GREEN : (bt.percent <= 20 ? C_RED : C_LABEL2), 0);
    refresh_update_row();
    int n = GAME_COUNT > 2 ? 2 : GAME_COUNT;
    for (int i = 0; i < n; i++) { fmt_thousands(b, sizeof b, game_high_score(GAMES[i])); lv_label_set_text(hs_val[i], b); }
}
void app_open_panel(void) { lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN); app_refresh_status(); show_page(); }

/* ---------- Update prompt / progress / rollback notice (modal) ---------- */
static lv_obj_t *modal, *modal_pct;
static void modal_close(void) { if (modal) { lv_obj_delete(modal); modal = NULL; modal_pct = NULL; } }
static void modal_btn(lv_event_t *e) {
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    hal_ota_ack(); modal_close();
    if (id == 1) hal_ota_install();
}
static lv_obj_t *modal_base(const char *title, const char *body) {
    modal_close();
    modal = plain(lv_layer_top()); lv_obj_set_size(modal, SCR, SCR); lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(modal, C_BG, 0);
    lv_obj_t *t = label(modal, F_TITLE, C_LABEL, title); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_t *b = label(modal, F_BODY, C_LABEL2, body); lv_obj_set_width(b, 300);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 168);
    return modal;
}
static void mbtn(const char *txt, bool primary, int y, intptr_t id) {
    lv_obj_t *b = lv_button_create(modal); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 220, 52); lv_obj_set_style_radius(b, 26, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, primary ? C_BLUE : C_SURFACE2, 0);
    lv_obj_set_style_transform_scale(b, 246, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, 110, 0); lv_obj_set_style_transform_pivot_y(b, 26, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *l = label(b, F_BODY, C_LABEL, txt); lv_obj_center(l);
    lv_obj_add_event_cb(b, modal_btn, LV_EVENT_CLICKED, (void *)id);
}
static bool modal_is_progress;
/* Called ~5x/s. Prompts only on the home screen, never mid-game. */
void app_ota_tick(void) {
    hal_ota_t o; hal_ota_get(&o);
    if (o.state == OTA_INSTALLING || o.state == OTA_RESTARTING) {
        if (!modal || !modal_is_progress) {
            modal_base("Installing Update", "Keep the device on.");
            modal_pct = label(modal, F_DISPLAY, C_LABEL, "0%"); lv_obj_align(modal_pct, LV_ALIGN_TOP_MID, 0, 230);
            modal_is_progress = true;
        }
        char b[16]; if (o.state == OTA_RESTARTING) snprintf(b, sizeof b, "Done"); else snprintf(b, sizeof b, "%d%%", o.progress);
        lv_label_set_text(modal_pct, b);
        return;
    }
    if (modal && modal_is_progress) { modal_close(); modal_is_progress = false; app_refresh_status(); }
    if (modal || running) return;
    char b[96];
    if (o.rolled_back) {
        snprintf(b, sizeof b, "Version %s couldn't start, so %s was restored.", o.failed_version, o.current);
        modal_base("Update Didn't Start", b); mbtn("OK", true, 260, 0); modal_is_progress = false;
    } else if (o.prompt && o.state == OTA_AVAILABLE) {
        snprintf(b, sizeof b, "Version %s is ready to install. You're on %s.", o.available, o.current);
        modal_base("Update Available", b); mbtn("Install", true, 244, 1); mbtn("Later", false, 308, 0); modal_is_progress = false;
    }
}

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
/* ---------- Settings: brightness + volume ---------- */
static lv_obj_t *settings, *sl_bright, *sl_vol, *val_bright, *val_vol;
static void apply_setting(lv_obj_t *sl) {
    int v = lv_slider_get_value(sl); char b[8]; snprintf(b, sizeof b, "%d%%", v);
    if (sl == sl_bright) { hal_set_brightness(v); hal_kv_set("bright", v); lv_label_set_text(val_bright, b); }
    else { hal_set_volume(v); hal_kv_set("volume", v); lv_label_set_text(val_vol, b); hal_sfx(SFX_ROTATE); }
}
static void slider_ev(lv_event_t *e) { apply_setting(lv_event_get_target(e)); }
static void step_ev(lv_event_t *e) {
    intptr_t d = (intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sl = (d == 1 || d == -1) ? sl_bright : sl_vol;
    int step = (d > 0) ? 10 : -10;
    int v = lv_slider_get_value(sl) + step; int min = (sl == sl_bright) ? 10 : 0;
    lv_slider_set_value(sl, v < min ? min : v > 100 ? 100 : v, LV_ANIM_OFF); apply_setting(sl);
}
static lv_obj_t *round_btn(lv_obj_t *p, const char *sym, int x, int y, intptr_t id) {
    lv_obj_t *b = lv_button_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 48, 48); lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(b, C_SURFACE2, 0);
    lv_obj_set_style_bg_color(b, C_SEP, LV_STATE_PRESSED);
    lv_obj_set_pos(b, x, y);
    lv_obj_t *l = label(b, F_BODY, C_LABEL, sym); lv_obj_center(l);
    lv_obj_add_event_cb(b, step_ev, LV_EVENT_CLICKED, (void *)id);
    return b;
}
static lv_obj_t *setting_row(lv_obj_t *p, const char *name, int y, int val, int min, lv_obj_t **val_lbl, intptr_t id) {
    lv_obj_t *n = label(p, F_BODY, C_LABEL, name); lv_obj_set_pos(n, 83, y);
    *val_lbl = label(p, F_BODY, C_LABEL2, ""); lv_obj_align(*val_lbl, LV_ALIGN_TOP_RIGHT, -83, y);
    round_btn(p, LV_SYMBOL_MINUS, 63, y + 34, -id);
    round_btn(p, LV_SYMBOL_PLUS, SCR - 63 - 48, y + 34, id);
    lv_obj_t *sl = lv_slider_create(p);
    lv_obj_set_size(sl, SCR - 2 * 63 - 2 * 48 - 2 * 20, 8); lv_obj_set_pos(sl, 63 + 48 + 20, y + 34 + 20);
    lv_slider_set_range(sl, min, 100); lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C_SURFACE2, LV_PART_MAIN); lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, C_LABEL, LV_PART_KNOB); lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(sl, 0, 0);
    lv_obj_set_ext_click_area(sl, 20);   /* 48 px tall touch target */
    lv_obj_add_event_cb(sl, slider_ev, LV_EVENT_VALUE_CHANGED, NULL);
    char b[8]; snprintf(b, sizeof b, "%d%%", val); lv_label_set_text(*val_lbl, b);
    return sl;
}
static const int SLEEP_OPTS[] = { 30, 60, 120, 300, 0 };
static const char *SLEEP_LBL[] = { "30 s", "1 min", "2 min", "5 min", "Never" };
static lv_obj_t *sleep_pill[5];
static void sleep_paint(void) {
    int cur = hal_kv_get("sleep_s", 60);
    for (int i = 0; i < 5; i++) {
        bool on = SLEEP_OPTS[i] == cur;
        lv_obj_set_style_bg_color(sleep_pill[i], on ? C_BLUE : C_SURFACE2, 0);
    }
}
static void sleep_ev(lv_event_t *e) {
    int v = SLEEP_OPTS[(intptr_t)lv_event_get_user_data(e)];
    hal_kv_set("sleep_s", v); hal_set_sleep_after(v); sleep_paint();
}
static void settings_done(lv_event_t *e) { (void)e; lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN); }
void app_open_settings(void) {
    if (!settings) {
        settings = plain(lv_layer_top()); lv_obj_set_size(settings, SCR, SCR); lv_obj_add_flag(settings, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(settings, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(settings, C_BG, 0);
        lv_obj_t *t = label(settings, F_HEAD, C_LABEL, "Settings"); lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 42);
        sl_bright = setting_row(settings, "Brightness", 92, hal_kv_get("bright", 80), 10, &val_bright, 1);
        sl_vol = setting_row(settings, "Volume", 190, hal_kv_get("volume", 65), 0, &val_vol, 2);
        lv_obj_t *sa = label(settings, F_BODY, C_LABEL, "Sleep after"); lv_obj_set_pos(sa, 83, 288);
        for (int i = 0; i < 5; i++) {   /* segmented control, 60x44 targets */
            lv_obj_t *b = lv_button_create(settings); lv_obj_remove_style_all(b);
            lv_obj_set_size(b, 62, 44); lv_obj_set_style_radius(b, 12, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_pos(b, (SCR - (5 * 62 + 4 * 6)) / 2 + i * 68, 320);
            lv_obj_t *l = label(b, F_CAPTION, C_LABEL, SLEEP_LBL[i]); lv_obj_center(l);
            lv_obj_add_event_cb(b, sleep_ev, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            sleep_pill[i] = b;
        }
        sleep_paint();
        lv_obj_t *d = lv_button_create(settings); lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 140, 48); lv_obj_set_style_radius(d, 24, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(d, C_BLUE, 0);
        lv_obj_set_style_transform_scale(d, 246, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_x(d, 70, 0); lv_obj_set_style_transform_pivot_y(d, 24, 0);
        lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 380);
        lv_obj_t *l = label(d, F_BODY, C_LABEL, "Done"); lv_obj_center(l);
        lv_obj_add_event_cb(d, settings_done, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_remove_flag(settings, LV_OBJ_FLAG_HIDDEN);
}
/* BOOT = Home, from anywhere. Games auto-pause and keep their state. */
void app_home_button(void) {
    if (settings) lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
    if (panel) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    if (running) { if (running->on_sleep) running->on_sleep(true); app_go_home(); }
    else show_page();
}
void app_init(void) { build_home(); build_panel(); lv_screen_load(home); app_refresh_status(); }
