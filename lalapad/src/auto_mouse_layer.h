/*
 * オートマウスレイヤーの固定（input_processor_auto_mouse_layer.c が持ち、behavior_auto_mouse_lock.c が使う）。
 */

#pragma once

#include <zmk/keymap.h>

/* オートマウスレイヤーの固定を切り替える（システムワークキューから呼ぶ）。固定するときにレイヤーが
 * 有効でなければ、layer を有効にしてから固定する */
void auto_mouse_layer_toggle_lock(zmk_keymap_layer_id_t layer);
