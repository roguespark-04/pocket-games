#pragma once
#include "lvgl.h"
/* A game. Add new games in registry.c only. */
typedef struct game {
    const char *id;          /* NVS key prefix, <= 8 chars */
    const char *name;
    lv_color_t tint;
    void (*draw_icon)(lv_obj_t *icon);        /* populate a 144x144 rounded tile */
    void (*start)(lv_obj_t *screen);          /* build game UI on an empty screen */
    void (*stop)(void);                       /* free resources; screen is deleted after */
    void (*on_sleep)(bool sleeping);          /* pause when screen sleeps */
    bool (*has_saved)(void);                  /* optional: a paused game is waiting to resume */
} game_t;
extern const game_t *const GAMES[];
extern const int GAME_COUNT;
int32_t game_high_score(const game_t *g);
void game_save_high_score(const game_t *g, int32_t score); /* only if higher */
