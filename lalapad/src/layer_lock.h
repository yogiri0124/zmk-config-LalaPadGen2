/*
 * レイヤーの固定（behavior_layer_lock.c）。
 */

#pragma once

#include <stdbool.h>

#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LAYER_LOCK)
/* layer を固定しているか（どのスレッドから呼んでもよい） */
bool layer_lock_is_locked(zmk_keymap_layer_id_t layer);
#else
static inline bool layer_lock_is_locked(zmk_keymap_layer_id_t layer) { return false; }
#endif
