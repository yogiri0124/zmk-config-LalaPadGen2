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
/* 16 個の位置が、重ならずに TP_POS_FIRST〜TP_POS_LAST を埋めていること（範囲で「トラックパッド由来か」を見るため） */
#define TP_POS_BIT(p) (((p) >= TP_POS_FIRST && (p) <= TP_POS_LAST) ? (1u << ((p) - TP_POS_FIRST)) : 0u)
BUILD_ASSERT(TP_POS_LAST - TP_POS_FIRST + 1 == 16 &&
                 (TP_POS_BIT(POS_TP_LCLK_L) + TP_POS_BIT(POS_TP_RCLK_L) + TP_POS_BIT(POS_TP_MCLK_L) +
                  TP_POS_BIT(POS_TP_LCLK_R) + TP_POS_BIT(POS_TP_RCLK_R) + TP_POS_BIT(POS_TP_MCLK_R) +
                  TP_POS_BIT(POS_TP_SWIPE_RIGHT_L) + TP_POS_BIT(POS_TP_SWIPE_LEFT_L) +
                  TP_POS_BIT(POS_TP_SWIPE_UP_L) + TP_POS_BIT(POS_TP_SWIPE_DOWN_L) +
                  TP_POS_BIT(POS_TP_PINCH_L) + TP_POS_BIT(POS_TP_SWIPE_RIGHT_R) +
                  TP_POS_BIT(POS_TP_SWIPE_LEFT_R) + TP_POS_BIT(POS_TP_SWIPE_UP_R) +
                  TP_POS_BIT(POS_TP_SWIPE_DOWN_R) + TP_POS_BIT(POS_TP_PINCH_R)) == 0xFFFFu,
             "trackpad positions must fill TP_POS_FIRST..TP_POS_LAST without overlap");
/* tp_position_is_right は範囲で見るので、右のクリック・右のジェスチャーがそれぞれ続き番号であること */
BUILD_ASSERT(POS_TP_RCLK_R == POS_TP_LCLK_R + 1 && POS_TP_MCLK_R == POS_TP_LCLK_R + 2 &&
                 POS_TP_SWIPE_LEFT_R == POS_TP_SWIPE_RIGHT_R + 1 &&
                 POS_TP_SWIPE_UP_R == POS_TP_SWIPE_RIGHT_R + 2 &&
                 POS_TP_SWIPE_DOWN_R == POS_TP_SWIPE_RIGHT_R + 3 &&
                 POS_TP_PINCH_R == POS_TP_SWIPE_RIGHT_R + 4,
             "right trackpad click and gesture positions must each be consecutive");

/* 右のトラックパッドの位置か（右のクリック、右のジェスチャー） */
static inline bool tp_position_is_right(uint32_t position) {
    return (position >= POS_TP_LCLK_R && position <= POS_TP_MCLK_R) ||
           (position >= POS_TP_SWIPE_RIGHT_R && position <= POS_TP_PINCH_R);
}
