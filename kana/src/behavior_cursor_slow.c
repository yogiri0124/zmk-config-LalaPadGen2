/*
 * カーソル低速のオン・オフ behavior（zmk,behavior-cursor-slow、引数なし。動作は mode プロパティで指定）。
 *
 * keymap で mode = <CURSOR_SLOW_HOLD> などを付けたインスタンスを作って使う。
 * レイヤーは使わず、低速の状態をここで持つ（全インスタンスで共有）。実際にカーソルを遅くするのは
 * input_processor_cursor_slow.c（トラックパッドのリスナーに入れた &zip_cursor_slow）。
 *   HOLD    : 押している間だけ低速
 *   TOGGLE  : 押すたびに低速のオン・オフ
 *   ONESHOT : 次のキー（ほかのキー位置の押下。トラックパッドのクリック・ジェスチャーも含む）まで低速
 *
 * 状態は入力スレッド（入力処理）からも読むので atomic で持つ。
 */

#define DT_DRV_COMPAT zmk_behavior_cursor_slow

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include <dt-bindings/lalapad/cursor_slow.h>

#include "cursor_slow.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* HOLD で押されている数（複数のキーに置いても正しく数える） */
static atomic_t hold_count;
/* TOGGLE で低速オン */
static atomic_t toggled;
/* ONESHOT で低速オン（oneshot_position 以外のキーが押されたら解除） */
static atomic_t oneshot;
static atomic_t oneshot_position;

bool cursor_slow_is_active(void) {
    return atomic_get(&hold_count) > 0 || atomic_get(&toggled) != 0 || atomic_get(&oneshot) != 0;
}

struct behavior_cursor_slow_config {
    uint8_t mode;
};

static uint8_t binding_mode(const struct zmk_behavior_binding *binding) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cursor_slow_config *cfg = dev->config;
    return cfg->mode;
}

static int on_cursor_slow_binding_pressed(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    const uint8_t mode = binding_mode(binding);
    switch (mode) {
    case CURSOR_SLOW_HOLD:
        atomic_inc(&hold_count);
        break;
    case CURSOR_SLOW_TOGGLE:
        atomic_set(&toggled, atomic_get(&toggled) ? 0 : 1);
        break;
    case CURSOR_SLOW_ONESHOT:
        atomic_set(&oneshot_position, event.position);
        atomic_set(&oneshot, 1);
        break;
    default:
        LOG_WRN("Unknown cursor slow mode %d", mode);
        break;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_cursor_slow_binding_released(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    if (binding_mode(binding) == CURSOR_SLOW_HOLD && atomic_get(&hold_count) > 0) {
        atomic_dec(&hold_count);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

/* ONESHOT: ほかのキー位置が押されたら低速を解除する */
static int cursor_slow_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev != NULL && ev->state && atomic_get(&oneshot) &&
        ev->position != (uint32_t)atomic_get(&oneshot_position)) {
        atomic_set(&oneshot, 0);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_cursor_slow, cursor_slow_position_listener);
ZMK_SUBSCRIPTION(behavior_cursor_slow, zmk_position_state_changed);

static const struct behavior_driver_api behavior_cursor_slow_driver_api = {
    .binding_pressed = on_cursor_slow_binding_pressed,
    .binding_released = on_cursor_slow_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define CURSOR_SLOW_INST(n)                                                                        \
    static const struct behavior_cursor_slow_config behavior_cursor_slow_config_##n = {            \
        .mode = DT_INST_PROP(n, mode),                                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_cursor_slow_config_##n, POST_KERNEL,    \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_cursor_slow_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CURSOR_SLOW_INST)
