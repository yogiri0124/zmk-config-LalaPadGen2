/*
 * 右（子機）とのつながりを見る（自作モジュールの中で共通）。
 * 親機は、右とは自分が central 側（接続を始める側）、PC とは peripheral 側でつなぐので、接続の役割で見分ける。
 * ZMK v0.3.0 の親機は、右とのつながりの変化を出来事として出さない（zmk_split_peripheral_status_changed は
 * 子機の側でだけ出る）ため、使う側が BT_CONN_CB_DEFINE で接続・切断の知らせを受け取る。
 */

#pragma once

#include <stdbool.h>

#include <zephyr/sys/util_macro.h>

/* 右とのつながりを見るか（Bluetooth で分割している親機） */
#define WATCH_SPLIT_LINK (IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#if WATCH_SPLIT_LINK
#include <zephyr/bluetooth/conn.h>

/* conn が右（子機）との接続か */
static inline bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}
#endif
