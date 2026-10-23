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
 *
 * 接続先（USB・BLE プロファイル）が変わったときは、押しっぱなしを解除する。ZMK は接続先が変わると
 * PC へ送る状態を消すが、押した数（ZMK の修飾キーごとの数）は残るので、ここで「離す」を送って数を合わせる
 * （そうしないと、次に押したときに解除になり、もう一度押すまで押しっぱなしにできない）。
 * 解除するのは、切り替わった時点で押しっぱなしだったものだけ（印を付けておき、ワークで離す）。
 * 印の付いたキーをワークより先に押したときは、まず古い押しっぱなしを離してから、新しく押しっぱなしにする。
 */

#define DT_DRV_COMPAT zmk_behavior_key_lock

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_key_lock_config {
    uint32_t keycode;
};

/* 状態のビット（接続先の切り替えの通知は別のスレッドから届くことがあるので atomic で持つ） */
enum {
    KEY_LOCK_LOCKED,         /* 押しっぱなしにしている */
    KEY_LOCK_UNLOCK_PENDING, /* 接続先が切り替わった時点で押しっぱなしだった（ワークで離す） */
};

struct behavior_key_lock_data {
    atomic_t flags;
};

/* 接続先の切り替えで印を付けたものなら、離す（システムワークキューで呼ぶ） */
static void unlock_if_pending(const struct behavior_key_lock_config *cfg,
                              struct behavior_key_lock_data *data, int64_t timestamp) {
    if (atomic_test_and_clear_bit(&data->flags, KEY_LOCK_UNLOCK_PENDING) &&
        atomic_test_and_clear_bit(&data->flags, KEY_LOCK_LOCKED)) {
        raise_zmk_keycode_state_changed_from_encoded(cfg->keycode, false, timestamp);
    }
}

static void unlock_all_work_handler(struct k_work *work);
static K_WORK_DEFINE(unlock_all_work, unlock_all_work_handler);

static int on_key_lock_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_key_lock_config *cfg = dev->config;
    struct behavior_key_lock_data *data = dev->data;

    // 接続先が切り替わる前の押しっぱなしが残っていれば、先に離す（このキーは新しく押しっぱなしにする操作になる）
    unlock_if_pending(cfg, data, event.timestamp);
    // 押しっぱなしなら離す、そうでなければ押す（もう一度押すと解除）
    const bool locked = !atomic_test_bit(&data->flags, KEY_LOCK_LOCKED);
    atomic_set_bit_to(&data->flags, KEY_LOCK_LOCKED, locked);
    raise_zmk_keycode_state_changed_from_encoded(cfg->keycode, locked, event.timestamp);
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

#define KEY_LOCK_DEV(n) DEVICE_DT_INST_GET(n),

static const struct device *const key_lock_devs[] = {DT_INST_FOREACH_STATUS_OKAY(KEY_LOCK_DEV)};

/* 接続先が変わった: その時点で押しっぱなしだったキーを離す */
static void unlock_all_work_handler(struct k_work *work) {
    for (size_t i = 0; i < ARRAY_SIZE(key_lock_devs); i++) {
        unlock_if_pending(key_lock_devs[i]->config, key_lock_devs[i]->data, k_uptime_get());
    }
}

/* 別のスレッドから呼ばれることがあるので、ここでは押しっぱなしのものに印を付けるだけにして、離すのはワークで行う */
static int key_lock_endpoint_listener(const zmk_event_t *eh) {
    for (size_t i = 0; i < ARRAY_SIZE(key_lock_devs); i++) {
        struct behavior_key_lock_data *data = key_lock_devs[i]->data;
        if (atomic_test_bit(&data->flags, KEY_LOCK_LOCKED)) {
            atomic_set_bit(&data->flags, KEY_LOCK_UNLOCK_PENDING);
        }
    }
    k_work_submit(&unlock_all_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_key_lock, key_lock_endpoint_listener);
ZMK_SUBSCRIPTION(behavior_key_lock, zmk_endpoint_changed);
