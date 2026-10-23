/*
 * かな入力 behavior (&kana_key)。
 *
 * `&kana_key KN_KA` のように文字 ID を受け取り、対応するローマ字のキー列を
 * ZMK の behavior queue（マクロと同じ仕組み）経由で &kp として順に送る。
 * かな漢字変換はホスト側の IME（ローマ字入力モード）が行う。
 * ホストのキーボード配列は英語配列 (US) を前提にしている。
 * 送るのは、かな（KN_）と、編集用のキー（KE_）。
 * 記号（、。… など）は、keymap で定義する zmk,behavior-kana-seq のキー（behavior_kana_seq.c）で送る。
 * スペース・エンター・バックスペース（KE_）も同じキューで送り、かな列を追い越さないようにする
 * （この3つは、押している間は押しっぱなしにして、キーリピートができるようにする）。
 * キューへの登録・押しっぱなし防止・かな配列モードの状態は kana_output.c にまとめている。
 */

#define DT_DRV_COMPAT zmk_behavior_kana

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

#include <dt-bindings/kana/kana.h>
#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* かな1文字の最大打鍵数（ゃ = x y a など） */
#define KANA_MAX_KEYS 3

struct kana_entry {
    uint8_t len;
    uint32_t keys[KANA_MAX_KEYS];
};

#define K1(a) {.len = 1, .keys = {a}}
#define K2(a, b) {.len = 2, .keys = {a, b}}
#define K3(a, b, c) {.len = 3, .keys = {a, b, c}}

/* 文字 ID・表示名（ZMK Studio の一覧に出る名前）・送るキー列（英語配列・ローマ字入力）。
 * 送るキー列の表と ZMK Studio の一覧は、どちらもこの1つの一覧から作る（取り違えや書き漏れが起きないように）。
 * 引数の名前を X にしないこと（キー X を送る行が、引数に置き換わってしまう） */
#define KANA_LIST(ENTRY)                                                                             \
    ENTRY(KN_A, "あ", K1(A)) \
    ENTRY(KN_I, "い", K1(I)) \
    ENTRY(KN_U, "う", K1(U)) \
    ENTRY(KN_E, "え", K1(E)) \
    ENTRY(KN_O, "お", K1(O)) \
    ENTRY(KN_KA, "か", K2(K, A)) \
    ENTRY(KN_KI, "き", K2(K, I)) \
    ENTRY(KN_KU, "く", K2(K, U)) \
    ENTRY(KN_KE, "け", K2(K, E)) \
    ENTRY(KN_KO, "こ", K2(K, O)) \
    ENTRY(KN_SA, "さ", K2(S, A)) \
    ENTRY(KN_SI, "し", K2(S, I)) \
    ENTRY(KN_SU, "す", K2(S, U)) \
    ENTRY(KN_SE, "せ", K2(S, E)) \
    ENTRY(KN_SO, "そ", K2(S, O)) \
    ENTRY(KN_TA, "た", K2(T, A)) \
    ENTRY(KN_TI, "ち", K2(T, I)) \
    ENTRY(KN_TU, "つ", K2(T, U)) \
    ENTRY(KN_TE, "て", K2(T, E)) \
    ENTRY(KN_TO, "と", K2(T, O)) \
    ENTRY(KN_NA, "な", K2(N, A)) \
    ENTRY(KN_NI, "に", K2(N, I)) \
    ENTRY(KN_NU, "ぬ", K2(N, U)) \
    ENTRY(KN_NE, "ね", K2(N, E)) \
    ENTRY(KN_NO, "の", K2(N, O)) \
    ENTRY(KN_HA, "は", K2(H, A)) \
    ENTRY(KN_HI, "ひ", K2(H, I)) \
    ENTRY(KN_HU, "ふ", K2(H, U)) \
    ENTRY(KN_HE, "へ", K2(H, E)) \
    ENTRY(KN_HO, "ほ", K2(H, O)) \
    ENTRY(KN_MA, "ま", K2(M, A)) \
    ENTRY(KN_MI, "み", K2(M, I)) \
    ENTRY(KN_MU, "む", K2(M, U)) \
    ENTRY(KN_ME, "め", K2(M, E)) \
    ENTRY(KN_MO, "も", K2(M, O)) \
    ENTRY(KN_YA, "や", K2(Y, A)) \
    ENTRY(KN_YU, "ゆ", K2(Y, U)) \
    ENTRY(KN_YO, "よ", K2(Y, O)) \
    ENTRY(KN_RA, "ら", K2(R, A)) \
    ENTRY(KN_RI, "り", K2(R, I)) \
    ENTRY(KN_RU, "る", K2(R, U)) \
    ENTRY(KN_RE, "れ", K2(R, E)) \
    ENTRY(KN_RO, "ろ", K2(R, O)) \
    ENTRY(KN_WA, "わ", K2(W, A)) \
    ENTRY(KN_WO, "を", K2(W, O)) \
    ENTRY(KN_NN, "ん", K2(N, N)) \
    ENTRY(KN_GA, "が", K2(G, A)) \
    ENTRY(KN_GI, "ぎ", K2(G, I)) \
    ENTRY(KN_GU, "ぐ", K2(G, U)) \
    ENTRY(KN_GE, "げ", K2(G, E)) \
    ENTRY(KN_GO, "ご", K2(G, O)) \
    ENTRY(KN_ZA, "ざ", K2(Z, A)) \
    ENTRY(KN_ZI, "じ", K2(Z, I)) \
    ENTRY(KN_ZU, "ず", K2(Z, U)) \
    ENTRY(KN_ZE, "ぜ", K2(Z, E)) \
    ENTRY(KN_ZO, "ぞ", K2(Z, O)) \
    ENTRY(KN_DA, "だ", K2(D, A)) \
    ENTRY(KN_DI, "ぢ", K2(D, I)) \
    ENTRY(KN_DU, "づ", K2(D, U)) \
    ENTRY(KN_DE, "で", K2(D, E)) \
    ENTRY(KN_DO, "ど", K2(D, O)) \
    ENTRY(KN_BA, "ば", K2(B, A)) \
    ENTRY(KN_BI, "び", K2(B, I)) \
    ENTRY(KN_BU, "ぶ", K2(B, U)) \
    ENTRY(KN_BE, "べ", K2(B, E)) \
    ENTRY(KN_BO, "ぼ", K2(B, O)) \
    ENTRY(KN_PA, "ぱ", K2(P, A)) \
    ENTRY(KN_PI, "ぴ", K2(P, I)) \
    ENTRY(KN_PU, "ぷ", K2(P, U)) \
    ENTRY(KN_PE, "ぺ", K2(P, E)) \
    ENTRY(KN_PO, "ぽ", K2(P, O)) \
    ENTRY(KN_XYA, "ゃ", K3(X, Y, A)) \
    ENTRY(KN_XYU, "ゅ", K3(X, Y, U)) \
    ENTRY(KN_XYO, "ょ", K3(X, Y, O)) \
    ENTRY(KN_XA, "ぁ", K2(X, A)) \
    ENTRY(KN_XI, "ぃ", K2(X, I)) \
    ENTRY(KN_XU, "ぅ", K2(X, U)) \
    ENTRY(KN_XE, "ぇ", K2(X, E)) \
    ENTRY(KN_XO, "ぉ", K2(X, O)) \
    ENTRY(KN_XTU, "っ", K3(X, T, U)) \
    ENTRY(KN_VU, "ゔ", K2(V, U)) \
    ENTRY(KE_SPACE, "スペース", K1(SPACE)) \
    ENTRY(KE_ENTER, "エンター", K1(ENTER)) \
    ENTRY(KE_BSPC, "バックスペース", K1(BACKSPACE))

#define KANA_TABLE_ENTRY(id, name, keys) [id] = keys,

static const struct kana_entry kana_table[KN_MAX_ID + 1] = {KANA_LIST(KANA_TABLE_ENTRY)};

struct behavior_kana_config {
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t ime_resync_ms;
};

/* スペース・エンター・バックスペースは押しっぱなし（キーリピート）ができるよう、押したとき「押す」、
 * 離したとき「離す」を送る。キー位置ごとに押しているキーコードを覚えておく（0 = 押していない） */
static uint32_t held_keycode[ZMK_KEYMAP_LEN];

static bool is_hold_key(uint32_t id) { return id == KE_SPACE || id == KE_ENTER || id == KE_BSPC; }

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

#define KANA_METADATA_ENTRY(id, name, keys)                                                        \
    {.display_name = name, .value = id, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},

static const struct behavior_parameter_value_metadata param_values[] = {
    KANA_LIST(KANA_METADATA_ENTRY)};

/* かな（1〜KN_VU）と、編集用のキー3つ。その間の番号は欠番（kana.h に ID を足したら KANA_LIST にも足す） */
BUILD_ASSERT(ARRAY_SIZE(param_values) == KN_VU + 3, "KANA_LIST must cover every kana ID");

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int on_kana_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kana_config *cfg = dev->config;
    const uint32_t id = binding->param1;

    if (id == 0 || id > KN_MAX_ID || kana_table[id].len == 0) {
        LOG_WRN("Unknown kana id %d", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (!kana_input_enabled()) {
        // かな配列モードでない、または解除処理中（IME オフの後にローマ字が出ないよう捨てる）
        LOG_DBG("Kana input disabled, dropped kana id %d", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (is_hold_key(id)) {
        if (event.position < ZMK_KEYMAP_LEN && held_keycode[event.position] == 0) {
            const uint32_t keycode = kana_table[id].keys[0];
            const int ret = kana_output_press(&event, keycode, cfg->tap_ms);
            if (ret == 0) {
                held_keycode[event.position] = keycode;
            } else {
                LOG_WRN("Behavior queue busy/full (err %d), dropped kana id %d", ret, id);
            }
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // かなの前だけ、しばらく打っていなかったら IME オンを先に送る（LANGUAGE_1 はトグルではないので重ねても害がない）。
    // スペース・エンター・バックスペースの前には送らない（IME オフのときに全角スペースなどにならないように）
    if (cfg->ime_resync_ms > 0) {
        const int ret = kana_output_ime_on(&event, cfg->tap_ms, cfg->wait_ms, cfg->ime_resync_ms);
        if (ret < 0) {
            // -EBUSY / -ENOSPC: IME オンを登録できなかった。次の入力で改めて IME オンを試す。
            // -EAGAIN: IME オンの「押す」は登録済み（「離す」は自動で再試行される）。
            // いずれも順番を守るため、この文字は送らない
            LOG_WRN("Could not queue IME on (err %d), dropped kana id %d", ret, id);
            return ZMK_BEHAVIOR_OPAQUE;
        }
    }

    const struct kana_entry *entry = &kana_table[id];
    for (int i = 0; i < entry->len; i++) {
        const int ret = kana_output_tap(&event, entry->keys[i], cfg->tap_ms, cfg->wait_ms);
        if (ret < 0) {
            // -EBUSY / -ENOSPC: 何も登録していない。-EAGAIN: 「離す」は自動で再試行される。
            // いずれも同じ文字の残りは送らない（順番が崩れないように）
            LOG_WRN("Behavior queue busy/full (err %d), dropped rest of kana id %d", ret, id);
            break;
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    // 押しっぱなしのキーは、かな配列モードの解除中でも必ず離す
    if (event.position < ZMK_KEYMAP_LEN && held_keycode[event.position] != 0) {
        const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
        const struct behavior_kana_config *cfg = dev->config;
        kana_output_release(&event, held_keycode[event.position], cfg->wait_ms);
        held_keycode[event.position] = 0;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kana_driver_api = {
    .binding_pressed = on_kana_binding_pressed,
    .binding_released = on_kana_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KANA_INST(n)                                                                               \
    KANA_CHECK_MS(n, tap_ms, 10000)                                                                \
    KANA_CHECK_MS(n, wait_ms, 10000)                                                               \
    KANA_CHECK_MS(n, ime_resync_ms, 60000)                                                         \
    static const struct behavior_kana_config behavior_kana_config_##n = {                         \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .ime_resync_ms = DT_INST_PROP(n, ime_resync_ms),                                           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kana_config_##n, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kana_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANA_INST)
