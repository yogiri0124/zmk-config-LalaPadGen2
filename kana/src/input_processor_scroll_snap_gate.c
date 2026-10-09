/*
 * スクロールスナップの入り口の入力処理 (zmk,input-processor-scroll-snap-gate)。
 *
 * zmk,behavior-scroll-snap-hold（keymap の scr_snap_hold）のキーを押している間だけ、
 * processor プロパティで指定した入力処理（zmk,input-processor-scroll-snap。2本指スクロールを
 * 縦か横のどちらかにそろえる）にイベントを渡す。押していない間は何もせず、そのまま通す。
 * レイヤーを使わずにスナップのオン・オフを切り替えるためのもの。
 */

#define DT_DRV_COMPAT zmk_input_processor_scroll_snap_gate

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>

#include "scroll_snap_gate.h"

/* 初期化の優先度。渡し先の入力処理（processor プロパティ）より後にする必要がある
 * （Zephyr のビルド時チェック）。初期化する状態は持たない */
#define SCROLL_SNAP_GATE_INIT_PRIORITY 99

struct scroll_snap_gate_config {
    const struct device *processor;
};

static int scroll_snap_gate_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    if (!scroll_snap_gate_is_active()) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct scroll_snap_gate_config *cfg = dev->config;
    return zmk_input_processor_handle_event(cfg->processor, event, 0, 0, state);
}

static struct zmk_input_processor_driver_api scroll_snap_gate_driver_api = {
    .handle_event = scroll_snap_gate_handle_event,
};

#define SCROLL_SNAP_GATE_INST(n)                                                                   \
    static const struct scroll_snap_gate_config scroll_snap_gate_config_##n = {                    \
        .processor = DEVICE_DT_GET(DT_INST_PHANDLE(n, processor)),                                 \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &scroll_snap_gate_config_##n, POST_KERNEL,          \
                          SCROLL_SNAP_GATE_INIT_PRIORITY, &scroll_snap_gate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_SNAP_GATE_INST)
