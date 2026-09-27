/*
 * マウスボタンの押しっぱなし切り替え behavior (&mbt LCLK / MCLK / RCLK / MB4 / MB5)。
 *
 * 押すたびに、指定したマウスボタンを押しっぱなしにする／離す（&mkp を押す・離す）。
 * 押しっぱなし中に同じボタンのクリック（&mkp による押下。トラックパッドのタップも含む）が来たら、
 * 押しっぱなしを解除する。ZMK v0.3.0 はマウスボタンの押下回数を数えているので、ここで解除しないと
 * クリックしてもボタンが押されたままになる。
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
#define MKP_BEHAVIOR_NAME DEVICE_DT_NAME(MKP_NODE)
#define NUM_BUTTONS ZMK_HID_MOUSE_NUM_BUTTONS

/* bit i: このbehaviorがボタン i を押しっぱなしにしている */
static atomic_t locked;
/* ボタン i について、このbehavior自身が起こした押下イベントの数（入力コールバックで無視する） */
static atomic_t own_presses[NUM_BUTTONS];
/* 入力コールバックから依頼された解除（bit i）。システムワークキューで処理する */
static atomic_t unlock_requests;
static struct zmk_behavior_binding_event last_event;

static void invoke_mkp(int button, bool pressed, struct zmk_behavior_binding_event event) {
    struct zmk_behavior_binding mkp = {
        .behavior_dev = MKP_BEHAVIOR_NAME,
        .param1 = BIT(button),
    };
    zmk_behavior_invoke_binding(&mkp, event, pressed);
}

static void unlock_work_handler(struct k_work *work) {
    const atomic_val_t requests = atomic_clear(&unlock_requests);
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if ((requests & BIT(i)) && atomic_test_and_clear_bit(&locked, i)) {
            LOG_DBG("Mouse button %d unlocked by click", i);
            invoke_mkp(i, false, last_event);
        }
    }
}

static K_WORK_DEFINE(unlock_work, unlock_work_handler);

/* &mkp が出す入力イベントを監視する（入力スレッドで呼ばれるので、解除そのものはワークに回す） */
static void mkp_input_callback(struct input_event *evt) {
    if (evt->type != INPUT_EV_KEY || evt->value == 0 || evt->code < INPUT_BTN_0 ||
        evt->code >= INPUT_BTN_0 + NUM_BUTTONS) {
        return;
    }

    const int button = evt->code - INPUT_BTN_0;

    if (atomic_get(&own_presses[button]) > 0) {
        atomic_dec(&own_presses[button]);
        return;
    }

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
    last_event = event;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (!(binding->param1 & BIT(i))) {
            continue;
        }
        if (atomic_test_and_clear_bit(&locked, i)) {
            // 押しっぱなし中 → 離す（もう一度押しても外れる）
            invoke_mkp(i, false, event);
        } else {
            // 押しっぱなしにする。この押下は自分のものなので入力コールバックでは無視させる
            atomic_inc(&own_presses[i]);
            atomic_set_bit(&locked, i);
            invoke_mkp(i, true, event);
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
