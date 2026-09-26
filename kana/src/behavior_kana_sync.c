/*
 * かな入力モジュール内部用の behavior（keymap には書かない）。
 * kana_output.c が behavior queue の末尾に「合図」として並べ、キューがここまで処理されたこと
 * （= それより前に並べた送信がすべて実行されたこと）を kana_output.c に知らせる。
 */

#define DT_DRV_COMPAT zmk_behavior_kana_sync

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "kana_output.h"

static int on_kana_sync_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    kana_output_on_sync(binding->param1);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_sync_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kana_sync_driver_api = {
    .binding_pressed = on_kana_sync_binding_pressed,
    .binding_released = on_kana_sync_binding_released,
};

#define KANA_SYNC_INST(n)                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kana_sync_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANA_SYNC_INST)
