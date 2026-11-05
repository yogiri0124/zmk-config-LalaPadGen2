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
 * 接続・切断のコールバックはシステムワークキュー、合図の処理は入力スレッドで呼ばれる。状態の書き換え（と、確かめてから
 * 書き換えるところ）は lock で一続きにする（合図の番号を確かめた直後に切れたとき、立ったばかりの印を消さないように）。
 * 印と回数は atomic でも持ち、読むだけなら lock を取らない。
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

/* 区切りの合図（入力の種類・コード）。値は epoch（接続のたびに増えるので 0 にはならない） */
#define SPLIT_LINK_MARKER_TYPE INPUT_EV_MSC
#define SPLIT_LINK_MARKER_CODE INPUT_MSC_SCAN
/* 入力の列が満杯で合図を流せなかったときに、流し直すまでの時間 */
#define SPLIT_LINK_MARKER_RETRY_MS 10

static struct k_spinlock lock;
/* つながっているか（lock の中で読み書きする） */
static bool connected;
/* 切断から、合図が処理されるまで */
static atomic_t input_lost;
/* 切れた回数 */
static atomic_t losses;
/* 接続・切断のたびに増える番号（合図の値。lock の中で読み書きする） */
static uint32_t epoch;

void split_link_mark_lost(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (connected) {
        connected = false;
        atomic_set(&input_lost, 1);
        atomic_inc(&losses);
        epoch++;
    }
    k_spin_unlock(&lock, key);
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
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool now_connected = connected;
    const uint32_t value = epoch;
    k_spin_unlock(&lock, key);
    if (!now_connected) {
        return;
    }
    // その後に切れたら、合図の値が今の番号と違うので、印は消えない
    const int ret = input_report(SPLIT_LINK_INPUT_DEV, SPLIT_LINK_MARKER_TYPE,
                                 SPLIT_LINK_MARKER_CODE, (int32_t)value, false, K_NO_WAIT);
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
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (connected && (uint32_t)evt->value == epoch) {
        atomic_set(&input_lost, 0);
    }
    k_spin_unlock(&lock, key);
}

INPUT_CALLBACK_DEFINE(SPLIT_LINK_INPUT_DEV, marker_input_cb);
#endif // SPLIT_LINK_HAS_INPUT

static void split_link_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0 || !is_split_link(conn)) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool changed = !connected;
    if (changed) {
        connected = true;
        epoch++;
#if !SPLIT_LINK_HAS_INPUT
        // 右からの入力がない: 待つものはない
        atomic_set(&input_lost, 0);
#endif
    }
    k_spin_unlock(&lock, key);
#if SPLIT_LINK_HAS_INPUT
    if (changed) {
        k_work_reschedule(&marker_work, K_NO_WAIT);
    }
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
