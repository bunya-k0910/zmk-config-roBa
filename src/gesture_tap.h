/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

/* Keep only the current activation's pending tap, never a repeat backlog. */
struct roba_gesture_tap {
    int pending;
    int pressed;
};

static inline void roba_gesture_tap_cancel(struct roba_gesture_tap *tap) {
    tap->pending = -1;
}

static inline bool roba_gesture_tap_submit(struct roba_gesture_tap *tap, int direction) {
    tap->pending = direction;
    return tap->pressed < 0;
}

static inline int roba_gesture_tap_press(struct roba_gesture_tap *tap) {
    if (tap->pressed >= 0 || tap->pending < 0) {
        return -1;
    }
    tap->pressed = tap->pending;
    tap->pending = -1;
    return tap->pressed;
}

static inline bool roba_gesture_tap_release(struct roba_gesture_tap *tap) {
    tap->pressed = -1;
    return tap->pending >= 0;
}
