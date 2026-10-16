/*
 * スクロールスナップを押している間だけオンにする behavior（zmk,behavior-scroll-snap-hold、引数なし）。
 *
 * レイヤーは使わず、オン・オフの状態をここで持つ（全インスタンスで共有）。実際にスクロールを
 * 縦か横のどちらかにそろえるのは、トラックパッドのリスナーに入れた
 * zmk,input-processor-scroll-snap-gate（input_processor_scroll_snap_gate.c）と、その先の
 * zmk,input-processor-scroll-snap（kot149/zmk-scroll-snap）。
 *
 * 状態は入力スレッド（入力処理）からも読むので atomic で持つ。
 */

#define DT_DRV_COMPAT zmk_behavior_scroll_snap_hold

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "scroll_snap_gate.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 押されている数（複数のキーに置いても正しく数える） */
static atomic_t hold_count;

bool scroll_snap_gate_is_active(void) { return atomic_get(&hold_count) > 0; }

static int on_scroll_snap_hold_binding_pressed(struct zmk_behavior_binding *binding,
                                               struct zmk_behavior_binding_event event) {
    atomic_inc(&hold_count);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_scroll_snap_hold_binding_released(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event event) {
    // 押した分だけ戻す（0 より小さくはしない）
    atomic_val_t count = atomic_get(&hold_count);
    while (count > 0 && !atomic_cas(&hold_count, count, count - 1)) {
        count = atomic_get(&hold_count);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_scroll_snap_hold_driver_api = {
    .binding_pressed = on_scroll_snap_hold_binding_pressed,
    .binding_released = on_scroll_snap_hold_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define SCROLL_SNAP_HOLD_INST(n)                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_scroll_snap_hold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_SNAP_HOLD_INST)
