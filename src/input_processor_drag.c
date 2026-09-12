/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT roba_input_processor_drag
#include <limits.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <drivers/input_processor.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include "gesture_stroke.h"
#include "gesture_queue.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
K_MUTEX_DEFINE(drag_lock);
static struct roba_stroke stroke = {.direction = -1};
static int32_t frame_x, frame_y;

/* P and slash share a mode. Each physical press owns its own tap decision. */
static struct held_key {
    bool known, active, used, letter_pressed;
    uint32_t position;
} held[2];

static int active_keys(void) { return held[0].active + held[1].active; }

/* The existing devicetree names are retained for keymap compatibility.
 * Gestures now emit keyboard taps; no mouse button or drag report is sent. */
static const uint32_t shortcut_codes[] = {
    LC(RIGHT_ARROW), LC(LEFT_ARROW), LC(LA(UP_ARROW)), LC(LA(DOWN_ARROW))};
static struct roba_stroke_queue shortcuts;
static int pressed_direction = -1;
static int64_t next_press_at;
static struct zmk_behavior_binding shortcut_binding;
static struct zmk_behavior_binding_event shortcut_event;
static void press_shortcut(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(press_work, press_shortcut);

static void release_shortcut(struct k_work *work) {
    k_mutex_lock(&drag_lock, K_FOREVER);
    if (pressed_direction >= 0) {
        shortcut_event.timestamp = k_uptime_get();
        int err = zmk_behavior_invoke_binding(&shortcut_binding, shortcut_event, false);
        if (err < 0) LOG_ERR("Gesture shortcut release failed: %d", err);
        pressed_direction = -1;
    }
    next_press_at = k_uptime_get() + 10;
    if (shortcuts.count) k_work_reschedule(&press_work, K_MSEC(10));
    k_mutex_unlock(&drag_lock);
}
K_WORK_DELAYABLE_DEFINE(release_work, release_shortcut);

static void press_shortcut(struct k_work *work) {
    k_mutex_lock(&drag_lock, K_FOREVER);
    if (pressed_direction >= 0) {
        k_mutex_unlock(&drag_lock);
        return;
    }
    pressed_direction = roba_stroke_dequeue(&shortcuts);
    if (pressed_direction >= 0) {
        shortcut_binding = (struct zmk_behavior_binding){
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
            .param1 = shortcut_codes[pressed_direction]};
        shortcut_event = (struct zmk_behavior_binding_event){
            .layer = 7, .position = INT32_MAX, .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };
        int err = zmk_behavior_invoke_binding(&shortcut_binding, shortcut_event, true);
        if (err < 0) LOG_ERR("Gesture shortcut press failed: %d", err);
        /* Always release, including after a partially successful press. */
        k_work_reschedule(&release_work, K_MSEC(30));
    }
    k_mutex_unlock(&drag_lock);
}

static void reset_gesture(void) {
    roba_stroke_reset(&stroke);
    frame_x = frame_y = 0;
    /* Already recognized strokes finish their short key taps even if P is
     * released before work runs. A release never triggers a new shortcut. */
}

static void process_frame(int32_t x, int32_t y) {
    int direction = roba_stroke_feed(&stroke, x, y, k_uptime_get());
    if (stroke.engaged) {
        for (int i = 0; i < ARRAY_SIZE(held); ++i)
            if (held[i].active) held[i].used = true;
    }
    if (direction < 0) return;
    if (!roba_stroke_enqueue(&shortcuts, direction)) {
        LOG_ERR("Gesture shortcut queue full");
        return;
    }
    if (pressed_direction < 0) {
        int64_t wait = next_press_at - k_uptime_get();
        k_work_schedule(&press_work, K_MSEC(wait > 0 ? wait : 0));
    }
}

static int mode_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    if (binding->param1 != 7) return -EINVAL;
    k_mutex_lock(&drag_lock, K_FOREVER);
    int slot = -1;
    for (int i = 0; i < ARRAY_SIZE(held); ++i) {
        if (held[i].known && held[i].position == event.position) { slot = i; break; }
        if (!held[i].known && slot < 0) slot = i;
    }
    if (slot < 0 || held[slot].active) {
        k_mutex_unlock(&drag_lock);
        return -ENOMEM;
    }
    if (!active_keys()) reset_gesture();
    /* Keep the position-to-slot mapping after mode release. Standard hold-tap
     * can release its hold before its letter's key-up; another mode key must
     * not overwrite that letter decision in between. */
    held[slot] = (struct held_key){.known = true, .active = true,
        .used = stroke.engaged, .position = event.position};
    zmk_keymap_layer_activate(7);
    k_mutex_unlock(&drag_lock);
    return 0;
}

static int mode_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    k_mutex_lock(&drag_lock, K_FOREVER);
    for (int i = 0; i < ARRAY_SIZE(held); ++i) {
        if (held[i].active && held[i].position == event.position) {
            held[i].active = false;
            break;
        }
    }
    if (!active_keys()) {
        reset_gesture();
        /* Zephyr mutexes allow same-thread re-entry by the synchronous layer
         * listener. Keep active-key state and layer teardown atomic. */
        zmk_keymap_layer_deactivate(7);
    }
    k_mutex_unlock(&drag_lock);
    return 0;
}

/* Preserve ZMK hold-tap's event capture and typing order. The hold branch
 * remembers whether a gesture was used; only the ordinary tap is suppressed. */
static int letter_state(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event, bool pressed) {
    bool forward = true;
    k_mutex_lock(&drag_lock, K_FOREVER);
    for (int i = 0; i < ARRAY_SIZE(held); ++i) {
        if (held[i].known && held[i].position == event.position) {
            if (pressed) held[i].letter_pressed = !held[i].used;
            forward = held[i].letter_pressed;
            if (!pressed) held[i].letter_pressed = false;
            break;
        }
    }
    k_mutex_unlock(&drag_lock);
    if (!forward) return 0;
    struct zmk_behavior_binding key = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)), .param1 = binding->param1};
    return zmk_behavior_invoke_binding(&key, event, pressed);
}
static int letter_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    return letter_state(binding, event, true);
}
static int letter_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    return letter_state(binding, event, false);
}
static const struct behavior_driver_api mode_api = {
    .binding_pressed = mode_pressed, .binding_released = mode_released};
static const struct behavior_driver_api letter_api = {
    .binding_pressed = letter_pressed, .binding_released = letter_released};
#define DRAG_MODE_DEFINE(node) \
    BEHAVIOR_DT_DEFINE(node, NULL, NULL, NULL, NULL, POST_KERNEL, \
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mode_api);
#define DRAG_LETTER_DEFINE(node) \
    BEHAVIOR_DT_DEFINE(node, NULL, NULL, NULL, NULL, POST_KERNEL, \
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &letter_api);
DT_FOREACH_STATUS_OKAY(roba_behavior_drag_mode, DRAG_MODE_DEFINE)
DT_FOREACH_STATUS_OKAY(roba_behavior_drag_letter, DRAG_LETTER_DEFINE)

static int layer_changed(const zmk_event_t *event) {
    const struct zmk_layer_state_changed *change = as_zmk_layer_state_changed(event);
    if (change->layer == 7 && !change->state) {
        k_mutex_lock(&drag_lock, K_FOREVER);
        reset_gesture();
        k_mutex_unlock(&drag_lock);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(roba_drag_layer, layer_changed);
ZMK_SUBSCRIPTION(roba_drag_layer, zmk_layer_state_changed);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int handle_event(const struct device *dev, struct input_event *event,
                        uint32_t p1, uint32_t p2,
                        struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y))
        return ZMK_INPUT_PROC_CONTINUE;
    k_mutex_lock(&drag_lock, K_FOREVER);
    if (!active_keys() || !zmk_keymap_layer_active(7)) {
        k_mutex_unlock(&drag_lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code == INPUT_REL_X)
        frame_x = roba_stroke_clamp((int64_t)frame_x + event->value);
    else
        frame_y = roba_stroke_clamp((int64_t)frame_y + event->value);
    bool sync = event->sync;
    /* v0.3 layer overrides swallow STOP. Also remove the event and sync bit
     * so their listener cannot leak pointer movement into a shortcut gesture. */
    event->value = 0;
    event->type = 0;
    event->sync = false;
    if (sync) {
        int32_t x = frame_x, y = frame_y;
        frame_x = frame_y = 0;
        process_frame(x, y);
    }
    k_mutex_unlock(&drag_lock);
    return ZMK_INPUT_PROC_STOP;
}
static const struct zmk_input_processor_driver_api processor_api = {.handle_event = handle_event};
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &processor_api);
#endif
