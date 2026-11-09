/*
 * かな入力 behavior (&kana_key)。
 *
 * `&kana_key KN_KA` のように文字 ID を受け取り、対応するローマ字のキー列を
 * ZMK の behavior queue（マクロと同じ仕組み）経由で &kp として順に送る。
 * かな漢字変換はホスト側の IME（ローマ字入力モード）が行う。
 * ホストのキーボード配列は英語配列 (US) を前提にしている。
 * 送るのは、かな（KN_）と、編集用のキー（KE_）。
 * 記号（、。… など）は、keymap で定義する zmk,behavior-kana-seq のキー（behavior_kana_seq.c）で送る。
 * 編集用のキー（KE_。スペース・エンター・バックスペース・Esc・Tab）も同じキューで送り、かな列を追い越さない
 * ようにする（押している間は押しっぱなしにして、キーリピートができるようにする。combos に置いても同じ）。
 * かなを送る間は、押している Shift・Ctrl（&sk の Shift も）を隠す（"Ka" や Ctrl+K にならないように。
 * Alt・Win は隠さない。理由と制約は kana_output.c の先頭）。
 * 編集用のキーでは隠さない（Shift+Enter・Ctrl+Backspace・Shift+Tab などとして使えるように）。
 * キューへの登録・押しっぱなし防止・修飾キーを隠す処理・かな配列モードの状態は kana_output.c にまとめている。
 * かなキーの同時押しで漢字を出す組み合わせ（docs/google-ime/kanji.tsv の「同時押し」）に入っているかなは、
 * 同時押しかどうかが決まるまで送るのを預ける（kanji_chord.c）。
 */

#define DT_DRV_COMPAT zmk_behavior_kana

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/matrix.h>

#include <dt-bindings/kana/kana.h>
#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"
#include "kanji_chord.h"

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
    ENTRY(KE_BSPC, "バックスペース", K1(BACKSPACE)) \
    ENTRY(KE_ESC, "エスケープ", K1(ESCAPE)) \
    ENTRY(KE_TAB, "タブ", K1(TAB))

// behavior のノードがないとき（入力処理など、同じモジュールのほかの部分だけを使うとき）に未使用の警告を出さない
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define KANA_TABLE_ENTRY(id, name, keys) [id] = keys,

static const struct kana_entry kana_table[KN_MAX_ID + 1] = {KANA_LIST(KANA_TABLE_ENTRY)};

/* かな（1〜KN_VU）と、編集用のキー（KE_SPACE〜KE_TAB）。その間の番号は欠番（kana.h に ID を足したら KANA_LIST にも足す） */
#define KANA_COUNT_ENTRY(id, name, keys) +1
BUILD_ASSERT((0 KANA_LIST(KANA_COUNT_ENTRY)) == KN_VU + (KE_TAB - KE_SPACE + 1),
             "KANA_LIST must cover every kana ID");

struct behavior_kana_config {
    uint32_t tap_ms;
    uint32_t wait_ms;
    uint32_t ime_resync_ms;
};

/* 編集用のキー（スペース・エンター・バックスペース・Esc・Tab）は押しっぱなし（キーリピート）ができるよう、押したとき「押す」、
 * 離したとき「離す」を送る。キー位置ごとに押しているキーコードを覚えておく（0 = 押していない）。
 * キーマップのキー位置は位置の番号で、それ以外（combos の位置など）は held_extra で覚える */
static uint32_t held_keycode[ZMK_KEYMAP_LEN];

static struct {
    uint32_t position;
    uint32_t keycode;
} held_extra[KANA_HOLD_EXTRA_MAX];

/* キー位置の「押しているキーコード」の置き場所。alloc = true なら、空いている置き場所を割り当てる。
 * 見つからなければ NULL（alloc = true で NULL なら、置き場所が足りない） */
static uint32_t *held_slot(uint32_t position, bool alloc) {
    if (position < ZMK_KEYMAP_LEN) {
        return &held_keycode[position];
    }
    for (size_t i = 0; i < ARRAY_SIZE(held_extra); i++) {
        if (held_extra[i].keycode != 0 && held_extra[i].position == position) {
            return &held_extra[i].keycode;
        }
    }
    if (!alloc) {
        return NULL;
    }
    for (size_t i = 0; i < ARRAY_SIZE(held_extra); i++) {
        if (held_extra[i].keycode == 0) {
            held_extra[i].position = position;
            return &held_extra[i].keycode;
        }
    }
    return NULL;
}

static bool is_hold_key(uint32_t id) { return id >= KE_SPACE && id <= KE_TAB; }

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

#define KANA_METADATA_ENTRY(id, name, keys)                                                        \
    {.display_name = name, .value = id, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},

static const struct behavior_parameter_value_metadata param_values[] = {
    KANA_LIST(KANA_METADATA_ENTRY)};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/* かな 1 文字（KN_…）のローマ字を送る。&kana_key を押したときと、同時押しの判定で預けたかなが同時押しに
 * ならなかったとき（kanji_chord.c）に使う。ctx は behavior_kana_config */
static void send_kana(const struct zmk_behavior_binding_event *event, uint32_t id,
                      const void *ctx) {
    const struct behavior_kana_config *cfg = ctx;
    // かなの前だけ、しばらく打っていなかったら IME オンを先に送る（LANGUAGE_1 はトグルではないので重ねても害がない。
    // ime-resync-ms が 0 なら送らない）。
    // 編集用のキーの前には送らない（IME オフのときに全角スペースなどにならないように）。
    // IME オンを登録できなかったときは、順番を守るため、この文字は送らない（次の入力で改めて IME オンを試す）
    const struct kana_entry *entry = &kana_table[id];
    const int ret = kana_output_send_keys(event, entry->keys, entry->len, cfg->tap_ms,
                                          cfg->wait_ms, cfg->ime_resync_ms > 0,
                                          cfg->ime_resync_ms);
    if (ret < 0) {
        // -EBUSY / -ENOSPC: その打鍵から先は登録していない。-EAGAIN: 「離す」は自動で再試行される。
        // いずれも同じ文字の残りは送っていない（順番が崩れないように）
        LOG_WRN("Behavior queue busy/full (err %d), dropped (rest of) kana id %u", ret, id);
    }
}

static int on_kana_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kana_config *cfg = dev->config;
    const uint32_t id = binding->param1;

    if (id == 0 || id > KN_MAX_ID || kana_table[id].len == 0) {
        LOG_WRN("Unknown kana id %u", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (!kana_input_enabled()) {
        // かな配列モードでない、または解除処理中（IME オフの後にローマ字が出ないよう捨てる）
        LOG_DBG("Kana input disabled, dropped kana id %u", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (is_hold_key(id)) {
        const uint32_t keycode = kana_table[id].keys[0];
        uint32_t *slot = held_slot(event.position, true);
        if (slot == NULL) {
            // 押しっぱなしを覚える置き場所が足りない（combos などを同時にたくさん押した）: 1回タップする
            const int ret = kana_output_tap(&event, keycode, cfg->tap_ms, cfg->wait_ms);
            if (ret < 0) {
                // -EAGAIN は「押す」だけ登録済み（「離す」は自動で再試行される）
                LOG_WRN("Behavior queue busy/full (err %d), kana id %u", ret, id);
            }
        } else if (*slot == 0) {
            const int ret = kana_output_press(&event, keycode, cfg->tap_ms);
            if (ret == 0) {
                *slot = keycode;
            } else {
                LOG_WRN("Behavior queue busy/full (err %d), dropped kana id %u", ret, id);
            }
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // かなキーの同時押しで漢字を出す組み合わせに入っているかなは、同時押しかどうかが決まるまで預ける
    // （kanji_chord.c。同時押しにならなければ、後で send_kana が送る）
    if (kanji_chord_kana_pressed(&event, id, send_kana, cfg)) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    send_kana(&event, id, cfg);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    // 同時押しの判定で預けているかなのキーを離したら、そこで決める
    kanji_chord_kana_released(&event);

    // 押しっぱなしのキーは、かな配列モードの解除中でも必ず離す
    uint32_t *slot = held_slot(event.position, false);
    if (slot != NULL && *slot != 0) {
        const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
        const struct behavior_kana_config *cfg = dev->config;
        kana_output_release(&event, *slot, cfg->wait_ms);
        *slot = 0;
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

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
