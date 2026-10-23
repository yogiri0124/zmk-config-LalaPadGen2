/*
 * 決まった打鍵の列を送る behavior（zmk,behavior-kana-seq、引数なし）。
 *
 * 送る打鍵は keymap 側の keys に書く（例: keys = <Z EQUAL>;）。記号やカッコのキーを足すたびに
 * このモジュールのプログラムを書き換えなくて済むようにするためのもの。
 *
 * ZMK 標準のマクロ（zmk,behavior-macro）との違いは、&kana_key と同じ送り方をすること:
 * - かな配列モードの解除処理中に押されたら、送らずに捨てる（IME オフの後に打鍵が出ないように）。
 * - 送信キューが満杯のときや「離す」の再試行中は、残りの打鍵を送らずに打ち切る（順番を守る）。
 *   「押す」だけ登録できたキーは、必ず離す（kana_output.c の再試行）。
 * - かなと同じ送信キューに並ぶので、直前に打ったかなを追い越さない。
 *
 * かな配列モードでないときも使える（かなのレイヤーと、ほかのレイヤーに、同じキーを置ける）。
 * ime-on を付けたキーは、打鍵の前に IME オン（LANGUAGE_1）を送る。
 * かな配列モード中は、ime-on がなくても、しばらく文字を打っていなければ IME オンを先に送る。
 */

#define DT_DRV_COMPAT zmk_behavior_kana_seq

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_kana_seq_config {
    const uint32_t *keys;
    size_t keys_len;
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t ime_resync_ms;
    bool ime_on;
};

static int on_kana_seq_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kana_seq_config *cfg = dev->config;

    if (kana_mode_exiting()) {
        // かな配列モードの解除処理中（IME オフの後に打鍵が出ないよう捨てる）
        LOG_DBG("Kana mode exiting, dropped key sequence");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int ret = 0;
    if (kana_input_enabled()) {
        // かな配列モード中: かなと同じく、しばらく打っていなかったときだけ IME オンを先に送る
        if (cfg->ime_resync_ms > 0) {
            ret = kana_output_ime_on(&event, cfg->tap_ms, cfg->wait_ms, cfg->ime_resync_ms);
        }
    } else if (cfg->ime_on) {
        // それ以外: IME の状態がわからないので、毎回 IME オンを先に送る
        ret = kana_output_ime_on(&event, cfg->tap_ms, cfg->wait_ms, 0);
    }
    if (ret < 0) {
        // IME オンを登録できなかった、または「離す」を再試行中: 順番を守るため、打鍵は送らない
        LOG_WRN("Could not queue IME on (err %d), dropped key sequence", ret);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    for (size_t i = 0; i < cfg->keys_len; i++) {
        ret = kana_output_tap(&event, cfg->keys[i], cfg->tap_ms, cfg->wait_ms);
        if (ret < 0) {
            // -EBUSY / -ENOSPC: 何も登録していない。-EAGAIN: 「離す」は自動で再試行される。
            // いずれも残りは送らない（順番が崩れないように）
            LOG_WRN("Behavior queue busy/full (err %d), dropped rest of key sequence", ret);
            break;
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_seq_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kana_seq_driver_api = {
    .binding_pressed = on_kana_seq_binding_pressed,
    .binding_released = on_kana_seq_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KANA_SEQ_INST(n)                                                                           \
    KANA_CHECK_MS(n, tap_ms, 10000)                                                                \
    KANA_CHECK_MS(n, wait_ms, 10000)                                                               \
    KANA_CHECK_MS(n, ime_resync_ms, 60000)                                                         \
    static const uint32_t behavior_kana_seq_keys_##n[] = DT_INST_PROP(n, keys);                    \
    static const struct behavior_kana_seq_config behavior_kana_seq_config_##n = {                 \
        .keys = behavior_kana_seq_keys_##n,                                                        \
        .keys_len = DT_INST_PROP_LEN(n, keys),                                                     \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .ime_resync_ms = DT_INST_PROP(n, ime_resync_ms),                                           \
        .ime_on = DT_INST_PROP(n, ime_on),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kana_seq_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kana_seq_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANA_SEQ_INST)
