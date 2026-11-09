/*
 * 漢字入力 (&kanji_key) の漢字の番号。
 * このファイルは docs/tools/gen_kanji.py が docs/google-ime/kanji.tsv から作る。直接書き換えないこと。
 * keymap では `&kanji_key KJ_…` のように使う。どの番号がどの漢字になるかは、PC 側のローマ字テーブル
 * （docs/google-ime/romantable.txt）で決まる（キーボードは番号の符号「;」＋英字 2 文字を送るだけ）。
 */

#pragma once

/* 番号の上限（符号が英字 2 文字なので 26 × 26 - 1。0 は使わない） */
#define KJ_MAX_ID 675
