/*
 * キーの押しっぱなし切り替え behavior（zmk,behavior-key-lock、引数なし）。
 * keymap で keycode = <LCTRL> などを付けたインスタンスを作って使う。
 *
 * 押すたびに、指定したキー（修飾キーなど）を押しっぱなしにする／離す。
 * 引数を持たない分、キーごとに名前付きのキーとして定義できる（LaLapad-Gen2-Editor の一覧が見やすくなる）。
 *
 * 押しっぱなしかどうかは、インスタンスごとに自分で覚える（ZMK 標準の &kt のように PC 側の状態は見ない）。
 * そのため、ほかのキーで同じ修飾キーを押していても、それとは別に押しっぱなし／解除が切り替わる。
 * また、Alt コードの送信中（behavior_alt_code.c がキー入力を預かっている間）に押しても、正しく切り替わる。
 */

#define DT_DRV_COMPAT zmk_behavior_key_lock

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_key_lock_config {
    uint32_t keycode;
};

struct behavior_key_lock_data {
    /* 押しっぱなしにしているか（behavior のコールバックはシステムワークキューだけで動く） */
    bool locked;
};

static int on_key_lock_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_key_lock_config *cfg = dev->config;
    struct behavior_key_lock_data *data = dev->data;

    // 押しっぱなしなら離す、そうでなければ押す（もう一度押すと解除）
    data->locked = !data->locked;
    raise_zmk_keycode_state_changed_from_encoded(cfg->keycode, data->locked, event.timestamp);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_lock_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_key_lock_driver_api = {
    .binding_pressed = on_key_lock_binding_pressed,
    .binding_released = on_key_lock_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KEY_LOCK_INST(n)                                                                           \
    static const struct behavior_key_lock_config behavior_key_lock_config_##n = {                 \
        .keycode = DT_INST_PROP(n, keycode),                                                       \
    };                                                                                             \
    static struct behavior_key_lock_data behavior_key_lock_data_##n;                               \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_key_lock_data_##n,                            \
                            &behavior_key_lock_config_##n, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_key_lock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KEY_LOCK_INST)
