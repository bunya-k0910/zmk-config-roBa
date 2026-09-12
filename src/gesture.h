/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Direction indices retain the original right, left, up, down binding order. */
struct roba_gesture {
    int64_t x, y;
    int64_t last_motion;
    bool moving;
    bool fired;
    bool vertical_latched;
};

static inline void roba_gesture_reset(struct roba_gesture *state) {
    *state = (struct roba_gesture){0};
}

/* Feed complete XY frames, including suppressed movement, to measure real idle. */
static inline int roba_gesture_feed(struct roba_gesture *state, int32_t x, int32_t y,
                                    int64_t now, int32_t threshold, int32_t idle_ms) {
    if (x == 0 && y == 0) {
        return -1;
    }
    if (state->moving && now - state->last_motion >= idle_ms) {
        state->x = state->y = 0;
        state->fired = false;
    }
    state->moving = true;
    state->last_motion = now;
    if (state->vertical_latched || state->fired) {
        return -1;
    }

    state->x += x;
    state->y += y;
    int64_t ax = state->x < 0 ? -state->x : state->x;
    int64_t ay = state->y < 0 ? -state->y : state->y;
    int direction;
    if (ax > threshold && ax > ay) {
        direction = state->x > 0 ? 0 : 1;
    } else if (ay > threshold && ay > ax) {
        direction = state->y < 0 ? 2 : 3;
        state->vertical_latched = true;
    } else {
        return -1;
    }
    state->fired = true;
    state->x = state->y = 0;
    return direction;
}
