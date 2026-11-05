/*
 * レイヤーの固定（behavior_layer_lock.c）。
 */

#pragma once

#include <stdbool.h>

#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LAYER_LOCK)
/* layer を固定しているか（どのスレッドから呼んでもよい） */
bool layer_lock_is_locked(zmk_keymap_layer_id_t layer);
/* layer の固定をやめる（レイヤーは解除しない。この後に解除すれば、有効にし直されない）。
 * かなモードを抜けるときなど、ほかのモジュールがレイヤーを確実に解除したいときに呼ぶ */
void layer_lock_forget(zmk_keymap_layer_id_t layer);
#else
static inline bool layer_lock_is_locked(zmk_keymap_layer_id_t layer) { return false; }
static inline void layer_lock_forget(zmk_keymap_layer_id_t layer) {}
#endif
