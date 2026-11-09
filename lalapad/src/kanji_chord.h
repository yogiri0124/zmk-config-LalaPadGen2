/*
 * かなキーの同時押しで漢字を出す仕組み（kanji_chord.c）。&kana_key（behavior_kana.c）が使う。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zmk/behavior.h>

/* 預かったかなを、同時押しにならなかったときに送る関数（ctx は kanji_chord_kana_pressed に渡したもの） */
typedef void (*kanji_chord_send_kana_t)(const struct zmk_behavior_binding_event *event,
                                        uint32_t kana, const void *ctx);

/* &kana_key でかな（KN_…）を押したときに呼ぶ。true なら同時押しの判定のために預かった（呼び出し側は送らない。
 * 同時押しにならなければ、後で send が呼ばれる）。false なら呼び出し側がすぐ送る
 * （預かっていた分は、この中で先に決めて送ってある） */
bool kanji_chord_kana_pressed(const struct zmk_behavior_binding_event *event, uint32_t kana,
                              kanji_chord_send_kana_t send, const void *ctx);

/* &kana_key を離したときに呼ぶ（預かっているキーなら、そこで同時押しかどうかを決める） */
void kanji_chord_kana_released(const struct zmk_behavior_binding_event *event);

/* 預かっているかながあれば、いま決めて送る（同時押しの組み合わせなら漢字、そうでなければかな）。
 * 送信キューに並べる前に kana_output.c が呼ぶ（記号・漢字のキー・Alt コード・編集用のキーなどが、
 * 預かっているかなを追い越さないように） */
void kanji_chord_flush(void);
