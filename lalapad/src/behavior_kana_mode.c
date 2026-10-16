/*
 * かな配列モードのオン／オフ behavior (&kana_sw KANA_SW_ON / KANA_SW_OFF)。
 * 実際の処理（レイヤー切替と IME のオン／オフの順序管理）は kana_output.c で行う。
 */

#define DT_DRV_COMPAT zmk_behavior_kana_mode

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <dt-bindings/kana/kana.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_kana_mode_config {
    zmk_keymap_layer_id_t layer;
    const zmk_keymap_layer_id_t *sub_layers;
    size_t sub_layers_len;
    uint32_t tap_ms;
    uint32_t wait_ms;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "オン", .value = KANA_SW_ON, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "オフ", .value = KANA_SW_OFF, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int on_kana_mode_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kana_mode_config *cfg = dev->config;

    switch (binding->param1) {
    case KANA_SW_ON:
        kana_mode_on(&event, cfg->layer, cfg->tap_ms, cfg->wait_ms);
        break;
    case KANA_SW_OFF:
        kana_mode_off(&event, cfg->sub_layers, cfg->sub_layers_len, cfg->tap_ms, cfg->wait_ms);
        break;
    default:
        LOG_WRN("Unknown kana mode command %d", binding->param1);
        break;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_mode_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kana_mode_driver_api = {
    .binding_pressed = on_kana_mode_binding_pressed,
    .binding_released = on_kana_mode_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KANA_MODE_CHECK_SUB_LAYER(node_id, prop, idx)                                             \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LAYERS_LEN,                       \
                 "kana_sw: sub-layers is out of range");

#define KANA_MODE_INST(n)                                                                          \
    BUILD_ASSERT(DT_INST_PROP(n, layer) < ZMK_KEYMAP_LAYERS_LEN,                                   \
                 "kana_sw: layer is out of range");                                                \
    DT_INST_FOREACH_PROP_ELEM(n, sub_layers, KANA_MODE_CHECK_SUB_LAYER)                            \
    static const zmk_keymap_layer_id_t behavior_kana_mode_sub_layers_##n[] = {                     \
        DT_INST_FOREACH_PROP_ELEM_SEP(n, sub_layers, DT_PROP_BY_IDX, (, ))};                       \
    static const struct behavior_kana_mode_config behavior_kana_mode_config_##n = {               \
        .layer = DT_INST_PROP(n, layer),                                                           \
        .sub_layers = behavior_kana_mode_sub_layers_##n,                                           \
        .sub_layers_len = DT_INST_PROP_LEN(n, sub_layers),                                         \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kana_mode_config_##n, POST_KERNEL,      \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kana_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANA_MODE_INST)
