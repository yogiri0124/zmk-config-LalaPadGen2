/*
 * 右（子機）とのつながりの状態（split_link.h）。
 *
 * 切断: 印を立て（split_link_input_lost が true）、切れた回数を増やす。
 * 接続: 右のトラックパッド（zmk,input-split で届く入力デバイス）の入力の列に、区切りの合図（INPUT_EV_MSC。値は
 *   接続・切断のたびに増える番号）を流す。入力の列は 1 本で順番どおりなので、合図が処理された時点で、それより前に
 *   並んでいた入力（切れる前に届いたもの）はすべて処理し終えている。合図の番号が今の番号と同じとき（その後に
 *   切れていないとき）だけ、印を消す。
 * 合図は、ZMK の入力のリスナー・自作の入力処理のどれも使わない種類（MSC）で、同期（sync）なしで流す。
 *
 * 接続・切断のコールバックはシステムワークキューで呼ばれる。印と番号は atomic で持つ（入力スレッドから読む）。
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "split_link.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if WATCH_SPLIT_LINK

/* 区切りの合図（入力の種類・コード）。値は epoch（0 にはならない） */
#define SPLIT_LINK_MARKER_TYPE INPUT_EV_MSC
#define SPLIT_LINK_MARKER_CODE INPUT_MSC_SCAN
/* 入力の列が満杯で合図を流せなかったときに、流し直すまでの時間 */
#define SPLIT_LINK_MARKER_RETRY_MS 10

/* つながっているか */
static atomic_t connected;
/* 切断から、合図が処理されるまで */
static atomic_t input_lost;
/* 切れた回数 */
static atomic_t losses;
/* 接続・切断のたびに増える番号（合図の値） */
static atomic_t epoch;

void split_link_mark_lost(void) {
    if (atomic_cas(&connected, 1, 0)) {
        atomic_set(&input_lost, 1);
        atomic_inc(&losses);
        atomic_inc(&epoch);
    }
}

bool split_link_input_lost(void) { return atomic_get(&input_lost) != 0; }

uint32_t split_link_losses(void) { return (uint32_t)atomic_get(&losses); }

#define SPLIT_LINK_HAS_INPUT                                                                       \
    (IS_ENABLED(CONFIG_ZMK_INPUT_SPLIT) && DT_HAS_COMPAT_STATUS_OKAY(zmk_input_split))

#if SPLIT_LINK_HAS_INPUT
/* 合図を流す入力デバイス（右のトラックパッドの 1 つ目。入力の列はすべてのデバイスで 1 本） */
#define SPLIT_LINK_INPUT_DEV DEVICE_DT_GET(DT_INST(0, zmk_input_split))

static void marker_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(marker_work, marker_work_cb);

/* 合図を流す（システムワークキュー）。流せなければ少し後に流し直す（その間に切れたら流さない） */
static void marker_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    if (!atomic_get(&connected)) {
        return;
    }
    const int ret = input_report(SPLIT_LINK_INPUT_DEV, SPLIT_LINK_MARKER_TYPE,
                                 SPLIT_LINK_MARKER_CODE, atomic_get(&epoch), false, K_NO_WAIT);
    if (ret < 0) {
        LOG_DBG("Split link: could not queue the input marker (%d), retrying", ret);
        k_work_reschedule(&marker_work, K_MSEC(SPLIT_LINK_MARKER_RETRY_MS));
    }
}

/* 合図が処理された（入力スレッド）: その後に切れていなければ、印を消す */
static void marker_input_cb(struct input_event *evt) {
    if (evt->type != SPLIT_LINK_MARKER_TYPE || evt->code != SPLIT_LINK_MARKER_CODE ||
        evt->value == 0) {
        return;
    }
    if (atomic_get(&connected) && evt->value == atomic_get(&epoch)) {
        atomic_set(&input_lost, 0);
        LOG_DBG("Split link: input marker %d processed", evt->value);
    }
}

INPUT_CALLBACK_DEFINE(SPLIT_LINK_INPUT_DEV, marker_input_cb);
#endif // SPLIT_LINK_HAS_INPUT

static void split_link_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0 || !is_split_link(conn) || !atomic_cas(&connected, 0, 1)) {
        return;
    }
    atomic_inc(&epoch);
#if SPLIT_LINK_HAS_INPUT
    k_work_reschedule(&marker_work, K_NO_WAIT);
#else
    // 右からの入力がない: 待つものはない
    atomic_set(&input_lost, 0);
#endif
}

static void split_link_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (is_split_link(conn)) {
        split_link_mark_lost();
    }
}

BT_CONN_CB_DEFINE(split_link_callbacks) = {
    .connected = split_link_connected,
    .disconnected = split_link_disconnected,
};

#endif // WATCH_SPLIT_LINK
