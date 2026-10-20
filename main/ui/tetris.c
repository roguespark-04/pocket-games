/* Tetris: 10x20 well centered on the round screen. */
#include "registry.h"
#include "theme.h"
#include "gesture.h"
#include "app.h"
#include "hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define W 10
#define H 20
#define CELL 19
#define WX ((SCR - W * CELL) / 2)   /* 138 */
#define WY ((SCR - H * CELL) / 2)   /* 43 */
void fmt_thousands(char *b, int n, int32_t v);
extern const game_t GAME_TETRIS;

static const uint32_t COLORS[8] = { 0, 0x64D2FF, 0xFFD60A, 0xBF5AF2, 0x30D158, 0xFF453A, 0x0A84FF, 0xFF9F0A };
/* 7 pieces x 4 rotations, 4 cells as (x,y) in 4x4 box (SRS orientation). */
static const int8_t SHAPES[7][4][4][2] = {
 {{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}}, /* I */
 {{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}}}, /* O */
 {{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}}, /* T */
 {{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},{{1,1},{2,1},{0,2},{1,2}},{{0,0},{0,1},{1,1},{1,2}}}, /* S */
 {{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{1,2},{2,2}},{{1,0},{0,1},{1,1},{0,2}}}, /* Z */
 {{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}}, /* J */
 {{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}}, /* L */
};

typedef struct {
    uint8_t board[H][W];
    int piece, rot, px, py, next, hold; bool hold_used;
    uint8_t bag[7]; int bag_n;
    int32_t score; int level, lines;
    uint32_t last_fall, ground_since; bool grounded;
    bool paused, over, music, boot_prev;
} tetris_t;
static tetris_t *T;
static lv_obj_t *well, *next_box, *hold_box, *score_l, *level_l, *lines_l, *overlay, *pause_btn;
static lv_timer_t *tick;
static gesture_t gest;

static bool fits(int p, int r, int x, int y) {
    for (int i = 0; i < 4; i++) {
        int cx = x + SHAPES[p][r][i][0], cy = y + SHAPES[p][r][i][1];
        if (cx < 0 || cx >= W || cy >= H) return false;
        if (cy >= 0 && T->board[cy][cx]) return false;
    }
    return true;
}
static int bag_draw(void) {
    if (T->bag_n == 0) {
        for (int i = 0; i < 7; i++) T->bag[i] = i;
        for (int i = 6; i > 0; i--) { int j = rand() % (i + 1); uint8_t t = T->bag[i]; T->bag[i] = T->bag[j]; T->bag[j] = t; }
        T->bag_n = 7;
    }
    return T->bag[--T->bag_n];
}
static uint32_t fall_ms(void) {           /* guideline curve, ms per row */
    static const uint16_t tbl[] = {1000,793,618,473,355,262,190,135,94,64,43,28,18,11,7};
    int l = T->level - 1; if (l > 14) l = 14; return tbl[l] < 16 ? 16 : tbl[l];
}
static void show_overlay(bool over);
static void redraw_side(void);
static void spawn(int p) {
    T->piece = p; T->rot = 0; T->px = 3; T->py = (p == 0) ? -1 : 0; T->grounded = false;
    T->last_fall = lv_tick_get();
    if (!fits(T->piece, T->rot, T->px, T->py)) {
        T->over = true; hal_sfx(SFX_GAMEOVER); hal_music(false);
        game_save_high_score(&GAME_TETRIS, T->score); show_overlay(true);
    }
}
static void lock_piece(void) {
    for (int i = 0; i < 4; i++) {
        int cx = T->px + SHAPES[T->piece][T->rot][i][0], cy = T->py + SHAPES[T->piece][T->rot][i][1];
        if (cy >= 0) T->board[cy][cx] = T->piece + 1;
    }
    int cleared = 0;
    for (int y = H - 1; y >= 0; y--) {
        bool full = true; for (int x = 0; x < W; x++) if (!T->board[y][x]) { full = false; break; }
        if (full) { memmove(&T->board[1], &T->board[0], y * W); memset(T->board[0], 0, W); cleared++; y++; }
    }
    static const int pts[5] = {0, 100, 300, 500, 800};
    T->score += pts[cleared] * T->level;
    int old = T->level; T->lines += cleared; T->level = 1 + T->lines / 10;
    if (cleared == 4) hal_sfx(SFX_TETRIS); else if (cleared) hal_sfx(SFX_CLEAR); else hal_sfx(SFX_LOCK);
    if (T->level > old) hal_sfx(SFX_LEVEL);
    T->hold_used = false;
    int n = T->next; T->next = bag_draw(); spawn(n);
    redraw_side();
}
static void changed(void) { lv_obj_invalidate(well); }
static void move(int dx) {
    if (fits(T->piece, T->rot, T->px + dx, T->py)) { T->px += dx; if (T->grounded) T->ground_since = lv_tick_get(); hal_sfx(SFX_MOVE); changed(); }
}
static void rotate(void) {
    int nr = (T->rot + 1) % 4;
    static const int8_t kicks[][2] = {{0,0},{-1,0},{1,0},{0,-1},{-2,0},{2,0}};
    for (unsigned k = 0; k < sizeof kicks / 2; k++)
        if (fits(T->piece, nr, T->px + kicks[k][0], T->py + kicks[k][1])) {
            T->rot = nr; T->px += kicks[k][0]; T->py += kicks[k][1];
            if (T->grounded) T->ground_since = lv_tick_get();
            hal_sfx(SFX_ROTATE); changed(); return;
        }
}
static int ghost_y(void) { int y = T->py; while (fits(T->piece, T->rot, T->px, y + 1)) y++; return y; }
static void hard_drop(void) { int y = ghost_y(); T->score += 2 * (y - T->py); T->py = y; lock_piece(); changed(); }
static void do_hold(void) {
    if (T->hold_used) return;
    int cur = T->piece;
    if (T->hold < 0) { T->hold = cur; int n = T->next; T->next = bag_draw(); spawn(n); }
    else { int h = T->hold; T->hold = cur; spawn(h); }
    T->hold_used = true; hal_sfx(SFX_HOLD); redraw_side(); changed();
}

/* ---- drawing ---- */
static void cell_rect(lv_layer_t *layer, int x, int y, int size, lv_color_t c, bool ghost) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    if (ghost) { d.bg_opa = LV_OPA_TRANSP; d.border_color = c; d.border_width = 2; d.border_opa = LV_OPA_60; }
    else { d.bg_color = c; d.bg_opa = LV_OPA_COVER; }
    lv_area_t a = { x, y, x + size - 1, y + size - 1 };
    lv_draw_rect(layer, &d, &a);
}
static void well_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c; lv_obj_get_coords(well, &c);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int v = T->board[y][x];
        int sx = c.x1 + x * CELL + 1, sy = c.y1 + y * CELL + 1;
        if (v) cell_rect(layer, sx, sy, CELL - 2, lv_color_hex(COLORS[v]), false);
        else cell_rect(layer, sx + 7, sy + 7, 3, C_SURFACE2, false); /* quiet grid dots */
    }
    if (T->over) return;
    lv_color_t pc = lv_color_hex(COLORS[T->piece + 1]);
    int gy = ghost_y();
    for (int i = 0; i < 4; i++) {
        int cx = T->px + SHAPES[T->piece][T->rot][i][0];
        int cy = gy + SHAPES[T->piece][T->rot][i][1];
        if (cy >= 0 && gy != T->py) cell_rect(layer, c.x1 + cx * CELL + 1, c.y1 + cy * CELL + 1, CELL - 2, pc, true);
    }
    for (int i = 0; i < 4; i++) {
        int cx = T->px + SHAPES[T->piece][T->rot][i][0], cy = T->py + SHAPES[T->piece][T->rot][i][1];
        if (cy >= 0) cell_rect(layer, c.x1 + cx * CELL + 1, c.y1 + cy * CELL + 1, CELL - 2, pc, false);
    }
}
static void mini_draw(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    int p = (o == next_box) ? T->next : T->hold;
    if (p < 0) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c; lv_obj_get_coords(o, &c);
    const int s = 14; int minx = 4, maxx = 0, miny = 4, maxy = 0;
    for (int i = 0; i < 4; i++) { int x = SHAPES[p][0][i][0], y = SHAPES[p][0][i][1];
        if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
    int ox = c.x1 + (lv_area_get_width(&c) - (maxx - minx + 1) * s) / 2;
    int oy = c.y1 + (lv_area_get_height(&c) - (maxy - miny + 1) * s) / 2;
    lv_color_t col = lv_color_hex(COLORS[p + 1]);
    if (o == hold_box && T->hold_used) col = C_SEP;
    for (int i = 0; i < 4; i++)
        cell_rect(layer, ox + (SHAPES[p][0][i][0] - minx) * s + 1, oy + (SHAPES[p][0][i][1] - miny) * s + 1, s - 2, col, false);
}
static void redraw_side(void) {
    char b[24];
    fmt_thousands(b, sizeof b, T->score); lv_label_set_text(score_l, b);
    snprintf(b, sizeof b, "%d", T->level); lv_label_set_text(level_l, b);
    snprintf(b, sizeof b, "%d", T->lines); lv_label_set_text(lines_l, b);
    lv_obj_invalidate(next_box); lv_obj_invalidate(hold_box);
}

/* ---- overlay (pause / game over) ---- */
static void btn_cb(lv_event_t *e);
static lv_obj_t *mk_btn(lv_obj_t *p, const char *txt, bool primary, int y, intptr_t id) {
    lv_obj_t *b = lv_button_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 220, 52); lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, primary ? C_BLUE : C_SURFACE2, 0);
    lv_obj_set_style_transform_scale(b, 246, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, 110, 0); lv_obj_set_style_transform_pivot_y(b, 26, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, F_BODY, 0); lv_obj_set_style_text_color(l, C_LABEL, 0); lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)id);
    return b;
}
static void hide_overlay(void) { if (overlay) { lv_obj_delete(overlay); overlay = NULL; } }
static void show_overlay(bool over) {
    hide_overlay();
    overlay = lv_obj_create(lv_obj_get_parent(well)); lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, SCR, SCR); lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(overlay, C_BG, 0); lv_obj_set_style_bg_opa(overlay, 235, 0);
    lv_obj_t *t = lv_label_create(overlay); lv_obj_set_style_text_font(t, F_TITLE, 0);
    lv_obj_set_style_text_color(t, C_LABEL, 0); lv_label_set_text(t, over ? "Game Over" : "Paused");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_t *s = lv_label_create(overlay); lv_obj_set_style_text_font(s, F_BODY, 0);
    lv_obj_set_style_text_color(s, C_LABEL2, 0);
    char b[48], n[16]; fmt_thousands(n, sizeof n, T->score);
    int32_t best = game_high_score(&GAME_TETRIS);
    if (over && T->score > 0 && T->score >= best) snprintf(b, sizeof b, "%s  New Best", n);
    else { char bb[16]; fmt_thousands(bb, sizeof bb, best > T->score ? best : T->score); snprintf(b, sizeof b, "%s  " LV_SYMBOL_BULLET "  Best %s", n, bb); }
    lv_label_set_text(s, b); lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 148);
    if (over) { mk_btn(overlay, "Play Again", true, 210, 1); mk_btn(overlay, "Home", false, 274, 3); }
    else { mk_btn(overlay, "Resume", true, 196, 0); mk_btn(overlay, T->music ? "Music: On" : "Music: Off", false, 260, 2); mk_btn(overlay, "Home", false, 324, 3); }
}
static void reset_game(void) {
    memset(T, 0, sizeof *T); T->hold = -1; T->level = 1; T->music = hal_kv_get("tet_music", 1);
    T->next = bag_draw(); spawn(bag_draw());
}
static void btn_cb(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
    case 0: hide_overlay(); T->paused = false; T->last_fall = lv_tick_get(); hal_music(T->music); break;
    case 1: hide_overlay(); reset_game(); redraw_side(); changed(); hal_music(T->music); break;
    case 2: T->music = !T->music; hal_kv_set("tet_music", T->music); show_overlay(false); break;
    case 3: app_go_home(); break;
    }
}
static void pause_game(void) {
    if (T->over || T->paused) return;
    T->paused = true; hal_music(false); show_overlay(false);
}

/* ---- input / loop ---- */
static void gest_cb(g_kind_t k, lv_point_t s, int arg, void *ud) {
    (void)ud;
    if (T->paused || T->over) return;
    switch (k) {
    case G_DRAG_X: move(arg); break;
    case G_LEFT: move(-1); break;
    case G_RIGHT: move(1); break;
    case G_DOWN: hard_drop(); break;
    case G_TAP: {
        lv_area_t a; lv_obj_get_coords(pause_btn, &a);
        if (s.x >= a.x1 - 8 && s.x <= a.x2 + 8 && s.y >= a.y1 - 8 && s.y <= a.y2 + 8) pause_game(); else rotate();
        break; }
    default: break;
    }
}
static void loop(lv_timer_t *t) {
    (void)t;
    bool b = hal_boot_pressed();
    if (b && !T->boot_prev && !T->paused && !T->over) do_hold();
    T->boot_prev = b;
    if (T->paused || T->over) return;
    uint32_t now = lv_tick_get();
    if (fits(T->piece, T->rot, T->px, T->py + 1)) {
        T->grounded = false;
        if (now - T->last_fall >= fall_ms()) { T->py++; T->last_fall = now; changed(); }
    } else {
        if (!T->grounded) { T->grounded = true; T->ground_since = now; }
        if (now - T->ground_since >= 450) lock_piece(), changed();
    }
}

static lv_obj_t *side_label(lv_obj_t *p, const char *cap, int x, int y, lv_text_align_t al, lv_obj_t **val) {
    lv_obj_t *c = lv_label_create(p); lv_obj_set_style_text_font(c, F_CAPTION, 0);
    lv_obj_set_style_text_color(c, C_LABEL2, 0); lv_label_set_text(c, cap);
    lv_obj_set_width(c, 110); lv_obj_set_style_text_align(c, al, 0); lv_obj_set_pos(c, x, y);
    if (val) {
        *val = lv_label_create(p); lv_obj_set_style_text_font(*val, F_HEAD, 0);
        lv_obj_set_style_text_color(*val, C_LABEL, 0); lv_obj_set_width(*val, 110);
        lv_obj_set_style_text_align(*val, al, 0); lv_obj_set_pos(*val, x, y + 20);
    }
    return c;
}
static lv_obj_t *mini(lv_obj_t *p, int x, int y) {
    lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_size(o, 72, 48);
    lv_obj_set_pos(o, x, y); lv_obj_add_event_cb(o, mini_draw, LV_EVENT_DRAW_MAIN, NULL); return o;
}
static void start(lv_obj_t *scr) {
    T = heap_caps_calloc_psram(sizeof *T);
    well = lv_obj_create(scr); lv_obj_remove_style_all(well);
    lv_obj_set_size(well, W * CELL, H * CELL); lv_obj_set_pos(well, WX, WY);
    lv_obj_set_style_bg_color(well, lv_color_hex(0x0B0B0C), 0); lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(well, well_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    /* well edges as hairlines (structure, not decoration) */
    lv_obj_set_style_outline_color(well, C_SEP, 0); lv_obj_set_style_outline_width(well, 1, 0); lv_obj_set_style_outline_pad(well, 1, 0);
    /* left column: Score / Level / Lines (right-aligned toward well) */
    int lx = WX - 12 - 110;
    side_label(scr, "Score", lx, 150, LV_TEXT_ALIGN_RIGHT, &score_l);
    side_label(scr, "Level", lx, 214, LV_TEXT_ALIGN_RIGHT, &level_l);
    side_label(scr, "Lines", lx, 278, LV_TEXT_ALIGN_RIGHT, &lines_l);
    /* right column: Next / Hold (left-aligned toward well) */
    int rx = WX + W * CELL + 12;
    side_label(scr, "Next", rx, 130, LV_TEXT_ALIGN_LEFT, NULL); next_box = mini(scr, rx, 152);
    side_label(scr, "Hold", rx, 216, LV_TEXT_ALIGN_LEFT, NULL); hold_box = mini(scr, rx, 238);
    pause_btn = lv_obj_create(scr); lv_obj_remove_style_all(pause_btn);
    lv_obj_set_size(pause_btn, 44, 44); lv_obj_set_pos(pause_btn, rx + 4, 306);
    lv_obj_set_style_radius(pause_btn, 22, 0); lv_obj_set_style_bg_color(pause_btn, C_SURFACE2, 0);
    lv_obj_set_style_bg_opa(pause_btn, LV_OPA_COVER, 0);
    lv_obj_t *pl = lv_label_create(pause_btn); lv_label_set_text(pl, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(pl, F_CAPTION, 0); lv_obj_set_style_text_color(pl, C_LABEL, 0); lv_obj_center(pl);
    /* touch surface over everything except overlay */
    lv_obj_t *touch = lv_obj_create(scr); lv_obj_remove_style_all(touch); lv_obj_set_size(touch, SCR, SCR);
    gest = (gesture_t){ .cb = gest_cb, .drag_step = 24 }; gesture_attach(touch, &gest);
    reset_game(); redraw_side();
    hal_music(T->music);
    tick = lv_timer_create(loop, 10, NULL);
}
static void stop(void) {
    hal_music(false);
    if (tick) { lv_timer_delete(tick); tick = NULL; }
    if (T && !T->over) game_save_high_score(&GAME_TETRIS, T->score);
    overlay = NULL; heap_caps_free_psram(T); T = NULL;
}
static void on_sleep(bool s) { if (s) pause_game(); }

static void icon(lv_obj_t *tile) {  /* stacked tetrominoes, white on tint */
    static const int8_t cells[][2] = {{0,3},{1,3},{2,3},{3,3},{0,2},{1,2},{2,2},{1,1}};
    for (unsigned i = 0; i < sizeof cells / 2; i++) {
        lv_obj_t *c = lv_obj_create(tile); lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 26, 26); lv_obj_set_style_radius(c, 5, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(c, i < 4 ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_set_pos(c, 17 + cells[i][0] * 28, 3 + cells[i][1] * 28);
    }
}
void tetris_preview_state(void) {   /* deterministic mid-game board for previews */
    static const char *rows[] = {
      "..........","..........","..........","..........","..........","..........","..........",
      "..........","..........","..........","..........","..........","..........",
      "......L...","....SSL...","T..SS.LL..","TTJJJ.ZZ.I","TOOJ.ZZ.II","IOOJJLLL.I","IIIIZZOO.I"};
    static const char map[] = ".ITOSZJL"; /* I=1 O=2 T=3 S=4 Z=5 J=6 L=7 */
    const char *m = "_IOTSZJL";
    (void)map;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const char *p = strchr(m + 1, rows[y][x]); T->board[y][x] = p ? (p - m) : 0; }
    T->piece = 2; T->rot = 1; T->px = 1; T->py = 6; T->next = 0; T->hold = 4;
    T->score = 12840; T->level = 4; T->lines = 34; T->paused = true; /* freeze */
    redraw_side(); changed();
}
const game_t GAME_TETRIS = { .id = "tetris", .name = "Tetris", .tint = LV_COLOR_MAKE(0x0A, 0x84, 0xFF),
    .draw_icon = icon, .start = start, .stop = stop, .on_sleep = on_sleep };
