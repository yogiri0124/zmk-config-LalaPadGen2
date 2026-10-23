/*
 * マウスボタンの押しっぱなし切り替え behavior（zmk,behavior-mouse-button-toggle、引数なし）。
 * keymap で button = <LCLK> などを付けたインスタンスを作って使う。
 *
 * 押すたびに、指定したマウスボタンを押しっぱなしにする／離す。
 * 押しっぱなし中に同じボタンのクリック（&mkp による押下。トラックパッドのタップも含む）が来たら、
 * 押しっぱなしを解除する。ZMK v0.3.0 はマウスボタンの押下回数を数えているので、ここで解除しないと
 * クリックしてもボタンが押されたままになる。&mkp のクリックは &mkp の入力デバイスから入力イベントとして出るので、
 * それを監視すれば「ほかからのクリック」だけを見分けられる。
 *
 * クリックと押しっぱなしの切り替えを、操作した順に扱うため、切り替えもすべて入力イベントの列に並べて、
 * 入力スレッドで1つずつ処理する。このキーを押したら、共通の入力デバイス mouse_lock_input
 * （zmk,mouse-button-lock-input）から「切り替えて」という依頼（INPUT_EV_MSC）を出し、入力スレッドで受け取って
 * HID のマウスボタンを押す／離す（ZMK の入力リスナーと同じく、入力スレッドで HID を変えて送る）。
 * クリックも同じ列から届くので、「クリック → 押しっぱなし」「押しっぱなし → クリック」の順番が入れ替わらない。
 *
 * 接続先（USB・BLE プロファイル）が変わったときも、押しっぱなしを解除する（同じ列に「全部離して」を並べる）。
 * 依頼には、接続先が何回切り替わったか（世代）を付ける。「全部離して」は、その世代より前に押しっぱなしにした
 * ものだけを離す。「切り替えて」は、受け取った時点の接続先の世代と違えば（切り替わる前に押されたもの）捨てる
 * （切り替わった後に押しっぱなしにしたものまで離したり、切り替え前の操作を切り替え先の PC に送ったりしないように）。
 * ZMK は接続先が変わるとキーボードの状態を消すが、押した数（ZMK のボタンごとの数）は残るので、
 * ここで離して数を合わせる（そうしないと、次に押したときに解除になり、もう一度押すまで押しっぱなしにできない）。
 * 制約: ZMK v0.3.0 は接続先を切り替えるとき、マウスのボタンの「離した」を切り替え前の PC に送らない
 * （キーボードの状態は送るが、マウスは送らない。切り替わったと知らされた時点で、送り先はもう切り替え後）。
 * そのため、切り替え前の PC ではボタンが押されたままに見えることがある（ZMK 標準の &mkp で押したままにした
 * 場合も同じ）。その PC に戻ってクリックすれば直る。
 */

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---- 依頼を入力イベントの列に並べるための入力デバイス（zmk,mouse-button-lock-input）---- */

#define DT_DRV_COMPAT zmk_mouse_button_lock_input

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
             "mouse_button_toggle.dtsi の mouse_lock_input がちょうど1つ必要");

#define LOCK_INPUT_DEV DEVICE_DT_GET(DT_NODELABEL(mouse_lock_input))

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      NULL);

#undef DT_DRV_COMPAT

/* 依頼の種類（INPUT_EV_MSC の code。ZMK の入力リスナーは INPUT_EV_MSC を使わない）。
 * value の下位 8 ビットはボタン（REQUEST_TOGGLE のとき）、その上は接続先の世代 */
#define REQUEST_TOGGLE 0     /* ボタンを、押しっぱなしにする／離す */
#define REQUEST_UNLOCK_ALL 1 /* この世代より前の押しっぱなしを、すべて離す */
#define REQUEST_BUTTONS_MASK 0xFF
#define REQUEST_GEN_SHIFT 8
/* value に入る世代の範囲（これを超えると 0 から数え直す。比べるときも同じ範囲にそろえる） */
#define REQUEST_GEN_MASK ((uint32_t)INT32_MAX >> REQUEST_GEN_SHIFT)

#define MKP_NODE DT_NODELABEL(mkp)
#define NUM_BUTTONS ZMK_HID_MOUSE_NUM_BUTTONS

/* 接続先が切り替わった回数（世代）。切り替わったと知らされた時点で増やす */
static atomic_t endpoint_gen;

/* 今の接続先の世代（value に入る範囲にそろえたもの） */
static uint32_t current_gen(void) { return (uint32_t)atomic_get(&endpoint_gen) & REQUEST_GEN_MASK; }

static void send_request(uint16_t request, uint32_t buttons, uint32_t gen) {
    const int32_t value =
        (int32_t)(((gen & REQUEST_GEN_MASK) << REQUEST_GEN_SHIFT) | (buttons & REQUEST_BUTTONS_MASK));
    input_report(LOCK_INPUT_DEV, INPUT_EV_MSC, request, value, true, K_FOREVER);
}

/* ---- 押しっぱなしの状態（入力スレッドでだけ触る）---- */

/* bit i: ボタン i を押しっぱなしにしている */
static uint32_t locked;
/* ボタンごとに、押しっぱなしにしたときの世代 */
static uint32_t locked_gen[NUM_BUTTONS];

static void set_locked(int button, bool on) {
    WRITE_BIT(locked, button, on);
    if (on) {
        zmk_hid_mouse_button_press(button);
    } else {
        zmk_hid_mouse_button_release(button);
    }
}

/* 依頼を受け取る（入力スレッド。クリックと同じ列から、操作した順に届く） */
static void lock_request_callback(struct input_event *evt) {
    if (evt->type != INPUT_EV_MSC) {
        return;
    }
    const uint32_t buttons = (uint32_t)evt->value & REQUEST_BUTTONS_MASK;
    const uint32_t gen = (uint32_t)evt->value >> REQUEST_GEN_SHIFT;
    if (evt->code == REQUEST_TOGGLE && gen != current_gen()) {
        // 接続先が切り替わる前に押された「切り替えて」: 切り替え先の PC には送らない
        return;
    }
    bool changed = false;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (evt->code == REQUEST_TOGGLE && (buttons & BIT(i))) {
            // 接続先が切り替わる前の押しっぱなしが残っていれば（「全部離して」がまだ届いていない）、先に離す。
            // このキーは、新しい接続先で押しっぱなしにする操作になる
            if ((locked & BIT(i)) && locked_gen[i] != gen) {
                set_locked(i, false);
            }
            set_locked(i, !(locked & BIT(i)));
            locked_gen[i] = gen;
            changed = true;
        } else if (evt->code == REQUEST_UNLOCK_ALL && (locked & BIT(i)) && locked_gen[i] != gen) {
            LOG_DBG("Mouse button %d unlocked (endpoint change)", i);
            set_locked(i, false);
            changed = true;
        }
    }
    if (changed) {
        zmk_endpoints_send_mouse_report();
    }
}

INPUT_CALLBACK_DEFINE(LOCK_INPUT_DEV, lock_request_callback);

/* &mkp のクリック（入力スレッド）: 押しっぱなし中の同じボタンなら、押しっぱなしを解除する */
static void mkp_input_callback(struct input_event *evt) {
    if (evt->type != INPUT_EV_KEY || evt->value == 0 || evt->code < INPUT_BTN_0 ||
        evt->code >= INPUT_BTN_0 + NUM_BUTTONS) {
        return;
    }
    const int button = evt->code - INPUT_BTN_0;
    if (locked & BIT(button)) {
        LOG_DBG("Mouse button %d unlocked (click)", button);
        set_locked(button, false);
        // 送るのは ZMK の入力リスナーに任せる（このクリックの押下と合わせて送られる）
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(MKP_NODE), mkp_input_callback);

/* ---- 接続先の切り替え ---- */

static void unlock_all_work_handler(struct k_work *work) {
    send_request(REQUEST_UNLOCK_ALL, 0, current_gen());
}

static K_WORK_DEFINE(unlock_all_work, unlock_all_work_handler);

/* 世代はすぐに進め（この後に押された「切り替えて」は新しい世代になる）、依頼はワークから出す
 * （別のスレッドから呼ばれることがあるため） */
static int mbt_endpoint_listener(const zmk_event_t *eh) {
    atomic_inc(&endpoint_gen);
    k_work_submit(&unlock_all_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_mouse_button_toggle, mbt_endpoint_listener);
ZMK_SUBSCRIPTION(behavior_mouse_button_toggle, zmk_endpoint_changed);

/* ---- behavior（zmk,behavior-mouse-button-toggle）---- */

#define DT_DRV_COMPAT zmk_behavior_mouse_button_toggle

struct behavior_mbt_config {
    uint32_t buttons;
};

static int on_mbt_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_mbt_config *cfg = dev->config;
    // 切り替えは入力スレッドで行う（クリックと同じ列に並べて、操作した順に扱う）
    send_request(REQUEST_TOGGLE, cfg->buttons & BIT_MASK(NUM_BUTTONS), current_gen());
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
