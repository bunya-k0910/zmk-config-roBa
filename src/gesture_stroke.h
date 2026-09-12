/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ROBA_STROKE_THRESHOLD 64
#define ROBA_STROKE_IDLE_MS 80

/* Directions: right, left, up, down. No distance-based repeat. */
struct roba_stroke {
    int32_t score[4];
    int direction;
    int64_t last_motion;
    bool moving, engaged;
};

static inline int32_t roba_stroke_clamp(int64_t n) {
    return n > 32767 ? 32767 : n < -32767 ? -32767 : (int32_t)n;
}
static inline void roba_stroke_reset(struct roba_stroke *s) {
    *s = (struct roba_stroke){.direction = -1};
}
static inline int roba_stroke_feed(struct roba_stroke *s, int32_t x, int32_t y,
                                   int64_t now) {
    if (!x && !y) return -1;
    if (!s->moving || now - s->last_motion >= ROBA_STROKE_IDLE_MS)
        roba_stroke_reset(s);
    s->moving = true;
    s->last_motion = now;
    int64_t ax = x < 0 ? -(int64_t)x : x;
    int64_t ay = y < 0 ? -(int64_t)y : y;
    /* Cross-axis motion opposes the candidate: slight diagonal drift must
     * not accumulate into a new vertical action during a long horizontal roll.
     * Scores use twice the sensor units to retain the 1/2-axis penalty. */
    int64_t delta[4] = {2LL*x-ay, -2LL*x-ay, -2LL*y-ax, 2LL*y-ax};
    int best = -1;
    int32_t peak = 0;
    bool tied = false;
    for (int i = 0; i < 4; ++i) {
        int64_t next = s->score[i] + delta[i];
        s->score[i] = i == s->direction || next < 0 ? 0 : roba_stroke_clamp(next);
        if (s->score[i] > peak) {
            best = i; peak = s->score[i]; tied = false;
        } else if (s->score[i] == peak) tied = true;
    }
    /* Match the previous small-motion typing suppression separately from
     * the larger displacement needed to emit a shortcut. */
    if (peak >= 8) s->engaged = true;
    if (peak < 2 * ROBA_STROKE_THRESHOLD || tied) return -1;
    s->direction = best;
    for (int i = 0; i < 4; ++i) s->score[i] = 0;
    return best;
}
