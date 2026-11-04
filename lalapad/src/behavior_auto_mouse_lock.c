/*
 * オートマウスレイヤーを固定する behavior（zmk,behavior-auto-mouse-lock、引数なし）。
 *
 * 押すたびに、オートマウスレイヤーの固定を切り替える（input_processor_auto_mouse_layer.c）。固定している間は、
 * 時間切れでも、マウスレイヤーで &trans のキーなどを押しても解除しない。マウスレイヤーに置いて使う
 * （ほかのレイヤーに置くと、押したときのレイヤーを有効にして固定する）。
 */

#define DT_DRV_COMPAT zmk_behavior_auto_mouse_lock

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "auto_mouse_layer.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_auto_mouse_lock_binding_pressed(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    auto_mouse_layer_toggle_lock(event.layer);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_auto_mouse_lock_binding_released(struct zmk_behavior_binding *binding,
                                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_auto_mouse_lock_driver_api = {
    .binding_pressed = on_auto_mouse_lock_binding_pressed,
    .binding_released = on_auto_mouse_lock_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define AUTO_MOUSE_LOCK_INST(n)                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_auto_mouse_lock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_LOCK_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
