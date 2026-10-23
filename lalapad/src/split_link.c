/*
 * 親機（左）から見た、右（子機）とのつながりを見て、変わったら zmk_lalapad_split_link_changed を出す。
 *
 * 親機は、右とは自分が central 側（接続を始める側）になって Bluetooth でつなぐ。PC とは peripheral 側でつなぐので、
 * 接続の役割で右との接続だけを見分けられる。つながった・切れたは Bluetooth のスレッドから届くので、
 * 数だけ数えて、出来事はシステムワークキューで出す（ほかの出来事と同じスレッドで受け取れるように）。
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "split_link.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(zmk_lalapad_split_link_changed);

/* 右との接続の数、つながった回数、切れた回数、最後に切れたときのつながった回数（Bluetooth のスレッドで数える） */
static atomic_t link_count;
static atomic_t connect_count;
static atomic_t disconnect_count;
static atomic_t ended_connections;
/* 最後に出来事で知らせた状態と、そのときまでに知らせた切れた回数（システムワークキューでだけ触る） */
static bool reported;
static atomic_val_t reported_disconnects;

bool lalapad_split_link_connected(void) { return atomic_get(&link_count) > 0; }

uint32_t lalapad_split_link_connections(void) { return (uint32_t)atomic_get(&connect_count); }

static void report(bool connected) {
    reported = connected;
    LOG_DBG("Split link %s", connected ? "connected" : "disconnected");
    raise_zmk_lalapad_split_link_changed((struct zmk_lalapad_split_link_changed){
        .connected = connected,
        .ended_connections = (uint32_t)atomic_get(&ended_connections),
    });
}

static void notify_work_handler(struct k_work *work) {
    // ワークが動く前に「切れた → つながった」と続いても、切れたことは必ず知らせる
    // （切れる前に押されていた位置を離すなど、切れたことをきっかけにする処理があるため）
    const atomic_val_t disconnects = atomic_get(&disconnect_count);
    if (disconnects != reported_disconnects) {
        reported_disconnects = disconnects;
        report(false); // 知らせていた状態が「切れている」でも、もう一度知らせる（受け取る側は重なっても困らない）
    }
    const bool connected = lalapad_split_link_connected();
    if (connected != reported) {
        report(connected);
    }
}

static K_WORK_DEFINE(notify_work, notify_work_handler);

/* 右との接続か（親機が central 側の接続） */
static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0 || !is_split_link(conn)) {
        return;
    }
    atomic_inc(&connect_count);
    atomic_inc(&link_count);
    k_work_submit(&notify_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (!is_split_link(conn)) {
        return;
    }
    // ここまでにつながった接続は、もう切れている（この後につなぎ直した接続と区別するため、回数を残す）
    atomic_set(&ended_connections, atomic_get(&connect_count));
    // 数え始める前からの接続の「切れた」で負にならないように
    atomic_val_t count = atomic_get(&link_count);
    while (count > 0 && !atomic_cas(&link_count, count, count - 1)) {
        count = atomic_get(&link_count);
    }
    atomic_inc(&disconnect_count);
    k_work_submit(&notify_work);
}

BT_CONN_CB_DEFINE(lalapad_split_link_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};
