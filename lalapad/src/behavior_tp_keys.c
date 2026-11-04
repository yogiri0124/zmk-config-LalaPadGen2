/*
 * トラックパッドの動きをキーに変える behavior（zmk,behavior-tp-keys、引数なし）。
 *
 * 押している間だけ、トラックパッドで指を動かした量 cursor-step（2 本指は scroll-step）ごとに、
 * その向きの binding を 1 回送る。bindings は 8 つ: 1 本指を上・下・左・右、2 本指を上・下・左・右へ動かしたとき
 * （2 本指も指を動かした向き。スクロールの向きではない）。
 * 1 本指（または 2 本指）の 4 つがすべて &trans なら、そちらは変えずにカーソル（スクロール）のまま通す。
 * 実際に動きを変えるのは、トラックパッドのリスナーに入れた zmk,input-processor-tp-keys（tp_keys.c）。
 */

#define DT_DRV_COMPAT zmk_behavior_tp_keys

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include "tp_keys.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static const struct behavior_driver_api behavior_tp_keys_driver_api = {
    .binding_pressed = tp_keys_binding_pressed,
    .binding_released = tp_keys_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define TP_KEYS_INST(n)                                                                            \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == TP_KEYS_BINDINGS_LEN,                            \
                 "tp-keys: bindings needs 8 entries (1 finger up/down/left/right, "                \
                 "2 fingers up/down/left/right)");                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, cursor_step) > 0 && DT_INST_PROP(n, cursor_step) <= UINT16_MAX,   \
                 "tp-keys: cursor-step must be 1..65535");                                         \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_step) > 0 && DT_INST_PROP(n, scroll_step) <= UINT16_MAX,   \
                 "tp-keys: scroll-step must be 1..65535");                                         \
    BUILD_ASSERT(DT_INST_PROP(n, tap_ms) >= 0 && DT_INST_PROP(n, tap_ms) <= UINT16_MAX,            \
                 "tp-keys: tap-ms must be 0..65535");                                              \
    static const struct zmk_behavior_binding tp_keys_bindings_##n[] = {LISTIFY(                    \
        DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))};         \
    static const struct tp_keys_mode tp_keys_mode_##n = {                                          \
        .kind = TP_KEYS_KIND_MOTION,                                                               \
        .bindings = tp_keys_bindings_##n,                                                          \
        .num_bindings = ARRAY_SIZE(tp_keys_bindings_##n),                                          \
        .cursor_step = DT_INST_PROP(n, cursor_step),                                               \
        .scroll_step = DT_INST_PROP(n, scroll_step),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &tp_keys_mode_##n, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tp_keys_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_KEYS_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
