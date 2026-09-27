/*
 * かな入力モジュールの共通部分（送信キューと、かな配列モードの状態）。
 * behavior_kana.c（文字）と behavior_kana_mode.c（オン／オフ）から使う。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zmk/behavior.h>

/* keycode を1回タップする操作（押す→離す）を behavior queue に並べる。
 * 戻り値: 0 = 登録できた
 *         -EBUSY  = 前のキーの「離す」を再試行中なので何も登録していない
 *         -ENOSPC = キューが満杯で何も登録していない
 *         -EAGAIN = 「押す」だけ登録できた。「離す」は登録できるまで自動で再試行する
 *                   （呼び出し側は、同じ文字の残りのキーを送らずに打ち切ること） */
int kana_output_tap(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                    uint32_t tap_ms, uint32_t wait_ms);

/* keycode の「押す」だけをキューに並べる（押しっぱなしにするキー用）。
 * 戻り値: 0 = 登録できた / -EBUSY = 「離す」を再試行中なので登録していない / -ENOSPC = キューが満杯 */
int kana_output_press(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                      uint32_t tap_ms);

/* keycode の「離す」をキューに並べる。満杯なら登録できるまで順番どおりに自動で再試行する（必ず離される） */
void kana_output_release(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                         uint32_t wait_ms);

/* かな配列モード中で、かつ解除処理中でなければ true（&kana の入力を受け付ける） */
bool kana_input_enabled(void);

/* かな配列モードをオンにする: かなレイヤーを即座に有効化し、IME オン (LANGUAGE_1) をキューに並べる */
void kana_mode_on(const struct zmk_behavior_binding_event *event, uint8_t layer_index,
                  uint32_t tap_ms, uint32_t wait_ms);

/* かな配列モードをオフにする: 即座に &kana の受付を止め、IME オフ (LANGUAGE_2) と完了の合図を
 * キューに並べ、合図が処理された（それより前の送信がすべて実行された）時点でかなレイヤーを解除する */
void kana_mode_off(const struct zmk_behavior_binding_event *event, uint32_t tap_ms,
                   uint32_t wait_ms);

/* 完了の合図 (&kana_sync) がキューで処理されたときに behavior_kana_sync.c から呼ばれる */
void kana_output_on_sync(uint32_t token);
