/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT roba_input_processor_gesture

#include <limits.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include "gesture.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
BUILD_ASSERT(DT_INST_PROP_LEN(0, bindings) == 4);
BUILD_ASSERT(DT_INST_PROP(0, threshold) > 0);
BUILD_ASSERT(DT_INST_PROP(0, idle_ms) > DT_INST_PROP(0, tap_ms));

static const struct zmk_behavior_binding bindings[] = {
    LISTIFY(4, ZMK_KEYMAP_EXTRACT_BINDING, (,), DT_DRV_INST(0))};
static struct roba_gesture gesture;
static int32_t frame_x, frame_y;
static int pending_direction = -1;
static int pressed_direction = -1;
static bool busy;
static struct zmk_behavior_binding_event tap_event;
K_MUTEX_DEFINE(gesture_lock);

static void release_key(struct k_work *work) {
    k_mutex_lock(&gesture_lock, K_FOREVER);
    if (pressed_direction >= 0) {
        tap_event.timestamp = k_uptime_get();
        int err = zmk_behavior_invoke_binding(&bindings[pressed_direction], tap_event, false);
        if (err < 0) {
            LOG_ERR("Gesture key release failed: %d", err);
        }
        pressed_direction = -1;
    }
    busy = false;
    k_mutex_unlock(&gesture_lock);
}
K_WORK_DELAYABLE_DEFINE(release_work, release_key);

/* One pending tap at most; no repeat queue can outlive the physical gesture. */
static void press_key(struct k_work *work) {
    k_mutex_lock(&gesture_lock, K_FOREVER);
    if (pending_direction < 0 || !zmk_keymap_layer_active(DT_INST_PROP(0, layer))) {
        pending_direction = -1;
        busy = false;
        k_mutex_unlock(&gesture_lock);
        return;
    }
    pressed_direction = pending_direction;
    pending_direction = -1;
    tap_event = (struct zmk_behavior_binding_event){
        .layer = DT_INST_PROP(0, layer),
        .position = INT32_MAX,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
    int err = zmk_behavior_invoke_binding(&bindings[pressed_direction], tap_event, true);
    if (err < 0) {
        LOG_ERR("Gesture key press failed: %d", err);
    }
    /* Release even if the modifier/key press was only partly successful. */
    k_work_reschedule(&release_work, K_MSEC(DT_INST_PROP(0, tap_ms)));
    k_mutex_unlock(&gesture_lock);
}
K_WORK_DEFINE(press_work, press_key);

static int layer_changed(const zmk_event_t *event) {
    const struct zmk_layer_state_changed *change = as_zmk_layer_state_changed(event);
    if (change->layer == DT_INST_PROP(0, layer)) {
        k_mutex_lock(&gesture_lock, K_FOREVER);
        roba_gesture_reset(&gesture);
        frame_x = frame_y = 0;
        pending_direction = -1;
        /* A key already pressed must still receive its scheduled release. */
        k_mutex_unlock(&gesture_lock);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(roba_gesture_layer, layer_changed);
ZMK_SUBSCRIPTION(roba_gesture_layer, zmk_layer_state_changed);

static int handle_event(const struct device *dev, struct input_event *event,
                        uint32_t param1, uint32_t param2,
                        struct zmk_input_processor_state *processor_state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&gesture_lock, K_FOREVER);
    if (!zmk_keymap_layer_active(DT_INST_PROP(0, layer))) {
        frame_x = frame_y = 0;
        k_mutex_unlock(&gesture_lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code == INPUT_REL_X) {
        frame_x += event->value;
    } else {
        frame_y += event->value;
    }
    /* Also zero values: ZMK v0.3 layer overrides do not propagate STOP. */
    event->value = 0;
    if (event->sync) {
        int direction = roba_gesture_feed(&gesture, frame_x, frame_y, k_uptime_get(),
                                          DT_INST_PROP(0, threshold), DT_INST_PROP(0, idle_ms));
        frame_x = frame_y = 0;
        if (direction >= 0 && !busy) {
            busy = true;
            pending_direction = direction;
            k_work_submit(&press_work);
        }
    }
    k_mutex_unlock(&gesture_lock);
    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api api = {.handle_event = handle_event};
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
