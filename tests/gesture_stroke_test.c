/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../src/gesture_stroke.h"
#include "../src/gesture_queue.h"

int main(void) {
    struct roba_stroke s = {.direction = -1};
    /* Deliberate motion suppresses typing before it emits a shortcut. */
    assert(roba_stroke_feed(&s, 4, 0, 0) == -1 && s.engaged);
    assert(roba_stroke_feed(&s, 60, 0, 8) == 0);
    /* A long flick, even with minor diagonal drift, is exactly one action. */
    for (int i = 1; i <= 1000; ++i)
        assert(roba_stroke_feed(&s, 100, 5, 8 + 8*i) == -1);
    /* Right/right/left with 130 ms pauses, without a mode reset. */
    assert(roba_stroke_feed(&s, 64, 0, 8138) == 0);
    assert(roba_stroke_feed(&s, -64, 0, 8268) == 1);

    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 64, 0, 0) == 0);
    /* Small return motion must not undo the action. A clear reversal can
     * start the next stroke without an idle or returning to the old origin. */
    assert(roba_stroke_feed(&s, -44, 0, 16) == -1);
    assert(roba_stroke_feed(&s, 44, 0, 32) == -1);
    assert(roba_stroke_feed(&s, -32, 0, 48) == -1);
    assert(roba_stroke_feed(&s, -32, 0, 64) == 1);
    assert(roba_stroke_feed(&s, -10000, 0, 72) == -1);
    assert(roba_stroke_feed(&s, 0, -64, 88) == 2);
    assert(roba_stroke_feed(&s, 0, -10000, 96) == -1);
    assert(roba_stroke_feed(&s, 0, -64, 226) == 2);
    assert(roba_stroke_feed(&s, 0, 64, 234) == 3);

    /* Boundary: idle measures nonzero frames, including suppressed movement. */
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 64, 0, 0) == 0);
    assert(roba_stroke_feed(&s, 64, 0, 79) == -1);
    assert(roba_stroke_feed(&s, 0, 0, 150) == -1);
    assert(roba_stroke_feed(&s, 64, 0, 159) == 0);
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 32, 0, 0) == -1);
    assert(roba_stroke_feed(&s, 32, 0, 80) == -1);
    assert(roba_stroke_feed(&s, 32, 0, 88) == 0);
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 32, 0, 89) == -1);
    /* Complete XY frames determine the dominant direction. */
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 200, -200, 0) == -1);
    assert(roba_stroke_feed(&s, 1, 0, 8) == 0);
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, 100, -200, 0) == 2);
    roba_stroke_reset(&s);
    assert(roba_stroke_feed(&s, INT_MAX, 0, 0) == 0);
    assert(roba_stroke_feed(&s, INT_MIN, 0, 8) == 1);
    assert(roba_stroke_feed(&s, 0, INT_MIN, 16) == 2);
    assert(roba_stroke_feed(&s, 0, INT_MAX, 24) == 3);

    /* Accepted taps retain their order and survive a recognition reset
     * (P release); resetting a partial stroke adds no new action. */
    struct roba_stroke_queue q = {0};
    assert(roba_stroke_enqueue(&q, 0));
    assert(roba_stroke_enqueue(&q, 0));
    assert(roba_stroke_enqueue(&q, 1));
    roba_stroke_reset(&s);
    assert(roba_stroke_dequeue(&q) == 0);
    assert(roba_stroke_dequeue(&q) == 0);
    assert(roba_stroke_dequeue(&q) == 1);
    assert(roba_stroke_dequeue(&q) == -1);
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 4; ++i) assert(roba_stroke_enqueue(&q, i));
        assert(!roba_stroke_enqueue(&q, 0));
        for (int i = 0; i < 4; ++i) assert(roba_stroke_dequeue(&q) == i);
    }
    puts("gesture stroke tests passed");
}
