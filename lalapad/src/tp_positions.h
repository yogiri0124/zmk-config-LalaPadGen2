/*
 * トラックパッドのクリック・ジェスチャーのキー位置（自作モジュールの中で使う分）。
 * 値は dt-bindings/lalapad/tp_positions.h（shield の dtsi と共通）に書いてある。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>
#include <zmk/matrix.h>

#include <dt-bindings/lalapad/tp_positions.h>

/* トラックパッド由来の位置の範囲（これより前は、キーマトリクスのキー） */
#define TP_POS_FIRST POS_TP_FIRST
#define TP_POS_LAST POS_TP_LAST

BUILD_ASSERT(TP_POS_LAST < ZMK_KEYMAP_LEN, "trackpad positions are out of the keymap range");
/* 右のトラックパッドの位置か（右のクリック 3 つ、右のジェスチャー 5 つ。それぞれ続き番号であること） */
BUILD_ASSERT(POS_TP_MCLK_R == POS_TP_LCLK_R + 2 && POS_TP_PINCH_R == POS_TP_SWIPE_RIGHT_R + 4,
             "right trackpad positions must be consecutive");
static inline bool tp_position_is_right(uint32_t position) {
    return (position >= POS_TP_LCLK_R && position <= POS_TP_MCLK_R) ||
           (position >= POS_TP_SWIPE_RIGHT_R && position <= POS_TP_PINCH_R);
}
