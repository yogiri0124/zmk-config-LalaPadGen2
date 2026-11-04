/*
 * トラックパッドをキー操作に変える behavior（zmk,behavior-tp-stroke）の strokes プロパティの値。
 *
 * 一筆書きの向きの並び（最大 4 つ）を 1 つの数にまとめる。1 つ目の向きを下の桁に置く（3 ビットずつ）。
 * 例: TP_STROKE2(TP_DOWN, TP_RIGHT) は「下へ動かしてから右へ」。
 */

#pragma once

#define TP_UP 1
#define TP_DOWN 2
#define TP_LEFT 3
#define TP_RIGHT 4

#define TP_STROKE1(a) (a)
#define TP_STROKE2(a, b) ((a) | ((b) << 3))
#define TP_STROKE3(a, b, c) ((a) | ((b) << 3) | ((c) << 6))
#define TP_STROKE4(a, b, c, d) ((a) | ((b) << 3) | ((c) << 6) | ((d) << 9))
