/*
 * スクロールスナップのオン・オフの状態（behavior_scroll_snap_hold.c が管理し、
 * input_processor_scroll_snap_gate.c が参照する）。
 */

#pragma once

#include <stdbool.h>

/* スナップがオンなら true（&scr_snap_hold のキーを押している間） */
bool scroll_snap_gate_is_active(void);
