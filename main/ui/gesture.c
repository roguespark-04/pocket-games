#include "gesture.h"
#include <stdlib.h>
#define SWIPE_MIN 36
static void ev(lv_event_t *e) {
    gesture_t *g = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active(); if (!in) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        g->start = g->last = p; g->t0 = lv_tick_get(); g->active = true; g->step_acc = 0; g->dragged = false; break;
    case LV_EVENT_PRESSING:
        if (!g->active) break;
        if (g->drag_step > 0) {
            int dx = p.x - g->last.x, dy = p.y - g->start.y;
            if (abs(p.x - g->start.x) > abs(dy) || g->dragged) {
                g->step_acc += dx;
                while (g->step_acc >= g->drag_step) { g->step_acc -= g->drag_step; g->dragged = true; g->cb(G_DRAG_X, g->start, 1, g->ud); }
                while (g->step_acc <= -g->drag_step) { g->step_acc += g->drag_step; g->dragged = true; g->cb(G_DRAG_X, g->start, -1, g->ud); }
            }
        }
        g->last = p; break;
    case LV_EVENT_RELEASED: {
        if (!g->active) break; g->active = false;
        int dx = g->last.x - g->start.x, dy = g->last.y - g->start.y;
        if (abs(dx) < SWIPE_MIN && abs(dy) < SWIPE_MIN) { if (!g->dragged) g->cb(G_TAP, g->start, 0, g->ud); }
        else if (abs(dy) > abs(dx)) g->cb(dy > 0 ? G_DOWN : G_UP, g->start, dy, g->ud);
        else if (!g->dragged) g->cb(dx > 0 ? G_RIGHT : G_LEFT, g->start, dx, g->ud);
        break; }
    default: break;
    }
}
void gesture_attach(lv_obj_t *o, gesture_t *g) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(o, ev, LV_EVENT_ALL, g);
}
