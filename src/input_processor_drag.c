/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT roba_input_processor_drag
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <drivers/input_processor.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include "gesture_drag.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
K_MUTEX_DEFINE(drag_lock);
static struct roba_drag_state drag;
static int32_t frame_x, frame_y;
static bool button_down;
/* P and slash share a mode. Each physical press owns its own tap decision. */
static struct held_key {
    bool active, used;
    uint32_t position;
    int64_t pressed_at;
} held[2];

static int active_keys(void) { return held[0].active + held[1].active; }

static int send_motion(int32_t delta, int axis) {
    zmk_hid_mouse_movement_set(axis == 1 ? delta : 0, axis == 2 ? delta : 0);
    int err = zmk_endpoints_send_mouse_report();
    zmk_hid_mouse_movement_set(0, 0);
    if (err < 0) LOG_WRN("Gesture mouse report failed: %d", err);
    return err;
}
static void end_drag(void) {
    if (button_down) {
        zmk_hid_mouse_button_release(2);
        button_down = false;
        send_motion(0, 0);
    }
    roba_drag_reset(&drag);
    frame_x = frame_y = 0;
}

static void process_frame(int32_t x, int32_t y) {
    struct roba_drag_step step = roba_drag_feed(&drag, x, y);
    if (step.begin) {
        for (int i = 0; i < ARRAY_SIZE(held); ++i)
            if (held[i].active) held[i].used = true;
        zmk_hid_mouse_button_press(2);
        button_down = true;
        if (send_motion(0, 0) < 0) { end_drag(); return; }
        /* MMF 3.0.8 consumes the first >7px frame solely to choose an axis.
         * Keep that frame separate from the actual preview displacement. */
        if (send_motion(step.delta < 0 ? -16 : 16, step.axis) < 0) {
            end_drag(); return;
        }
    }
    if (step.delta && send_motion(step.delta, step.axis) < 0) {
        end_drag(); return;
    }
    /* This follows the final movement report, never precedes it. The next
     * physical sensor frame may start a fresh drag with P still held. */
    if (step.end) end_drag();
}

static int key_pressed(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    if (binding->param1 != 7) return -EINVAL;
    k_mutex_lock(&drag_lock, K_FOREVER);
    for (int i = 0; i < ARRAY_SIZE(held); ++i) {
        if (!held[i].active) {
            if (!active_keys()) end_drag();
            held[i] = (struct held_key){.active = true, .used = button_down,
                .position = event.position, .pressed_at = event.timestamp};
            zmk_keymap_layer_activate(7);
            k_mutex_unlock(&drag_lock);
            return 0;
        }
    }
    k_mutex_unlock(&drag_lock);
    return -ENOMEM;
}
static int key_released(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    bool tap = false;
    k_mutex_lock(&drag_lock, K_FOREVER);
    for (int i = 0; i < ARRAY_SIZE(held); ++i) {
        if (held[i].active && held[i].position == event.position) {
            tap = !held[i].used && event.timestamp - held[i].pressed_at < 200;
            held[i].active = false;
            break;
        }
    }
    bool last_key = !active_keys();
    if (last_key) end_drag();
    k_mutex_unlock(&drag_lock);
    /* Layer listeners run synchronously; notify them outside our state lock. */
    if (last_key) zmk_keymap_layer_deactivate(7);
    if (tap) {
        struct zmk_behavior_binding key = {
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)), .param1 = binding->param2};
        int err = zmk_behavior_invoke_binding(&key, event, true);
        /* Always release, including after a partially successful press. */
        int release_err = zmk_behavior_invoke_binding(&key, event, false);
        return err < 0 ? err : release_err;
    }
    return 0;
}
static const struct behavior_driver_api key_api = {
    .binding_pressed = key_pressed, .binding_released = key_released};
#define DRAG_KEY_DEFINE(node) \
    BEHAVIOR_DT_DEFINE(node, NULL, NULL, NULL, NULL, POST_KERNEL, \
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &key_api);
DT_FOREACH_STATUS_OKAY(roba_behavior_drag_tap, DRAG_KEY_DEFINE)

static int layer_changed(const zmk_event_t *event) {
    const struct zmk_layer_state_changed *change = as_zmk_layer_state_changed(event);
    if (change->layer == 7 && !change->state) {
        k_mutex_lock(&drag_lock, K_FOREVER);
        end_drag();
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
        frame_x = roba_drag_clamp((int64_t)frame_x + event->value, 32767);
    else
        frame_y = roba_drag_clamp((int64_t)frame_y + event->value, 32767);
    bool sync = event->sync;
    /* v0.3 layer overrides swallow STOP. Also remove the event and sync bit
     * so their listener cannot send a duplicate report after our ordered ones. */
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
