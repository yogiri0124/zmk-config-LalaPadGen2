/*
 * マウスボタンの押しっぱなし切り替え behavior (&mbt LCLK / MCLK / RCLK / MB4 / MB5)。
 *
 * 押すたびに、指定したマウスボタンを押しっぱなしにする／離す。
 * このbehavior自身を入力デバイスとして INPUT_BTN_* を出し、mouse_button_toggle.dtsi の入力リスナー
 * （&mkp の mkp_input_listener と同じ形）が HID のマウスボタンに反映する。
 * 押しっぱなし中に同じボタンのクリック（&mkp による押下。トラックパッドのタップも含む）が来たら、
 * 押しっぱなしを解除する。ZMK v0.3.0 はマウスボタンの押下回数を数えているので、ここで解除しないと
 * クリックしてもボタンが押されたままになる。自分の押下は &mkp の入力としては現れないので、
 * &mkp の入力を監視すれば「ほかからのクリック」だけを確実に見分けられる。
 */

#define DT_DRV_COMPAT zmk_behavior_mouse_button_toggle

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>

#include <dt-bindings/zmk/pointing.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MKP_NODE DT_NODELABEL(mkp)
#define NUM_BUTTONS ZMK_HID_MOUSE_NUM_BUTTONS

/* bit i: このbehaviorがボタン i を押しっぱなしにしている */
static atomic_t locked;
/* 入力コールバックから依頼された解除（bit i）。システムワークキューで処理する */
static atomic_t unlock_requests;
/* 入力イベントを出すデバイス（このbehavior自身）。最初に押されたときに記録する */
static const struct device *mbt_dev;

static void report_button(const struct device *dev, int button, bool pressed) {
    input_report_key(dev, INPUT_BTN_0 + button, pressed ? 1 : 0, true, K_FOREVER);
}

static void unlock_work_handler(struct k_work *work) {
    const atomic_val_t requests = atomic_clear(&unlock_requests);
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if ((requests & BIT(i)) && mbt_dev != NULL && atomic_test_and_clear_bit(&locked, i)) {
            LOG_DBG("Mouse button %d unlocked by click", i);
            report_button(mbt_dev, i, false);
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

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "左 (Mouse1)", .value = LCLK, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "右 (Mouse2)", .value = RCLK, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "中 (Mouse3)", .value = MCLK, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Mouse4", .value = MB4, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.display_name = "Mouse5", .value = MB5, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
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

static int on_mbt_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    mbt_dev = dev;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!(binding->param1 & BIT(i))) {
            continue;
        }
        if (atomic_test_and_clear_bit(&locked, i)) {
            // 押しっぱなし中 → 離す（もう一度押しても外れる）
            report_button(dev, i, false);
        } else {
            atomic_set_bit(&locked, i);
            report_button(dev, i, true);
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
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define MBT_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_mbt_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MBT_INST)
