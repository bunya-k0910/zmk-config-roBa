/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Raw sensor counts. No idle timer: each completed stroke re-arms immediately. */
#define ROBA_DRAG_GAIN 8
#define ROBA_DRAG_START 4
#define ROBA_DRAG_DISTANCE_X 160
#define ROBA_DRAG_DISTANCE_Y 96

struct roba_drag_state { int axis; int32_t x, y, offset; };
struct roba_drag_step { bool begin, end; int axis; int32_t delta; };

static inline int32_t roba_drag_clamp(int64_t value, int32_t limit) {
    return value > limit ? limit : value < -limit ? -limit : (int32_t)value;
}
static inline int32_t roba_drag_abs(int32_t n) { return n < 0 ? -n : n; }
static inline void roba_drag_reset(struct roba_drag_state *s) {
    *s = (struct roba_drag_state){0};
}
static inline struct roba_drag_step roba_drag_feed(struct roba_drag_state *s,
                                                    int32_t x, int32_t y) {
    struct roba_drag_step out = {0};
    int32_t d;
    if (!s->axis) {
        s->x = roba_drag_clamp((int64_t)s->x + x, 32767);
        s->y = roba_drag_clamp((int64_t)s->y + y, 32767);
        int32_t ax = roba_drag_abs(s->x), ay = roba_drag_abs(s->y);
        if (ax == ay || (ax < ROBA_DRAG_START && ay < ROBA_DRAG_START)) return out;
        s->axis = ax > ay ? 1 : 2;
        d = s->axis == 1 ? s->x : s->y;
        out.begin = true;
    } else {
        d = s->axis == 1 ? x : y;
    }
    out.axis = s->axis;
    int32_t limit = s->axis == 1 ? ROBA_DRAG_DISTANCE_X : ROBA_DRAG_DISTANCE_Y;
    int32_t next = roba_drag_clamp((int64_t)s->offset + d, limit);
    /* Returning through the origin cancels this preview. A later frame starts
     * the opposite stroke, so it cannot accidentally commit the old direction. */
    if ((s->offset > 0 && next <= 0) || (s->offset < 0 && next >= 0)) {
        next = 0;
        out.end = true;
    }
    out.delta = (next - s->offset) * ROBA_DRAG_GAIN;
    s->offset = next;
    if (roba_drag_abs(next) == limit) out.end = true;
    if (out.end) roba_drag_reset(s);
    return out;
}
