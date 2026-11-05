/*
 * 右（子機）とのつながりを見る（自作モジュールの中で共通。split_link.c）。
 * 親機は、右とは自分が central 側（接続を始める側）、PC とは peripheral 側でつなぐので、接続の役割で見分ける。
 * ZMK v0.3.0 の親機は、右とのつながりの変化を出来事として出さない（zmk_split_peripheral_status_changed は
 * 子機の側でだけ出る）ため、使う側が BT_CONN_CB_DEFINE で接続・切断の知らせを受け取る。
 *
 * 切れる前に右から届いていた入力（右のトラックパッドの動き・触れた・タップなど）は、入力の列や中継の列に残っていて、
 * 切れた後や、つながり直した後に処理されることがある。それを新しい入力として扱わないために、次の 2 つを使う:
 * - split_link_input_lost(): 入力スレッドで使う。切断から、つながり直した後に入力の列へ流した区切りの合図が
 *   処理されるまで true（入力の列は 1 本で順番どおりなので、合図より前の入力は、すべて切れる前に届いたもの）。
 * - split_link_losses(): 切断のたびに増える数。入力スレッドで出来事を預かるときに覚えておき、システムワーク
 *   キューで処理するときに今の数と比べれば、その後に切れたか（切れる前に届いたものか）がわかる。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

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

/* 右との接続が切れた。各モジュールの切断のコールバックの最初で呼ぶ（コールバックの呼ばれる順番に頼らないため。
 * 1 回の切断で何度呼んでもよい） */
void split_link_mark_lost(void);

/* 入力スレッドで使う: 右との接続が切れてから、つながり直した後の区切りの合図が処理されるまで true */
bool split_link_input_lost(void);

/* 右との接続が切れた回数 */
uint32_t split_link_losses(void);
#endif
