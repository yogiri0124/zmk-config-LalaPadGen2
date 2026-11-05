/*
 * トラックパッドのタップ・ジェスチャーを割り当てるキーの位置（_L・_R は左右のトラックパッド）。
 * shield の lalapadgen2.dtsi（trackpad_key_behaviors_*）、keymap（オートマウスの excluded-positions など）、
 * 自作モジュールの C（lalapad/src/tp_positions.h）から読む（値はここだけに書く）。keymap の 52〜67 番目に書いたものが、それぞれの動作になる。
 * SWIPE_* は 3 本指で指を動かした向き（ドライバは右へのスワイプを INPUT_BTN_3、左へを BTN_4、
 * 上へを BTN_5、下へを BTN_6 で送る）。PINCH は 2 本指のピンチ中に押される修飾キー（ピンチは無効にしてある）。
 */

#pragma once

#define POS_TP_LCLK_L 52
#define POS_TP_RCLK_L 53
#define POS_TP_MCLK_L 54
#define POS_TP_LCLK_R 55
#define POS_TP_RCLK_R 56
#define POS_TP_MCLK_R 57
#define POS_TP_SWIPE_RIGHT_L 58
#define POS_TP_SWIPE_LEFT_L 59
#define POS_TP_SWIPE_UP_L 60
#define POS_TP_SWIPE_DOWN_L 61
#define POS_TP_PINCH_L 62
#define POS_TP_SWIPE_RIGHT_R 63
#define POS_TP_SWIPE_LEFT_R 64
#define POS_TP_SWIPE_UP_R 65
#define POS_TP_SWIPE_DOWN_R 66
#define POS_TP_PINCH_R 67

/* トラックパッド由来の位置の範囲（これより前は、キーマトリクスのキー） */
#define POS_TP_FIRST POS_TP_LCLK_L
#define POS_TP_LAST POS_TP_PINCH_R
