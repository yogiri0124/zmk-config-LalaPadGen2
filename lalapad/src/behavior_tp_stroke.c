/*
 * トラックパッドの一筆書きでキーを送る behavior（zmk,behavior-tp-stroke、引数なし）。
 *
 * 押している間、1 本指で描いた向きの並び（上下左右、最大 4 つ）を覚え、指を離したとき（または先に
 * このキーを離したとき）に、strokes の中で並びが一致するものの binding（bindings の同じ順番）を 1 回送る。
 * 描いている間はカーソルは動かない。2 本指のスクロールはそのまま通す。
 * 実際に動きを読むのは、トラックパッドのリスナーに入れた zmk,input-processor-tp-keys（tp_keys.c）。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_stroke

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "tp_keys.h"

static int on_tp_stroke_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    tp_keys_mode_pressed(dev->config, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tp_stroke_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    tp_keys_mode_released(dev->config, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_tp_stroke_driver_api = {
    .binding_pressed = on_tp_stroke_binding_pressed,
    .binding_released = on_tp_stroke_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define TP_STROKE_BINDING(idx, n)                                                                  \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)),                  \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param1))),                     \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param2))),                     \
    }

#define TP_STROKE_INST(n)                                                                          \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == DT_INST_PROP_LEN(n, strokes),                    \
                 "tp-stroke: strokes and bindings need the same number of entries");               \
    static const uint16_t tp_stroke_strokes_##n[] = DT_INST_PROP(n, strokes);                      \
    static const struct zmk_behavior_binding tp_stroke_bindings_##n[] = {                          \
        LISTIFY(DT_INST_PROP_LEN(n, bindings), TP_STROKE_BINDING, (, ), n)};                       \
    static const struct tp_keys_mode tp_stroke_mode_##n = {                                        \
        .strokes = tp_stroke_strokes_##n,                                                          \
        .stroke_bindings = tp_stroke_bindings_##n,                                                 \
        .num_strokes = DT_INST_PROP_LEN(n, strokes),                                               \
        .stroke_step = DT_INST_PROP(n, stroke_step),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &tp_stroke_mode_##n, POST_KERNEL,                 \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_stroke_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_STROKE_INST)
