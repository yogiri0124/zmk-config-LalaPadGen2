/*
 * 打っている間だけ続くレイヤー（behavior_word_layer.c。Num Word）を、ほかのモジュールから終える。
 */

#pragma once

#include <zephyr/sys/util_macro.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_WORD_LAYER)
/* 有効な Num Word などをすべて終えて、そのレイヤーを解除する（システムワークキューから呼ぶ） */
void word_layer_finish_all(void);
#else
static inline void word_layer_finish_all(void) {}
#endif
