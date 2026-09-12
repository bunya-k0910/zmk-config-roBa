/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

/* Serializes brief key taps, not screen animations. Never replaces an older
 * accepted direction with a newer one. Four entries bound abnormal bursts. */
#define ROBA_STROKE_QUEUE_SIZE 4
struct roba_stroke_queue { int items[ROBA_STROKE_QUEUE_SIZE]; unsigned head, count; };
static inline bool roba_stroke_enqueue(struct roba_stroke_queue *q, int direction) {
    if (q->count == ROBA_STROKE_QUEUE_SIZE) return false;
    q->items[(q->head + q->count) % ROBA_STROKE_QUEUE_SIZE] = direction;
    ++q->count;
    return true;
}
static inline int roba_stroke_dequeue(struct roba_stroke_queue *q) {
    if (!q->count) return -1;
    int direction = q->items[q->head];
    q->head = (q->head + 1) % ROBA_STROKE_QUEUE_SIZE;
    --q->count;
    return direction;
}
