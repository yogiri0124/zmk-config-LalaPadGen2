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
#include <zmk/keymap.h>

#include "tp_keys.h"

static const struct behavior_driver_api behavior_tp_stroke_driver_api = {
    .binding_pressed = tp_keys_binding_pressed,
    .binding_released = tp_keys_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define TP_STROKE_INST(n)                                                                          \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == DT_INST_PROP_LEN(n, strokes),                    \
                 "tp-stroke: strokes and bindings need the same number of entries");               \
    BUILD_ASSERT(DT_INST_PROP(n, stroke_step) > 0 && DT_INST_PROP(n, stroke_step) <= UINT16_MAX,   \
                 "tp-stroke: stroke-step must be 1..65535");                                       \
    BUILD_ASSERT(DT_INST_PROP(n, tap_ms) >= 0 && DT_INST_PROP(n, tap_ms) <= UINT16_MAX,            \
                 "tp-stroke: tap-ms must be 0..65535");                                            \
    static const uint16_t tp_stroke_strokes_##n[] = DT_INST_PROP(n, strokes);                      \
    static const struct zmk_behavior_binding tp_stroke_bindings_##n[] = {LISTIFY(                  \
        DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))};         \
    static const struct tp_keys_mode tp_stroke_mode_##n = {                                        \
        .kind = TP_KEYS_KIND_STROKE,                                                               \
        .bindings = tp_stroke_bindings_##n,                                                        \
        .num_bindings = ARRAY_SIZE(tp_stroke_bindings_##n),                                        \
        .strokes = tp_stroke_strokes_##n,                                                          \
        .stroke_step = DT_INST_PROP(n, stroke_step),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &tp_stroke_mode_##n, POST_KERNEL,                 \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_stroke_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_STROKE_INST)
