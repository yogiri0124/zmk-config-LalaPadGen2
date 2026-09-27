/*
 * カーソル低速の入力処理 (zmk,input-processor-cursor-slow)。
 *
 * zmk,behavior-cursor-slow（keymap の cur_slow_*）で低速がオンの間だけ、カーソル移動 (REL_X / REL_Y) を param1 / param2 倍にする
 * （例: <&zip_cursor_slow 1 3> で 1/3）。スクロールなど、ほかのイベントはそのまま通す。
 * 端数は ZMK の scaler と同じく state->remainder に持ち越す（track-remainders）。
 */

#define DT_DRV_COMPAT zmk_input_processor_cursor_slow

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>

#include "cursor_slow.h"

static int cursor_slow_handle_event(const struct device *dev, struct input_event *event,
                                    uint32_t param1, uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!cursor_slow_is_active() || param2 == 0) {
        // 低速でないときは何もしない（前回の端数は捨てる）
        if (state && state->remainder) {
            *state->remainder = 0;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t value = (int32_t)event->value * (int32_t)param1;
    if (state && state->remainder) {
        value += *state->remainder;
    }
    const int32_t scaled = value / (int32_t)param2;
    if (state && state->remainder) {
        *state->remainder = (int16_t)(value - scaled * (int32_t)param2);
    }
    event->value = scaled;

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api cursor_slow_driver_api = {
    .handle_event = cursor_slow_handle_event,
};

#define CURSOR_SLOW_IP_INST(n)                                                                     \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cursor_slow_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CURSOR_SLOW_IP_INST)
