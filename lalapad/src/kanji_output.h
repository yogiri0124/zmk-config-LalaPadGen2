/*
 * 漢字入力の送り方（behavior_kanji.c）を、かなキーの同時押し（kanji_chord.c）から使うためのもの。
 */

#pragma once

#include <stdint.h>
#include <zmk/behavior.h>

/* 漢字の番号 id の符号を、かなと同じ送信キューで送る（最初の kanji_key の tap-ms などを使う）。
 * かな配列モードでないときは送らない。戻り値: 0 = 送った / 負の値 = 送らなかった（途中で打ち切った） */
int kanji_output_send(const struct zmk_behavior_binding_event *event, uint32_t id);

/* 同時押しとみなす時間 (ms)。0 なら同時押しを使わない（最初の kanji_key の chord-ms） */
uint32_t kanji_chord_ms(void);
