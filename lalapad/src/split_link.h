/*
 * 親機（左）から見た、右（子機）とのつながり（split_link.c）。
 *
 * ZMK v0.3.0 の zmk_split_peripheral_status_changed は子機の側でだけ出る出来事で、親機では出ない。
 * 親機の分割の処理も、右とのつながりが変わったことを出来事として出さない。そのため親機では、
 * 右との Bluetooth の接続（親機が central 側になる接続）を自分で見て、この出来事を出す。
 */

#pragma once

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_lalapad_split_link_changed {
    /* 右とつながっている（1台以上） */
    bool connected;
};

ZMK_EVENT_DECLARE(zmk_lalapad_split_link_changed);

/* 今、右とつながっているか */
bool lalapad_split_link_connected(void);
