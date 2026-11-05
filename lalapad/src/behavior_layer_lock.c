/*
 * レイヤーを固定する behavior（zmk,behavior-layer-lock、引数なし。QMK の Layer Lock）。
 *
 * 押すと、このキーを置いたレイヤー（押したときに、このキーの割り当てが見つかったレイヤー）を固定する。
 * 固定している間は、ほかの操作でそのレイヤーが解除されても、すぐに有効にし直す（&mo のキーを離しても、
 * オートマウスレイヤーの時間切れでも切れない）。もう一度押すと固定をやめて、そのレイヤーを解除する
 * （オートマウスレイヤーが有効にしているレイヤーなら解除はせず、ふだんどおり時間を数え直す）。
 * 一番下のレイヤー（Default）と、conditional_layers が有効・解除するレイヤー（then-layer。数字＋記号でなる
 * システムレイヤーなど）は固定しない（条件で解除されるたびに有効にし直すと、条件の評価と取り合いになるため）。
 *
 * 状態はシステムワークキューで書き換える（behavior のコールバックと、レイヤーの出来事の listener）。
 * オートマウスレイヤーの入力処理（入力スレッド）からも読むので、atomic で持つ。
 */

#define DT_DRV_COMPAT zmk_behavior_layer_lock

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

#include "layer_lock.h"
#if IS_ENABLED(CONFIG_ZMK_AUTO_MOUSE_LAYER)
#include "auto_mouse_layer.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= 32, "layer lock: up to 32 layers");

/* conditional_layers の then-layer（固定しないレイヤー）。keymap の宣言順の番号（起動時はレイヤー ID と同じ） */
#define LAYER_LOCK_CL_THEN(child) | BIT(DT_PROP(child, then_layer))
#define LAYER_LOCK_CL_NODE(node) DT_FOREACH_CHILD(node, LAYER_LOCK_CL_THEN)
static const uint32_t conditional_layers =
    0 DT_FOREACH_STATUS_OKAY(zmk_conditional_layers, LAYER_LOCK_CL_NODE);

/* 固定しているレイヤー（レイヤー ID のビット） */
static atomic_t locked;

bool layer_lock_is_locked(zmk_keymap_layer_id_t layer) {
    return layer < 32 && (atomic_get(&locked) & BIT(layer)) != 0;
}

void layer_lock_forget(zmk_keymap_layer_id_t layer) {
    if (layer < 32) {
        atomic_and(&locked, ~BIT(layer));
    }
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_layer_lock_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    zmk_keymap_layer_id_t layer = event.layer;
    if (event.position >= ZMK_KEYMAP_LEN && layer == zmk_keymap_layer_default()) {
        // combos から押した: ZMK v0.3.0 はレイヤーを Default として渡す。combos は layers で働くレイヤーを
        // 決めるので、いちばん上の有効なレイヤー（その同時押しが働いたレイヤー）を固定する
        layer = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
    }
    if (layer >= 32 || layer == zmk_keymap_layer_default() || (conditional_layers & BIT(layer))) {
        LOG_DBG("Layer %d cannot be locked", layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    if (layer_lock_is_locked(layer)) {
        // 先に印を外してから解除する（下の listener が有効にし直さないように）
        atomic_and(&locked, ~BIT(layer));
        LOG_DBG("Layer %d unlocked", layer);
#if IS_ENABLED(CONFIG_ZMK_AUTO_MOUSE_LAYER)
        if (auto_mouse_layer_unlocked(layer)) {
            return ZMK_BEHAVIOR_OPAQUE;
        }
#endif
        zmk_keymap_layer_deactivate(layer);
    } else {
        atomic_or(&locked, BIT(layer));
        zmk_keymap_layer_activate(layer);
        LOG_DBG("Layer %d locked", layer);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_layer_lock_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_layer_lock_driver_api = {
    .binding_pressed = on_layer_lock_binding_pressed,
    .binding_released = on_layer_lock_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

/* 固定しているレイヤーが解除されたら、すぐに有効にし直す（&mo のキーを離したときなど） */
static int layer_lock_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev != NULL && !ev->state && layer_lock_is_locked(ev->layer) &&
        !zmk_keymap_layer_active(ev->layer)) {
        zmk_keymap_layer_activate(ev->layer);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_layer_lock, layer_lock_listener);
ZMK_SUBSCRIPTION(behavior_layer_lock, zmk_layer_state_changed);

#define LAYER_LOCK_INST(n)                                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_layer_lock_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LAYER_LOCK_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
