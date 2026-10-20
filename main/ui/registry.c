#include "registry.h"
#include "hal.h"
#include <stdio.h>
extern const game_t GAME_TETRIS;
/* ---- Register games here ---- */
const game_t *const GAMES[] = { &GAME_TETRIS };
const int GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);

static void key(const game_t *g, char *b) { snprintf(b, 16, "hs_%s", g->id); }
int32_t game_high_score(const game_t *g) { char k[16]; key(g, k); return hal_kv_get(k, 0); }
void game_save_high_score(const game_t *g, int32_t s) {
    char k[16]; key(g, k);
    if (s > hal_kv_get(k, 0)) hal_kv_set(k, s);
}
