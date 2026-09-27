/*
 * カーソル低速の状態（behavior_cursor_slow.c が管理し、input_processor_cursor_slow.c が参照する）。
 */

#pragma once

#include <stdbool.h>

/* 低速がオンなら true（押している間・入れ切り・次のキーまで のどれかが有効） */
bool cursor_slow_is_active(void);
