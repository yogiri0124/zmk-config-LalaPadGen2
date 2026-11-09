/*
 * かなキーの同時押しで漢字を出す（部分漢直）。
 *
 * かな配列モードの間、かなキー（&kana_key KN_…）を 2〜3 個ほぼ同時に押すと、漢字の表
 * （docs/google-ime/kanji.tsv の「同時押し」の列。docs/tools/gen_kanji.py が kanji.h の KANJI_CHORDS にする）
 * でその組み合わせの漢字を送る（送り方は &kanji_key と同じ。kanji_output_send）。レイヤーは使わない。
 *
 * 組み合わせは「かな」で決める（キーの位置ではない）。どのかなかは、キーマップがそのキーを押したときに決めたもの
 * （親指シフトを押しながら押せば、シフト面のかな）。
 *
 * 見分け方（同時に押したか、続けて打ったか）:
 * - 「押した瞬間」の差で決める（押していた長さでは決めない）。最初のキーを押してから chord-ms（kanji_key の設定。
 *   既定 40 ms）のうちに、表の組み合わせになるように残りのキーを押せば同時押し。時刻はキーを押したときの時刻
 *   （ZMK の combos などで遅れて届いても、押したときの時刻で比べる）。
 * - 組み合わせがそろい、それより多いキーの組み合わせ（3 キー）が表になければ、すぐ漢字を送る（待たない）。
 *   3 キーの組み合わせの一部になっている 2 キーは、chord-ms がたつか、ほかのキーを押す・預かったキーを離すまで
 *   待ってから送る。
 * - 表のどの組み合わせにも入っていないかなは預からない（今までどおりすぐ送る）。表の組み合わせに入っているかなだけ、
 *   単独で押したときも最大 chord-ms 待つ（その間にキーを離すか、ほかのかな・記号などを押せば、そこで送る）。
 * - chord-ms がたった時点で、その間に押した組み合わせのかなのキーが、まだ &kana_key に届いていない（ZMK の combos が
 *   Esc・Tab の候補として持っている、など）ときは、届くか離されるまで待つ（最大 CHORD_LATE_MAX_MS）。
 *   届いたキーは押したときの時刻で比べるので、押す順番で結果が変わらない。
 *
 * 仕組み: &kana_key が押されたとき（キーマップ・ZMK の combos・hold-tap の処理が済んで、どのかなかが決まった後）に、
 * そのかなの「送信」だけを預かる（キーの押した・離したの出来事はそのまま流れるので、キーマップの状態や
 * combos・レイヤーには影響しない）。
 * - 同時押しになったら、預かったかなは送らずに漢字を送る。
 * - ならなかったら、預かったかなを押した順に送る。
 * - 預かっている間に、かな入力の送信キューへ何かを並べるとき（ほかのかな・編集用のキー・記号・漢字のキー・
 *   Alt コード・かなモードの切り替え）は、kana_output.c がその前に kanji_chord_flush を呼び、預かった分を先に決める
 *   （順番が入れ替わらない）。
 * - 送信キューを通らないキー（Default から透ける矢印などの &kp、マウスのボタン）は、預かっているかなを追い越す
 *   ことがある（送信キューに並んだかなを追い越すことがあるのと同じ。README）。
 * - まだ届いていないキーを知るため、キー位置の出来事を、キーマップ・combos より前で見る（見るだけで、預からない）。
 *   押したキーが組み合わせのかなのキーなら、押した時刻を覚え、&kana_key に届いたら消す。届かないまま離された
 *   （combos が Esc などにした）ら、その出来事の処理が済んだ後で消す（combos は、持っていた「押した」を、
 *   「離した」より先に流すので、処理が済んだ後なら届いている）。
 *
 * 状態はシステムワークキューだけで触る（behavior の処理、キー位置の出来事、時間切れの処理はシステムワークキューで動く）。
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
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
#include "kanji_chord.h"
#include "kanji_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_kanji) && KJ_CHORD_COUNT > 0

/* 同時押しにできるキーの数の上限（docs/tools/gen_kanji.py の CHORD_MAX_KEYS と合わせる） */
#define CHORD_MAX_KEYS 3
/* chord-ms がたった後、まだ届いていない組み合わせのかなのキーを待つ時間の上限（ZMK の combos の timeout-ms の
 * 既定 50 ms より長く） */
#define CHORD_LATE_MAX_MS 100

struct kanji_chord {
    uint16_t kanji;               /* 漢字の番号（KJ_…） */
    uint8_t len;                  /* かなの数（2 か 3） */
    uint8_t kana[CHORD_MAX_KEYS]; /* かなの ID（小さい順。使わない分は 0） */
};

#define KANJI_CHORD_ENTRY(kanji_id, n, a, b, c) {.kanji = kanji_id, .len = n, .kana = {a, b, c}},
static const struct kanji_chord chords[] = {KANJI_CHORDS(KANJI_CHORD_ENTRY)};
BUILD_ASSERT(ARRAY_SIZE(chords) == KJ_CHORD_COUNT, "KANJI_CHORDS must match KJ_CHORD_COUNT");

/* 預かっているかな（押した順） */
struct held_kana {
    struct zmk_behavior_binding_event event;
    uint8_t kana;
    kanji_chord_send_kana_t send;
    const void *ctx;
};

static struct held_kana held[CHORD_MAX_KEYS];
static size_t held_len;
/* 最初に預かったかなを押した時刻（同時押しとみなす時間はここから数える） */
static int64_t first_timestamp;

/* 押したが、まだ &kana_key に届いていない組み合わせのかなのキー（キー位置ごと）と、押した時刻。
 * released は、届かないまま離されたもの（出来事の処理が済んだ後で消す） */
static ATOMIC_DEFINE(late_pressed, ZMK_KEYMAP_LEN);
static ATOMIC_DEFINE(late_released, ZMK_KEYMAP_LEN);
static int64_t late_timestamp[ZMK_KEYMAP_LEN];

static void chord_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(chord_timeout_work, chord_timeout_handler);
static void late_release_handler(struct k_work *work);
static K_WORK_DEFINE(late_release_work, late_release_handler);

/* kana が表のどれかの組み合わせに入っているか */
static bool kana_in_any_chord(uint32_t kana) {
    for (size_t i = 0; i < ARRAY_SIZE(chords); i++) {
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

/* set と同じ組み合わせの表の行（なければ NULL）を返す。more には set を含むより大きい組み合わせが表にあるか、
 * partial には set を含む組み合わせ（同じものも含む）が表にあるかを入れる */
static const struct kanji_chord *find_chord(const uint8_t *set, size_t len, bool *more,
                                            bool *partial) {
    const struct kanji_chord *exact = NULL;
    *more = false;
    *partial = false;
    for (size_t i = 0; i < ARRAY_SIZE(chords); i++) {
        if (chords[i].len < len || !chord_has_all(&chords[i], set, len)) {
            continue;
        }
        *partial = true;
        if (chords[i].len == len) {
            exact = &chords[i];
        } else {
            *more = true;
        }
    }
    return exact;
}

/* 預かっているかなを決める: 表の組み合わせなら漢字を送り、そうでなければかなを押した順に送る */
static void decide(void) {
    if (held_len == 0) {
        return;
    }
    k_work_cancel_delayable(&chord_timeout_work);

    uint8_t set[CHORD_MAX_KEYS];
    for (size_t i = 0; i < held_len; i++) {
        set[i] = held[i].kana;
    }
    bool more;
    bool partial;
    const struct kanji_chord *chord = find_chord(set, held_len, &more, &partial);

    // 送る処理の中から kanji_chord_flush が呼ばれても二重に送らないよう、先に預かりを空にする
    struct held_kana keys[CHORD_MAX_KEYS];
    const size_t len = held_len;
    memcpy(keys, held, sizeof(keys[0]) * len);
    held_len = 0;

    if (chord != NULL) {
        LOG_DBG("Kanji chord (%zu keys) -> kanji id %u", len, chord->kanji);
        kanji_output_send(&keys[0].event, chord->kanji);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        keys[i].send(&keys[i].event, keys[i].kana, keys[i].ctx);
    }
}

/* 預かっている組み合わせに加わるかもしれない、まだ届いていないキーがあるか（押した時刻が、最初のかなの
 * chord-ms 前から chord-ms 後までのもの） */
static bool late_key_pending(uint32_t chord_ms) {
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
        if (atomic_test_bit(late_pressed, pos) && !atomic_test_bit(late_released, pos) &&
            late_timestamp[pos] >= first_timestamp - (int64_t)chord_ms &&
            late_timestamp[pos] <= first_timestamp + (int64_t)chord_ms) {
            return true;
        }
    }
    return false;
}

/* 時間を見て決めるか、待ち続けるかを決める（時間切れ・まだ届いていないキーが離されたときに呼ぶ） */
static void check_timeout(void) {
    if (held_len == 0) {
        return;
    }
    const uint32_t chord_ms = kanji_chord_ms();
    const int64_t now = k_uptime_get();
    const int64_t deadline = first_timestamp + chord_ms;
    if (now < deadline) {
        k_work_reschedule(&chord_timeout_work, K_MSEC(deadline - now));
        return;
    }
    const int64_t late_deadline = deadline + CHORD_LATE_MAX_MS;
    if (now < late_deadline && late_key_pending(chord_ms)) {
        // まだ届いていないキーが届く（&kana_key で kanji_chord_kana_pressed が呼ばれる）か、
        // 離される（late_release_handler）まで待つ。どちらも来なければ late_deadline に決める
        k_work_reschedule(&chord_timeout_work, K_MSEC(late_deadline - now));
        return;
    }
    decide();
}

static void chord_timeout_handler(struct k_work *work) { check_timeout(); }

static void late_release_handler(struct k_work *work) {
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
        if (atomic_test_and_clear_bit(late_released, pos)) {
            atomic_clear_bit(late_pressed, pos);
        }
    }
    check_timeout();
}

static void hold(const struct zmk_behavior_binding_event *event, uint8_t kana,
                 kanji_chord_send_kana_t send, const void *ctx) {
    held[held_len] = (struct held_kana){.event = *event, .kana = kana, .send = send, .ctx = ctx};
    held_len++;
}

bool kanji_chord_kana_pressed(const struct zmk_behavior_binding_event *event, uint32_t kana,
                              kanji_chord_send_kana_t send, const void *ctx) {
    if (event->position < ZMK_KEYMAP_LEN) {
        // 届いた: まだ届いていないキーの印を消す
        atomic_clear_bit(late_pressed, event->position);
        atomic_clear_bit(late_released, event->position);
    }

    const uint32_t chord_ms = kanji_chord_ms();
    const bool candidate =
        chord_ms > 0 && kana >= KN_A && kana <= KN_VU && kana_in_any_chord(kana);

    if (held_len > 0) {
        if (candidate && held_len < CHORD_MAX_KEYS &&
            event->timestamp - first_timestamp <= (int64_t)chord_ms &&
            first_timestamp - event->timestamp <= (int64_t)chord_ms) {
            uint8_t set[CHORD_MAX_KEYS];
            bool duplicate = false;
            for (size_t i = 0; i < held_len; i++) {
                set[i] = held[i].kana;
                duplicate |= set[i] == kana;
            }
            set[held_len] = (uint8_t)kana;
            bool more = false;
            bool partial = false;
            const struct kanji_chord *exact =
                duplicate ? NULL : find_chord(set, held_len + 1, &more, &partial);
            if (!duplicate && partial) {
                // 表の組み合わせの途中か、そろった
                hold(event, (uint8_t)kana, send, ctx);
                if (exact != NULL && !more) {
                    decide(); // そろって、これより大きい組み合わせもない: すぐ送る
                } else {
                    check_timeout(); // 時間切れの後に届いたキーなら、ここで決めるか待ち続けるかを見直す
                }
                return true;
            }
        }
        // 組み合わせにならないかな・時間外のかな: 預かっている分を先に決めてから、このかなを改めて見る
        decide();
    }

    if (!candidate) {
        return false;
    }

    hold(event, (uint8_t)kana, send, ctx);
    first_timestamp = event->timestamp;
    // 押した時刻から chord-ms たったら決める（combos などで遅れて届いたときは、すぐ見直す）
    check_timeout();
    return true;
}

void kanji_chord_kana_released(const struct zmk_behavior_binding_event *event) {
    for (size_t i = 0; i < held_len; i++) {
        if (held[i].event.position == event->position) {
            decide(); // 預かったキーを離した: そこで決める
            return;
        }
    }
}

void kanji_chord_flush(void) { decide(); }

/* ---- まだ届いていないキーを知る（キー位置の出来事を、キーマップ・combos より前で見るだけ） ---- */

#define KANA_DEV(node_id) DEVICE_DT_GET(node_id),
static const struct device *const kana_devs[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_kana, KANA_DEV)};

#if DT_NODE_HAS_STATUS(DT_NODELABEL(trans), okay)
#define TRANS_DEV DEVICE_DT_GET(DT_NODELABEL(trans))
#else
#define TRANS_DEV NULL
#endif

static bool is_kana_dev(const struct device *dev) {
    for (size_t i = 0; i < ARRAY_SIZE(kana_devs); i++) {
        if (kana_devs[i] == dev) {
            return true;
        }
    }
    return false;
}

/* position を今押したら、表の組み合わせのかなになるか（キーマップと同じく、有効なレイヤーを上から見て、
 * 透過は下へ） */
static bool is_chord_kana_position(uint32_t position) {
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
        return dev != NULL && is_kana_dev(dev) && binding->param1 >= KN_A &&
               binding->param1 <= KN_VU && kana_in_any_chord(binding->param1);
    }
    return false;
}

static int kanji_chord_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || ev->position >= ZMK_KEYMAP_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->state) {
        atomic_clear_bit(late_released, ev->position);
        if (kana_input_enabled() && kanji_chord_ms() > 0 && is_chord_kana_position(ev->position)) {
            late_timestamp[ev->position] = ev->timestamp;
            atomic_set_bit(late_pressed, ev->position);
        } else {
            atomic_clear_bit(late_pressed, ev->position);
        }
    } else if (atomic_test_bit(late_pressed, ev->position)) {
        // 届かないまま離された（か、ZMK の combos がまだ「押した」を持っている）: この出来事の処理が済んで
        // から消す（combos は持っていた「押した」を先に流すので、その後なら届いている）
        atomic_set_bit(late_released, ev->position);
        k_work_submit(&late_release_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kanji_chord, kanji_chord_position_listener);
ZMK_SUBSCRIPTION(kanji_chord, zmk_position_state_changed);

#else // 漢字のキー（kanji_key）がない、または表に同時押しがない: 何も預からない

bool kanji_chord_kana_pressed(const struct zmk_behavior_binding_event *event, uint32_t kana,
                              kanji_chord_send_kana_t send, const void *ctx) {
    return false;
}

void kanji_chord_kana_released(const struct zmk_behavior_binding_event *event) {}

void kanji_chord_flush(void) {}

#endif
