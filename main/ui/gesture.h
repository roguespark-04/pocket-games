#pragma once
#include "lvgl.h"
/* Lightweight, immediate gesture tracker (no LVGL gesture delay). */
typedef enum { G_TAP, G_LEFT, G_RIGHT, G_UP, G_DOWN, G_DRAG_X } g_kind_t;
typedef struct {
    lv_point_t start, last; uint32_t t0; bool active; int step_acc; bool dragged;
    void (*cb)(g_kind_t k, lv_point_t start, int arg, void *ud); void *ud;
    int drag_step;           /* >0: emit G_DRAG_X every drag_step px of horizontal travel */
} gesture_t;
void gesture_attach(lv_obj_t *obj, gesture_t *g);
