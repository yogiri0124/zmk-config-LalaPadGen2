/*
 * トラックパッドの動きをキーに変える behavior（zmk,behavior-tp-keys、引数なし）。
 *
 * 押している間だけ、トラックパッドを動かした量 cursor-step（2 本指のスクロールは scroll-step）ごとに、
 * その向きの binding を 1 回送る。bindings は 8 つ: 1 本指の上・下・左・右、2 本指の上・下・左・右。
 * 1 本指（または 2 本指）の 4 つがすべて &trans なら、そちらは変えずにカーソル（スクロール）のまま通す。
 * 実際に動きを変えるのは、トラックパッドのリスナーに入れた zmk,input-processor-tp-keys（tp_keys.c）。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_keys

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "tp_keys.h"

#define TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))

static bool any_converted(const struct zmk_behavior_binding *b) {
    for (int i = 0; i < 4; i++) {
        if (strcmp(b[i].behavior_dev, TRANS_BEHAVIOR_NAME) != 0) {
            return true;
        }
    }
    return false;
}

static int behavior_tp_keys_init(const struct device *dev) {
    struct tp_keys_mode *mode = dev->data;
    mode->convert_cursor = any_converted(&mode->bindings[TP_KEYS_CURSOR_FIRST]);
    mode->convert_scroll = any_converted(&mode->bindings[TP_KEYS_SCROLL_FIRST]);
    return 0;
}

static int on_tp_keys_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    tp_keys_mode_pressed(dev->data, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tp_keys_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    tp_keys_mode_released(dev->data, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tp_keys_driver_api = {
    .binding_pressed = on_tp_keys_binding_pressed,
    .binding_released = on_tp_keys_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define TP_KEYS_BINDING(idx, n)                                                                    \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)),                  \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param1))),                     \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param2))),                     \
    }

#define TP_KEYS_INST(n)                                                                            \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == TP_KEYS_BINDINGS_LEN,                            \
                 "tp-keys: bindings needs 8 entries (cursor up/down/left/right, "                  \
                 "scroll up/down/left/right)");                                                    \
    static const struct zmk_behavior_binding tp_keys_bindings_##n[] = {                            \
        LISTIFY(TP_KEYS_BINDINGS_LEN, TP_KEYS_BINDING, (, ), n)};                                  \
    static struct tp_keys_mode tp_keys_mode_##n = {                                                \
        .bindings = tp_keys_bindings_##n,                                                          \
        .cursor_step = DT_INST_PROP(n, cursor_step),                                               \
        .scroll_step = DT_INST_PROP(n, scroll_step),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_tp_keys_init, NULL, &tp_keys_mode_##n, NULL, POST_KERNEL,  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_keys_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_KEYS_INST)
