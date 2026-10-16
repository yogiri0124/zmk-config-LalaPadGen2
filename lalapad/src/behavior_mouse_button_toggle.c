/*
 * マウスボタンの押しっぱなし切り替え behavior（zmk,behavior-mouse-button-toggle、引数なし）。
 * keymap で button = <LCLK> などを付けたインスタンスを作って使う。
 *
 * 押すたびに、指定したマウスボタンを押しっぱなしにする／離す。
 * 入力イベントは共通の入力デバイス mouse_lock_input（zmk,mouse-button-lock-input）から INPUT_BTN_* として出し、
 * mouse_button_toggle.dtsi の入力リスナー（&mkp の mkp_input_listener と同じ形）が HID のマウスボタンに反映する。
 * 押しっぱなし中に同じボタンのクリック（&mkp による押下。トラックパッドのタップも含む）が来たら、
 * 押しっぱなしを解除する。ZMK v0.3.0 はマウスボタンの押下回数を数えているので、ここで解除しないと
 * クリックしてもボタンが押されたままになる。自分の押下は &mkp の入力としては現れないので、
 * &mkp の入力を監視すれば「ほかからのクリック」だけを確実に見分けられる。
 */

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---- 共通の入力デバイス（zmk,mouse-button-lock-input）---- */

#define DT_DRV_COMPAT zmk_mouse_button_lock_input

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "mouse_button_toggle.dtsi の mouse_lock_input がちょうど1つ必要");

#define LOCK_INPUT_DEV DEVICE_DT_GET(DT_NODELABEL(mouse_lock_input))

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      NULL);

#undef DT_DRV_COMPAT

/* ---- 押しっぱなしの状態（全インスタンスで共有）---- */

#define MKP_NODE DT_NODELABEL(mkp)
#define NUM_BUTTONS ZMK_HID_MOUSE_NUM_BUTTONS

/* bit i: ボタン i を押しっぱなしにしている */
static atomic_t locked;
/* 入力コールバックから依頼された解除（bit i）。システムワークキューで処理する */
static atomic_t unlock_requests;

static void report_button(int button, bool pressed) {
    input_report_key(LOCK_INPUT_DEV, INPUT_BTN_0 + button, pressed ? 1 : 0, true, K_FOREVER);
}

static void unlock_work_handler(struct k_work *work) {
    const atomic_val_t requests = atomic_clear(&unlock_requests);
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if ((requests & BIT(i)) && atomic_test_and_clear_bit(&locked, i)) {
            LOG_DBG("Mouse button %d unlocked by click", i);
            report_button(i, false);
        }
    }
}

static K_WORK_DEFINE(unlock_work, unlock_work_handler);

/* &mkp が出す入力イベント（＝ほかからのクリック）を監視する。入力スレッドで呼ばれるので、解除はワークに回す */
static void mkp_input_callback(struct input_event *evt) {
    if (evt->type != INPUT_EV_KEY || evt->value == 0 || evt->code < INPUT_BTN_0 ||
        evt->code >= INPUT_BTN_0 + NUM_BUTTONS) {
        return;
    }

    const int button = evt->code - INPUT_BTN_0;

    if (atomic_test_bit(&locked, button)) {
        atomic_set_bit(&unlock_requests, button);
        k_work_submit(&unlock_work);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(MKP_NODE), mkp_input_callback);

/* ---- behavior（zmk,behavior-mouse-button-toggle）---- */

#define DT_DRV_COMPAT zmk_behavior_mouse_button_toggle

struct behavior_mbt_config {
    uint32_t buttons;
};

static int on_mbt_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_mbt_config *cfg = dev->config;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!(cfg->buttons & BIT(i))) {
            continue;
        }
        if (atomic_test_and_clear_bit(&locked, i)) {
            // 押しっぱなし中 → 離す（もう一度押しても外れる）
            report_button(i, false);
        } else {
            atomic_set_bit(&locked, i);
            report_button(i, true);
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_mbt_binding_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_mbt_driver_api = {
    .binding_pressed = on_mbt_binding_pressed,
    .binding_released = on_mbt_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define MBT_INST(n)                                                                                \
    static const struct behavior_mbt_config behavior_mbt_config_##n = {                           \
        .buttons = DT_INST_PROP(n, button),                                                        \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_mbt_config_##n, POST_KERNEL,            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_mbt_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MBT_INST)
