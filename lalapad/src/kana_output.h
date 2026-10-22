/*
 * かな入力モジュールの共通部分（送信キューと、かな配列モードの状態）。
 * behavior_kana.c（かな）・behavior_kana_seq.c（記号などの打鍵の列）・behavior_kana_mode.c（オン／オフ）から使う。
 * 送信キューの目印（kana_output_add_marker）は behavior_alt_code.c からも使う。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

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

/* 必要なら IME オン (LANGUAGE_1) のタップをキューに並べる（かな・記号を送る前に呼ぶ）。
 * resync_ms > 0: 最後にこの関数を呼んでから resync_ms 以上たっているときだけ送る（続けて打っている間は送らない）。
 * resync_ms = 0: 毎回送る。
 * 戻り値: 0 = 送らなくてよい、または登録できた / -EBUSY・-ENOSPC = 登録できなかった（次に呼んだときに改めて送る）
 *         -EAGAIN = 「押す」だけ登録できた（「離す」は自動で再試行する）。
 * 負の値のとき、呼び出し側は続く文字を送らずに打ち切ること */
int kana_output_ime_on(const struct zmk_behavior_binding_event *event, uint32_t tap_ms,
                       uint32_t wait_ms, uint32_t resync_ms);

/* かな配列モード中で、かつ解除処理中でなければ true（&kana_key の入力を受け付ける） */
bool kana_input_enabled(void);

/* かな配列モードの解除処理中（受付停止からレイヤー解除まで）なら true。この間の入力は捨てる */
bool kana_mode_exiting(void);

/* かな配列モードをオンにする: かなレイヤーを即座に有効化し、IME オン (LANGUAGE_1) をキューに並べる。
 * layer は keymap の宣言順の番号（&mo などと同じくレイヤー ID として扱う） */
void kana_mode_on(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                  uint32_t tap_ms, uint32_t wait_ms);

/* かな配列モードをオフにする: 即座に &kana_key の受付を止め、IME オフ (LANGUAGE_2) と完了の合図を
 * キューに並べ、合図が処理された（それより前の送信がすべて実行された）時点でかなレイヤーを解除する。
 * sub_layers（親指シフト面など）も同じ時点で解除する（親指を押したまま抜けてもシフト面を残さない） */
void kana_mode_off(const struct zmk_behavior_binding_event *event,
                   const zmk_keymap_layer_id_t *sub_layers, size_t sub_layers_len, uint32_t tap_ms,
                   uint32_t wait_ms);

/* 目印が処理されたときに呼ばれる関数（id は kana_output_add_marker に渡した番号） */
typedef void (*kana_output_marker_cb_t)(uint32_t id);

/* 目印が処理されたときに呼ぶ関数を登録する（1つだけ。Alt コードが使う） */
void kana_output_set_marker_handler(kana_output_marker_cb_t cb);

/* 送信キューの末尾に目印を並べる。それより前に並べた送信（かな・記号・マクロなど）がすべて実行されたとき、
 * 登録した関数が id（下位 31 ビット）を付けて呼ばれる。キューが空なら、この関数の中ですぐに呼ばれる。
 * 戻り値: 0 = 並べた / -EBUSY = 「離す」を再試行中なので並べていない / -ENOSPC = キューが満杯で並べていない */
int kana_output_add_marker(const struct zmk_behavior_binding_event *event, uint32_t id);

/* 完了の合図 (&kana_sync) がキューで処理されたときに behavior_kana_sync.c から呼ばれる */
void kana_output_on_sync(uint32_t token);
