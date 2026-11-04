/*
 * トラックパッドをキー操作に変えるモード（behavior_tp_keys.c・behavior_tp_stroke.c が定義し、
 * tp_keys.c の入力処理が使う）。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk/behavior.h>

/* bindings の並び（zmk,behavior-tp-keys）: 1 本指の上・下・左・右、2 本指（スクロール）の上・下・左・右 */
#define TP_KEYS_BINDINGS_LEN 8
#define TP_KEYS_CURSOR_FIRST 0
#define TP_KEYS_SCROLL_FIRST 4

struct tp_keys_mode {
    /* zmk,behavior-tp-keys: 動かした量に応じて送るもの（TP_KEYS_BINDINGS_LEN 個）。一筆書きなら NULL */
    const struct zmk_behavior_binding *bindings;
    /* 1 本指・2 本指を変えるか（4 つとも &trans なら変えずに、カーソル・スクロールのまま通す） */
    bool convert_cursor;
    bool convert_scroll;
    /* 1 回送るのに必要な動きの量（トラックパッドの生の値） */
    uint16_t cursor_step;
    uint16_t scroll_step;

    /* zmk,behavior-tp-stroke: 向きの並び（dt-bindings/lalapad/tp_keys.h の TP_STROKEn）と、そのとき送るもの */
    const uint16_t *strokes;
    const struct zmk_behavior_binding *stroke_bindings;
    size_t num_strokes;
    /* 1 つの向きとみなす動きの量 */
    uint16_t stroke_step;

    /* 押してから離すまで、離してから次を押すまでの時間 (ms) */
    uint16_t tap_ms;
};

/* モードのキーを押した／離した（システムワークキューから呼ぶ）。最後に押したキーのモードが有効になる */
void tp_keys_mode_pressed(const struct tp_keys_mode *mode, const struct zmk_behavior_binding_event *event);
void tp_keys_mode_released(const struct tp_keys_mode *mode, const struct zmk_behavior_binding_event *event);
