/*
 * かなキーの同時押しで漢字を出す（部分漢直）。
 *
 * かな配列モードの間、かなキー（&kana_key KN_…）を 2〜3 個ほぼ同時に押すと、漢字の表
 * （docs/google-ime/kanji.tsv の「同時押し」の列。docs/tools/gen_kanji.py が kanji.h の KANJI_CHORDS にする）
 * でその組み合わせの漢字を送る（送り方は &kanji_key と同じ。kanji_output_send）。レイヤーは使わない。
 *
 * 組み合わせは「かな」で決める（キーの位置ではない）。どのかなかは、押したときに有効なレイヤーで決まる
 * （親指シフトを押しながら押せば、シフト面のかな）。
 *
 * 見分け方（同時に押したか、続けて打ったか）:
 * - 「押した瞬間」の差で決める（押していた長さでは決めない）。最初のキーを押してから chord-ms（kanji_key の設定。
 *   既定 40 ms）のうちに、表の組み合わせになるように残りのキーを押せば同時押し。速く続けて打つときの指の重なり
 *   （前のキーを離す前に次を押す）は、押した瞬間の差が chord-ms より長いので、かなのまま。
 * - 組み合わせがそろい、それより多いキーの組み合わせ（3 キー）が表になければ、すぐ漢字を送る（待たない）。
 *   3 キーの組み合わせの一部になっている 2 キーは、chord-ms がたつか、ほかのキーを押す・離すまで待ってから送る。
 * - 表のどの組み合わせにも入っていないかなのキーは、預からない（今までどおりすぐ送る）。表の組み合わせに入っている
 *   かなのキーだけ、単独で押したときも最大 chord-ms 待つ（その間にキーを離せば、そこで送る）。
 *
 * 仕組み: キーマップより前でキー位置の出来事（zmk_position_state_changed）を見て、同時押しの候補の「押した」を預かる。
 * - 同時押しになったら、預かった「押した」は捨てて漢字を送り、それらのキーの「離した」も捨てる。
 * - ならなかったら、預かった「押した」を届いた順のまま、この listener の次から流し直す（ZMK_EVENT_RELEASE）。
 * - 預かっている間にほかの出来事（ほかのキーの押した・離した。親指シフトの「離した」など）が届いたら、先に決めて
 *   （同時押しか、流し直すか）から、その出来事を通す。順番が入れ替わらないので、預かったかなが、後から届いた
 *   親指シフトの「離した」より後に処理されてシフト面が外れる、といったことは起きない。
 * listener は、オートマウスレイヤーなど、キー位置の出来事を見るほかの自作モジュールより後、ZMK のキーマップ・
 * combos より前に呼ばれる（CMakeLists.txt で最後に足す。listener はリンクの順に呼ばれ、モジュールは ZMK 本体より前）。
 * ZMK の combos（Esc・Tab の同時押しなど）より前に見るので、同じかなの組み合わせを表に書くと、こちらが先に取る。
 *
 * 状態はシステムワークキューだけで触る（キー位置の出来事も、時間切れの処理もシステムワークキューで動く）。
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

#include <dt-bindings/kana/kana.h>
#include <dt-bindings/kana/kanji.h>

#include "kana_output.h"
#include "kanji_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_kanji)

/* 同時押しにできるキーの数の上限 */
#define CHORD_MAX_KEYS 3

struct kanji_chord {
    uint16_t kanji;               /* 漢字の番号（KJ_…） */
    uint8_t len;                  /* かなの数（2 か 3） */
    uint8_t kana[CHORD_MAX_KEYS]; /* かなの ID（小さい順。使わない分は 0） */
};

#if KJ_CHORD_COUNT > 0
#define KANJI_CHORD_ENTRY(kanji_id, n, a, b, c) {.kanji = kanji_id, .len = n, .kana = {a, b, c}},
static const struct kanji_chord chords[] = {KANJI_CHORDS(KANJI_CHORD_ENTRY)};
BUILD_ASSERT(ARRAY_SIZE(chords) == KJ_CHORD_COUNT, "KANJI_CHORDS must match KJ_CHORD_COUNT");
#define CHORDS_LEN ARRAY_SIZE(chords)
#else
static const struct kanji_chord *const chords = NULL;
#define CHORDS_LEN 0
#endif

/* かなのキー（&kana_key）の behavior と、透過（&trans） */
#define KANA_DEV(node_id) DEVICE_DT_GET(node_id),
static const struct device *const kana_devs[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_kana, KANA_DEV)};

#if DT_NODE_HAS_STATUS(DT_NODELABEL(trans), okay)
#define TRANS_DEV DEVICE_DT_GET(DT_NODELABEL(trans))
#else
#define TRANS_DEV NULL
#endif

/* 預かっている「押した」（届いた順）と、そのキーのかな・レイヤー */
struct held_key {
    struct zmk_position_state_changed_event ev;
    uint8_t kana;
    zmk_keymap_layer_id_t layer;
};

static struct held_key held[CHORD_MAX_KEYS];
static size_t held_len;
/* 最初に預かった「押した」の時刻（同時押しとみなす時間はここから数える） */
static int64_t first_timestamp;
/* 同時押しにしたキー（「離した」を捨てる） */
static ATOMIC_DEFINE(consumed, ZMK_KEYMAP_LEN);

static void chord_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(chord_timeout_work, chord_timeout_handler);

static bool is_kana_dev(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(kana_devs); i++) {
        if (kana_devs[i] == dev) {
            return true;
        }
    }
    return false;
}

/* position を今押したら、どのかなになるか（キーマップと同じく、有効なレイヤーを上から見て、透過は下へ）。
 * かな（KN_A〜KN_VU）の &kana_key なら true */
static bool resolve_kana(uint32_t position, uint8_t *kana, zmk_keymap_layer_id_t *layer) {
    const zmk_keymap_layer_id_t default_layer = zmk_keymap_layer_default();
    for (int idx = ZMK_KEYMAP_LAYERS_LEN - 1; idx >= 0; idx--) {
        const zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(idx);
        if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL || !zmk_keymap_layer_active(layer_id)) {
            continue;
        }
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer_id, position);
        const struct device *dev =
            binding == NULL ? NULL : zmk_behavior_get_binding(binding->behavior_dev);
        if (dev != NULL && dev == TRANS_DEV && layer_id != default_layer) {
            continue; // 透過: 下のレイヤーを見る
        }
        if (dev == NULL || !is_kana_dev(dev) || binding->param1 < KN_A ||
            binding->param1 > KN_VU) {
            return false;
        }
        *kana = binding->param1;
        *layer = layer_id;
        return true;
    }
    return false;
}

/* kana が表のどれかの組み合わせに入っているか */
static bool kana_in_any_chord(uint8_t kana) {
    for (size_t i = 0; i < CHORDS_LEN; i++) {
        for (size_t k = 0; k < chords[i].len; k++) {
            if (chords[i].kana[k] == kana) {
                return true;
            }
        }
    }
    return false;
}

/* chord が、かなの集まり set（len 個。重複なし）をすべて含むか */
static bool chord_has_all(const struct kanji_chord *chord, const uint8_t *set, size_t len) {
    for (size_t i = 0; i < len; i++) {
        bool found = false;
        for (size_t k = 0; k < chord->len; k++) {
            found |= chord->kana[k] == set[i];
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

/* set と同じ組み合わせの表の行（なければ NULL）。more には set を含むより大きい組み合わせが表にあるか、
 * partial には set を含む組み合わせ（同じものも含む）が表にあるかを入れる（NULL なら入れない） */
static const struct kanji_chord *find_chord(const uint8_t *set, size_t len, bool *more,
                                            bool *partial) {
    const struct kanji_chord *exact = NULL;
    bool larger = false;
    bool any = false;
    for (size_t i = 0; i < CHORDS_LEN; i++) {
        if (chords[i].len < len || !chord_has_all(&chords[i], set, len)) {
            continue;
        }
        any = true;
        if (chords[i].len == len) {
            exact = &chords[i];
        } else {
            larger = true;
        }
    }
    if (more != NULL) {
        *more = larger;
    }
    if (partial != NULL) {
        *partial = any;
    }
    return exact;
}

static void held_kana_set(uint8_t *set) {
    for (size_t i = 0; i < held_len; i++) {
        set[i] = held[i].kana;
    }
}

/* 預かっているキーを決める: 表の組み合わせなら漢字を送り、そうでなければ届いた順に流し直す */
static void decide(void) {
    if (held_len == 0) {
        return;
    }
    k_work_cancel_delayable(&chord_timeout_work);

    uint8_t set[CHORD_MAX_KEYS];
    held_kana_set(set);
    const struct kanji_chord *chord = find_chord(set, held_len, NULL, NULL);

    // 流し直している間に、ほかの listener の処理から自分が呼ばれても大丈夫なよう、先に預かりを空にする
    struct held_key keys[CHORD_MAX_KEYS];
    const size_t len = held_len;
    memcpy(keys, held, sizeof(keys[0]) * len);
    held_len = 0;

    if (chord != NULL) {
        for (size_t i = 0; i < len; i++) {
            atomic_set_bit(consumed, keys[i].ev.data.position);
        }
        struct zmk_behavior_binding_event event = {
            .layer = keys[0].layer,
            .position = keys[0].ev.data.position,
            .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = keys[0].ev.data.source,
#endif
        };
        LOG_DBG("Kanji chord (%zu keys) -> kanji id %u", len, chord->kanji);
        kanji_output_send(&event, chord->kanji);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        ZMK_EVENT_RELEASE(keys[i].ev);
    }
}

static void chord_timeout_handler(struct k_work *work) { decide(); }

static void hold_key(const struct zmk_position_state_changed *ev, uint8_t kana,
                     zmk_keymap_layer_id_t layer) {
    held[held_len].ev = copy_raised_zmk_position_state_changed(ev);
    held[held_len].kana = kana;
    held[held_len].layer = layer;
    held_len++;
}

static int on_press(const struct zmk_position_state_changed *ev, uint32_t chord_ms) {
    uint8_t kana = 0;
    zmk_keymap_layer_id_t layer = 0;
    const bool candidate = kana_input_enabled() && ev->position < ZMK_KEYMAP_LEN &&
                           resolve_kana(ev->position, &kana, &layer) && kana_in_any_chord(kana);

    if (held_len > 0) {
        if (candidate && held_len < CHORD_MAX_KEYS &&
            ev->timestamp - first_timestamp <= (int64_t)chord_ms) {
            uint8_t set[CHORD_MAX_KEYS];
            held_kana_set(set);
            bool duplicate = false;
            for (size_t i = 0; i < held_len; i++) {
                duplicate |= set[i] == kana;
            }
            set[held_len] = kana;
            bool more = false;
            bool partial = false;
            const struct kanji_chord *exact =
                duplicate ? NULL : find_chord(set, held_len + 1, &more, &partial);
            if (!duplicate && partial) {
                // 表の組み合わせの途中か、そろった
                hold_key(ev, kana, layer);
                if (exact != NULL && !more) {
                    decide(); // そろって、これより大きい組み合わせもない: すぐ送る
                }
                return ZMK_EV_EVENT_CAPTURED;
            }
        }
        // 組み合わせにならないキー・時間切れ後のキー: 預かっている分を先に決めてから、このキーを改めて見る
        decide();
    }

    if (!candidate) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    hold_key(ev, kana, layer);
    first_timestamp = ev->timestamp;
    const int64_t wait = first_timestamp + chord_ms - k_uptime_get();
    k_work_reschedule(&chord_timeout_work, K_MSEC(MAX(wait, 0)));
    return ZMK_EV_EVENT_CAPTURED;
}

static int on_release(const struct zmk_position_state_changed *ev) {
    if (held_len > 0) {
        // 預かっているキーの「離した」も、ほかのキーの「離した」も、先に決めてから通す
        // （預かったキーを離したときは、そこで同時押しかどうかが決まる）
        decide();
    }
    if (ev->position < ZMK_KEYMAP_LEN && atomic_test_and_clear_bit(consumed, ev->position)) {
        return ZMK_EV_EVENT_HANDLED; // 同時押しにしたキーの「離した」は捨てる
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int kanji_chord_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint32_t chord_ms = kanji_chord_ms();
    if (CHORDS_LEN == 0 || chord_ms == 0) {
        // 表に同時押しがない・使わない設定: 何もしない（預かっていた分は、設定は変わらないので無い）
        return ZMK_EV_EVENT_BUBBLE;
    }

    return ev->state ? on_press(ev, chord_ms) : on_release(ev);
}

ZMK_LISTENER(kanji_chord, kanji_chord_listener);
ZMK_SUBSCRIPTION(kanji_chord, zmk_position_state_changed);

#endif // DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_kanji)
