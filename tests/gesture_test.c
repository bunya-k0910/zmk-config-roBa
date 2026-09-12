/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../src/gesture.h"
#include "../src/gesture_tap.h"

static struct roba_gesture state;
static int feed(int x, int y, int64_t time) {
    return roba_gesture_feed(&state, x, y, time, 240, 350);
}

int main(void) {
    /* Vertical: repeated movement, stopping, reversing, and moving sideways
     * must never close/reopen Mission Control until the activation is released. */
    assert(feed(20, -241, 10) == 2);
    for (int i = 1; i < 100; i++) {
        assert(feed(1000, i % 2 ? 1000 : -1000, 10 + i * 500) == -1);
    }
    roba_gesture_reset(&state);
    assert(feed(0, 241, 50000) == 3);
    roba_gesture_reset(&state);
    assert(feed(0, -241, 50001) == 2);

    /* Horizontal: continuous rolling/reversal cannot repeat; a real idle can. */
    roba_gesture_reset(&state);
    assert(feed(241, 0, 0) == 0);
    for (int i = 1; i <= 100; i++) {
        assert(feed(i % 2 ? -1000 : 1000, 1000, i * 100) == -1);
    }
    assert(feed(-241, 0, 10349) == -1);
    assert(feed(-241, 0, 10699) == 1);
    assert(feed(0, -241, 11049) == 2);
    assert(feed(241, 0, 12000) == -1);

    /* Compare complete XY frames, not X first. Ties await a clear direction. */
    roba_gesture_reset(&state);
    assert(feed(300, -500, 0) == 2);
    roba_gesture_reset(&state);
    assert(feed(-500, 300, 0) == 1);
    roba_gesture_reset(&state);
    assert(feed(300, -300, 0) == -1);
    assert(feed(0, -1, 10) == 2);

    /* No stale partial movement survives idle or a release/repress without motion. */
    roba_gesture_reset(&state);
    assert(feed(200, 0, 0) == -1);
    assert(feed(100, 0, 350) == -1);
    assert(feed(141, 0, 351) == 0);
    roba_gesture_reset(&state);
    assert(feed(100, 0, 352) == -1);
    roba_gesture_reset(&state);
    assert(feed(141, 0, 353) == -1);
    assert(feed(0, 0, 1000) == -1);
    assert(feed(100, 0, 1001) == -1);
    assert(feed(140, 0, 1002) == -1);
    assert(feed(1, 0, 1003) == 0);
    /* Release/repress while the old key is still down: the new vertical
     * gesture must run after release even if no more movement arrives. */
    struct roba_gesture_tap tap = {.pending = -1, .pressed = -1};
    roba_gesture_reset(&state);
    assert(roba_gesture_tap_submit(&tap, feed(241, 0, 2000)));
    assert(roba_gesture_tap_press(&tap) == 0);
    roba_gesture_reset(&state);
    roba_gesture_tap_cancel(&tap);
    assert(!roba_gesture_tap_submit(&tap, feed(0, -241, 2010)));
    assert(tap.pending == 2 && tap.pressed == 0);
    assert(roba_gesture_tap_press(&tap) == -1);
    assert(roba_gesture_tap_release(&tap));
    assert(roba_gesture_tap_press(&tap) == 2);
    assert(!roba_gesture_tap_release(&tap));
    assert(feed(0, -1000, 2040) == -1);

    /* Releasing before queued work runs cancels it, including while another
     * key is down; cancellation never discards the old key's release. */
    assert(roba_gesture_tap_submit(&tap, 1));
    roba_gesture_tap_cancel(&tap);
    assert(roba_gesture_tap_press(&tap) == -1);
    assert(roba_gesture_tap_submit(&tap, 0));
    assert(roba_gesture_tap_press(&tap) == 0);
    assert(!roba_gesture_tap_submit(&tap, 3));
    roba_gesture_tap_cancel(&tap);
    assert(tap.pressed == 0);
    assert(!roba_gesture_tap_release(&tap));
    assert(roba_gesture_tap_press(&tap) == -1);
    puts("gesture tests passed");
}
