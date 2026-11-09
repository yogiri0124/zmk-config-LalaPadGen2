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
 *   既定 40 ms）のうちに、表の組み合わせになるように残りのキーを押せば同時押し。
 * - 組み合わせがそろい、それより多いキーの組み合わせ（3 キー）が表になければ、すぐ漢字を送る（待たない）。
 *   3 キーの組み合わせの一部になっている 2 キーは、chord-ms がたつか、ほかのキーを押す・預かったキーを離すまで
 *   待ってから送る。
 * - 表のどの組み合わせにも入っていないかなは預からない（今までどおりすぐ送る）。表の組み合わせに入っているかなだけ、
 *   単独で押したときも最大 chord-ms 待つ（その間にキーを離すか、ほかのかな・記号などを押せば、そこで送る）。
 *
 * 仕組み: &kana_key が押されたとき（キーマップ・ZMK の combos・hold-tap の処理が済んで、どのかなかが決まった後）に、
 * そのかなの「送信」だけを預かる（キーの押した・離したの出来事はそのまま流れるので、キーマップの状態や
 * combos・レイヤーには影響しない）。
 * - 同時押しになったら、預かったかなは送らずに漢字を送る。
 * - ならなかったら、預かったかなを押した順に送る。
 * - 預かっている間に、かな入力の送信キューへ何かを並べるとき（ほかのかな・編集用のキー・記号・漢字のキー・
 *   Alt コード・かなモードの切り替え）は、kana_output.c がその前に kanji_chord_flush を呼び、預かった分を先に決める
 *   （順番が入れ替わらない）。
 * - 送信キューを通らないキー（Default から透ける矢印などの &kp、マウスのボタン）は、預かっているかな
 *   （最大 chord-ms）を追い越すことがある（送信キューに並んだかなを追い越すことがあるのと同じ。README）。
 *
 * 状態はシステムワークキューだけで触る（behavior の処理も、時間切れの処理もシステムワークキューで動く）。
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>

#include <dt-bindings/kana/kana.h>
#include <dt-bindings/kana/kanji.h>

#include "kanji_chord.h"
#include "kanji_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_kanji) && KJ_CHORD_COUNT > 0

/* 同時押しにできるキーの数の上限（docs/tools/gen_kanji.py の CHORD_MAX_KEYS と合わせる） */
#define CHORD_MAX_KEYS 3

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

static void chord_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(chord_timeout_work, chord_timeout_handler);

/* kana が表のどれかの組み合わせに入っているか */
static bool kana_in_any_chord(uint8_t kana) {
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

static void chord_timeout_handler(struct k_work *work) { decide(); }

static void hold(const struct zmk_behavior_binding_event *event, uint8_t kana,
                 kanji_chord_send_kana_t send, const void *ctx) {
    held[held_len] = (struct held_kana){.event = *event, .kana = kana, .send = send, .ctx = ctx};
    held_len++;
}

bool kanji_chord_kana_pressed(const struct zmk_behavior_binding_event *event, uint32_t kana,
                              kanji_chord_send_kana_t send, const void *ctx) {
    const uint32_t chord_ms = kanji_chord_ms();
    const bool candidate = chord_ms > 0 && kana >= KN_A && kana <= KN_VU &&
                           kana_in_any_chord((uint8_t)kana);

    if (held_len > 0) {
        if (candidate && held_len < CHORD_MAX_KEYS &&
            event->timestamp - first_timestamp <= (int64_t)chord_ms) {
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
                }
                return true;
            }
        }
        // 組み合わせにならないかな・時間切れ後のかな: 預かっている分を先に決めてから、このかなを改めて見る
        decide();
    }

    if (!candidate) {
        return false;
    }

    hold(event, (uint8_t)kana, send, ctx);
    first_timestamp = event->timestamp;
    // 押した時刻から chord-ms たったら決める（combos などで遅れて届いたときは、すぐ）
    const int64_t wait = first_timestamp + chord_ms - k_uptime_get();
    k_work_reschedule(&chord_timeout_work, K_MSEC(MAX(wait, 0)));
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

#else // 漢字のキー（kanji_key）がない、または表に同時押しがない: 何も預からない

bool kanji_chord_kana_pressed(const struct zmk_behavior_binding_event *event, uint32_t kana,
                              kanji_chord_send_kana_t send, const void *ctx) {
    return false;
}

void kanji_chord_kana_released(const struct zmk_behavior_binding_event *event) {}

void kanji_chord_flush(void) {}

#endif
