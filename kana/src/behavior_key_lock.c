/*
 * キーの押しっぱなし切り替え behavior（zmk,behavior-key-lock、引数なし）。
 * keymap で keycode = <LCTRL> などを付けたインスタンスを作って使う。
 *
 * 押すたびに、指定したキー（修飾キーなど）を押しっぱなしにする／離す。
 * ZMK 標準の &kt（key-toggle の flip）と同じ動きで、引数を持たない分、
 * キーごとに名前付きのキーとして定義できる（LaLapad-Gen2-Editor の一覧が見やすくなる）。
 */

#define DT_DRV_COMPAT zmk_behavior_key_lock

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_key_lock_config {
    uint32_t keycode;
};

static int on_key_lock_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_key_lock_config *cfg = dev->config;

    // 押されていれば離す、離れていれば押す（もう一度押すと解除）
    const bool pressed = zmk_hid_is_pressed(cfg->keycode);
    return raise_zmk_keycode_state_changed_from_encoded(cfg->keycode, !pressed, event.timestamp);
}

static int on_key_lock_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_key_lock_driver_api = {
    .binding_pressed = on_key_lock_binding_pressed,
    .binding_released = on_key_lock_binding_released,
};

#define KEY_LOCK_INST(n)                                                                           \
    static const struct behavior_key_lock_config behavior_key_lock_config_##n = {                 \
        .keycode = DT_INST_PROP(n, keycode),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_key_lock_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_key_lock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KEY_LOCK_INST)
