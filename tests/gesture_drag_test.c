/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../src/gesture_drag.h"
int main(void) {
    struct roba_drag_state s = {0};
    struct roba_drag_step r = roba_drag_feed(&s, 2, 0);
    assert(!r.begin && !r.delta);
    r = roba_drag_feed(&s, 6, 1);
    assert(r.begin && r.axis == 1 && r.delta == -64 && !r.end);
    r = roba_drag_feed(&s, 100, 0);
    assert(!r.begin && r.delta == -800 && !r.end);
    r = roba_drag_feed(&s, 100, 0);
    assert(r.end && r.delta == -416 && s.axis == 0);
    /* No release of P and no idle time needed to start another direction. */
    r = roba_drag_feed(&s, -20, 0);
    assert(r.begin && r.delta == 160 && !r.end);
    r = roba_drag_feed(&s, 30, 0);
    assert(r.end && r.delta == -160 && s.axis == 0);
    r = roba_drag_feed(&s, 0, -200);
    assert(r.begin && r.end && r.axis == 2 && r.delta == -768);
    r = roba_drag_feed(&s, 0, 200);
    assert(r.begin && r.end && r.axis == 2 && r.delta == 768);
    /* Large flicks produce one bounded transition, no queued overshoot. */
    r = roba_drag_feed(&s, INT_MAX, 0);
    assert(r.begin && r.end && r.delta == -1280);
    r = roba_drag_feed(&s, INT_MIN, 0);
    assert(r.begin && r.end && r.delta == 1280);
    roba_drag_reset(&s);
    r = roba_drag_feed(&s, 10, 10);
    assert(!r.begin);
    r = roba_drag_feed(&s, 1, 0);
    assert(r.begin && r.axis == 1);
    roba_drag_reset(&s);
    assert(s.axis == 0 && s.offset == 0);
    puts("gesture drag tests passed");
}
