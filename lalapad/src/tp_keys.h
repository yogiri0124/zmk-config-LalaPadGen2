/*
 * トラックパッドをキー操作に変えるモード（behavior_tp_keys.c・behavior_tp_stroke.c が定義し、
 * tp_keys.c の入力処理が使う）。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zmk/behavior.h>

/* bindings の並び（zmk,behavior-tp-keys）: 1 本指を上・下・左・右へ動かしたとき、2 本指を上・下・左・右へ
 * 動かしたとき（どちらも指を動かした向き。2 本指はスクロールの向きではない） */
#define TP_KEYS_BINDINGS_LEN 8
#define TP_KEYS_CURSOR_FIRST 0
#define TP_KEYS_SCROLL_FIRST 4

enum tp_keys_kind {
    /* zmk,behavior-tp-keys: 動かした量に応じて、その向きの binding を送る */
    TP_KEYS_KIND_MOTION,
    /* zmk,behavior-tp-stroke: 描いた向きの並びで binding を送る */
    TP_KEYS_KIND_STROKE,
};

struct tp_keys_mode {
    enum tp_keys_kind kind;
    /* MOTION: TP_KEYS_BINDINGS_LEN 個。STROKE: strokes と同じ数 */
    const struct zmk_behavior_binding *bindings;
    size_t num_bindings;
    /* MOTION: 1 回送るのに必要な動きの量（トラックパッドの生の値）。1 本指・2 本指 */
    uint16_t cursor_step;
    uint16_t scroll_step;
    /* STROKE: 向きの並び（dt-bindings/lalapad/tp_keys.h の TP_STROKEn）と、1 つの向きとみなす動きの量 */
    const uint16_t *strokes;
    uint16_t stroke_step;
    /* 押してから離すまで、離してから次を押すまでの時間 (ms) */
    uint16_t tap_ms;
};

/* zmk,behavior-tp-keys・zmk,behavior-tp-stroke の押した／離した（behavior の config が struct tp_keys_mode）。
 * 最後に押したキーのモードが有効になる */
int tp_keys_binding_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event);
int tp_keys_binding_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event);
