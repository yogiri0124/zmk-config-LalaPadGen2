/*
 * 漢字入力 behavior (&kanji_key)。部分漢直（よく使う漢字だけを直接打つ）用。
 *
 * `&kanji_key KJ_…` のように漢字の番号（1〜KJ_MAX_ID）を受け取り、その番号の「符号」をローマ字と同じく
 * キーの打鍵として送る。符号を漢字に変えるのは PC 側の IME のローマ字テーブル（Google 日本語入力）で、
 * キーボードは漢字を送らない（どの番号がどの漢字かは docs/google-ime/kanji.tsv に書き、
 * docs/tools/gen_kanji.py でローマ字テーブルと dt-bindings/kana/kanji.h を作る。kanji.h の漢字は、
 * ZMK Studio の一覧に名前として出すためだけに使う）。
 * そのため、番号に対応する漢字を変えただけなら、ファームウェアの書き換えは要らない（ローマ字テーブルを PC に
 * 読み込み直すだけ）。名前（KJ_…）の番号を変えたときは、ビルドしたときの番号の符号を送るので、書き換えが要る。
 *
 * 符号: 「;」＋英字 2 文字（番号 n なら 2 文字目は 'a' + n / 26、3 文字目は 'a' + n % 26）。
 * 「;」は英語配列でも日本語配列でも同じキーコード（SEMICOLON）で同じ文字になるので、どちらの PC でも同じ符号になる。
 *
 * &kana_key と同じ送り方をする（同じ送信キューに並ぶので、直前のかなを追い越さない。かなと漢字が一続きの
 * 確定前の文字になる。送る間は Shift・Ctrl を隠す。しばらく打っていなかったら IME オンを先に送る）。
 * かな配列モードでないときと、解除処理中は送らずに捨てる（IME オフのときに「;ab」が入らないように）。
 */

#define DT_DRV_COMPAT zmk_behavior_kanji

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <dt-bindings/kana/kanji.h>
#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 符号の 2・3 文字目に使う英字（26 文字なので、番号は 26 × 26 - 1 まで） */
static const uint32_t kanji_letters[] = {A, B, C, D, E, F, G, H, I, J, K, L, M,
                                         N, O, P, Q, R, S, T, U, V, W, X, Y, Z};

BUILD_ASSERT(ARRAY_SIZE(kanji_letters) == 26, "kanji code letters must be a-z");
BUILD_ASSERT(KJ_MAX_ID == 26 * 26 - 1, "KJ_MAX_ID must match the 2-letter code space");

// behavior のノードがないとき（入力処理など、同じモジュールのほかの部分だけを使うとき）に未使用の警告を出さない
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_kanji_config {
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t ime_resync_ms;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/* ZMK Studio の一覧。表に漢字があれば、表の漢字を名前に出して選べるようにする（kanji.h の KANJI_LIST。
 * 出す文字はビルドしたときの表のもの。実際に入る漢字は PC 側のローマ字テーブルで決まる）。
 * 表が空のときは、番号の範囲で割り当てられるようにする */
#if KJ_COUNT > 0
#define KANJI_METADATA_ENTRY(id, name)                                                             \
    {.display_name = name, .value = id, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},

static const struct behavior_parameter_value_metadata param_values[] = {
    KANJI_LIST(KANJI_METADATA_ENTRY)};
#else
static const struct behavior_parameter_value_metadata param_values[] = {{
    .display_name = "漢字の番号",
    .range = {.min = 1, .max = KJ_MAX_ID},
    .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
}};
#endif

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int on_kanji_binding_pressed(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kanji_config *cfg = dev->config;
    const uint32_t id = binding->param1;

    if (id == 0 || id > KJ_MAX_ID) {
        LOG_WRN("Unknown kanji id %u", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (!kana_input_enabled()) {
        // かな配列モードでない、または解除処理中（IME オフのときに符号がそのまま入らないよう捨てる）
        LOG_DBG("Kana input disabled, dropped kanji id %u", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const uint32_t keys[] = {SEMICOLON, kanji_letters[id / 26], kanji_letters[id % 26]};

    // かなと同じく、しばらく打っていなかったら IME オンを先に送る（ime-resync-ms が 0 なら送らない）。
    // 登録できなかったときは、順番を守るため残りを送らない
    const int ret = kana_output_send_keys(&event, keys, ARRAY_SIZE(keys), cfg->tap_ms,
                                          cfg->wait_ms, cfg->ime_resync_ms > 0,
                                          cfg->ime_resync_ms);
    if (ret < 0) {
        // -EBUSY / -ENOSPC: その打鍵から先は登録していない。-EAGAIN: 「離す」は自動で再試行される
        LOG_WRN("Behavior queue busy/full (err %d), dropped (rest of) kanji id %u", ret, id);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kanji_binding_released(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kanji_driver_api = {
    .binding_pressed = on_kanji_binding_pressed,
    .binding_released = on_kanji_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KANJI_INST(n)                                                                              \
    KANA_CHECK_MS(n, tap_ms, 10000)                                                                \
    KANA_CHECK_MS(n, wait_ms, 10000)                                                               \
    KANA_CHECK_MS(n, ime_resync_ms, 60000)                                                         \
    static const struct behavior_kanji_config behavior_kanji_config_##n = {                       \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .ime_resync_ms = DT_INST_PROP(n, ime_resync_ms),                                           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kanji_config_##n, POST_KERNEL,          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kanji_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANJI_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
