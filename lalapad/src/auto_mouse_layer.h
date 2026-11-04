/*
 * オートマウスレイヤー（input_processor_auto_mouse_layer.c）を、レイヤーの固定（behavior_layer_lock.c）から使う。
 */

#pragma once

#include <stdbool.h>

#include <zmk/keymap.h>

/* layer の固定をやめた（システムワークキューから呼ぶ）。オートマウスレイヤーがそのレイヤーを有効にしていれば、
 * 時間を数え直して true を返す（レイヤーは解除しない）。そうでなければ何もせず false */
bool auto_mouse_layer_unlocked(zmk_keymap_layer_id_t layer);
