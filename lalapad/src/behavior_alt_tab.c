/*
 * ウィンドウの切り替え behavior（zmk,behavior-alt-tab、引数なし）。
 * keymap で swap_next（Alt+Tab）・swap_prev（reverse を付けたもの。Alt+Shift+Tab）のインスタンスを作って使う。
 *
 * 最初に押したときに Alt を押したままにして、Tab（reverse なら Shift+Tab）を送る。続けて押すたびに
 * Tab・Shift+Tab を送るので、Windows のウィンドウの切り替えの画面で、選ぶ先を進めたり戻したりできる。
 * 次のどれかで Alt を離す（選んでいるウィンドウに切り替わる）:
 * - 押し始めたときのレイヤーが解除された（&mo のレイヤーに置いて、そのレイヤーのキーを離したとき）
 * - ほかのキーの入力（キーコードの「押す」）が届いた（そのキーより先に Alt を離すので、そのキーは Alt なしで届く）
 * - 接続先（USB・BLE プロファイル）が変わった
 * zmk-tri-state（urob）の swapper と同じ考え方で、順方向と逆方向のキーで Alt を共有する
 * （どちらから押し始めても、途中で入れ替えても、Alt は押したまま。離すのも1回だけ）。
 *
 * 順方向と逆方向のキーを重ねて押したとき（4 を離す前に 3 を押した、など）は、押しているほうの Tab を先に離してから
 * 次を押す（同じ Tab を離さずに押し重ねると、PC には2回目の押下として届かないため）。
 *
 * 状態はシステムワークキューで触る（behavior のコールバック、listener、接続先の切り替えのワーク）。
 */

#define DT_DRV_COMPAT zmk_behavior_alt_tab

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/keys.h>

#include "alt_code.h"
#include "layer_lock.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_alt_tab_config {
    /* Shift+Tab を送る（逆方向） */
    bool reverse;
};

/* Alt を押したままにしている */
static bool active;
/* 押し始めたときのレイヤー（このレイヤーが解除されたら Alt を離す） */
static int active_layer;
/* 今押している Tab（TAB または LS(TAB)。押していなければ 0） */
static uint32_t held_tab;
/* held_tab を押したキーの位置（離したキーが、押したままのキーかを見分ける） */
static uint32_t held_tab_position;
/* 自分の送っているキー入力（ほかのキーの入力と見分ける） */
static bool raising;

static void raise_key(uint32_t keycode, bool pressed) {
    raising = true;
    raise_zmk_keycode_state_changed_from_encoded(keycode, pressed, k_uptime_get());
    raising = false;
}

/* Alt を離して、切り替えを終える（押したままの Tab も先に離す。ほかのキーで終えたとき、
 * そのキーが Tab と一緒に届かないように） */
static void end_switch(void) {
    if (!active) {
        return;
    }
    active = false;
    if (held_tab != 0) {
        raise_key(held_tab, false);
        held_tab = 0;
    }
    raise_key(LALT, false);
}

static uint32_t tab_keycode(const struct zmk_behavior_binding *binding) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_alt_tab_config *cfg = dev->config;
    return cfg->reverse ? LS(TAB) : TAB;
}

static int on_alt_tab_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    if (!active) {
        active = true;
        active_layer = event.layer;
        raise_key(LALT, true);
    }
    // 別のキーの Tab を押したままなら、先に離す（離さずに押し重ねると、PC に押下として届かない）
    if (held_tab != 0) {
        raise_key(held_tab, false);
    }
    held_tab = tab_keycode(binding);
    held_tab_position = event.position;
    raise_key(held_tab, true);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_alt_tab_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    // 後から押した別のキーに Tab を引き継いでいたら、そちらの分は離さない（同じ向きのキーでも、位置で見分ける）
    const uint32_t keycode = tab_keycode(binding);
    if (held_tab == keycode && held_tab_position == event.position) {
        raise_key(keycode, false);
        held_tab = 0;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_alt_tab_driver_api = {
    .binding_pressed = on_alt_tab_binding_pressed,
    .binding_released = on_alt_tab_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

/* 接続先が変わった: Alt を離す（ZMK は切り替えで PC へ送る状態を消すが、修飾キーの押した数は残るので、
 * 離して数を戻す）。通知は別のスレッドから届くことがあるので、ワークで行う */
static void end_work_handler(struct k_work *work) { end_switch(); }

static K_WORK_DEFINE(end_work, end_work_handler);

static int alt_tab_listener(const zmk_event_t *eh) {
    if (!active) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        // ほかのキーの「押す」: そのキーより先に Alt を離す（この listener は ZMK 本体の HID への反映より前に呼ばれる）
        // 次のものでは終えない（送っている間に始めた切り替えが、すぐ確定しないように）:
        // - Alt コードの送信が自分で送るキー（テンキーの数字・IME オン など）
        // - Alt・Tab の「押す」: Alt コードの送信中に押すと、この behavior が送った Alt・Tab は Alt コードに預けられ、
        //   送り終えてから（raising でないときに）この listener に届くため。Alt・Tab のキーを自分で押したときも、
        //   切り替えは終わらない（Alt+Tab として、切り替えの画面で選ぶ先が進む）
        const bool own_keys = kc->usage_page == HID_USAGE_KEY &&
                              (kc->keycode == HID_USAGE_KEY_KEYBOARD_TAB ||
                               kc->keycode == HID_USAGE_KEY_KEYBOARD_LEFTALT);
        if (kc->state && !raising && !own_keys && !alt_code_is_own_event(kc)) {
            end_switch();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_layer_state_changed *ly = as_zmk_layer_state_changed(eh);
    if (ly != NULL) {
        // 解除の出来事でも、まだ有効なら終えない（固定したレイヤーは、&to などで解除されても固定の listener が
        // 有効にし直す。この listener はそれより先に呼ばれることがあるので、固定しているかも見る）
        if (!ly->state && ly->layer == active_layer && !zmk_keymap_layer_active(active_layer) &&
            !layer_lock_is_locked(active_layer)) {
            end_switch();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (as_zmk_endpoint_changed(eh) != NULL) {
        k_work_submit(&end_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_alt_tab, alt_tab_listener);
ZMK_SUBSCRIPTION(behavior_alt_tab, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(behavior_alt_tab, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(behavior_alt_tab, zmk_endpoint_changed);

#define ALT_TAB_INST(n)                                                                            \
    static const struct behavior_alt_tab_config behavior_alt_tab_config_##n = {                   \
        .reverse = DT_INST_PROP(n, reverse),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_alt_tab_config_##n, POST_KERNEL,        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_alt_tab_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ALT_TAB_INST)
