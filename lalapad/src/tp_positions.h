/*
 * トラックパッドのクリック・ジェスチャーのキー位置（自作モジュールの中で使う分）。
 * config/boards/shields/lalapadgen2/lalapadgen2.dtsi の POS_TP_* と合わせること
 * （52〜54 左のクリック、55〜57 右のクリック、58〜62 左のジェスチャー、63〜67 右のジェスチャー）。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk/matrix.h>

/* トラックパッド由来の位置の範囲（これより前は、キーマトリクスのキー） */
#define TP_POS_FIRST 52
#define TP_POS_LAST 67

BUILD_ASSERT(TP_POS_LAST < ZMK_KEYMAP_LEN, "trackpad positions are out of the keymap range");

/* 右のトラックパッドの位置か（右のクリック 55〜57、右のジェスチャー 63〜67） */
static inline bool tp_position_is_right(uint32_t position) {
    return (position >= 55 && position <= 57) || (position >= 63 && position <= 67);
}
