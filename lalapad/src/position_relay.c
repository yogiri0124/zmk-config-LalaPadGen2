/*
 * システムワークキュー以外のスレッドから出たキー位置の出来事を、システムワークキューで処理し直す。
 *
 * トラックパッドのタップ・ジェスチャーは、入力スレッドの中で入力処理（zmk,input-processor-behaviors-consume の
 * &tp_to_pos）がキー位置の「押した／離した」として出す。そのままだと、キーマップと、その位置に割り当てた
 * behavior まで入力スレッドで動く。次の問題があるので、ここで出来事を預かり、システムワークキューで続きを処理する。
 * - &mkp などは入力イベントの列へ K_FOREVER で並べるので、入力スレッドの中で動くと、列が満杯のときに
 *   入力スレッドが自分の列の空きを待って止まる。
 * - かな入力・Alt コードなどの自作の behavior は、状態をシステムワークキューだけで触る前提で作っている
 *   （その位置に Studio などで割り当てると、別のスレッドから同時に触ることになる）。
 * 預かった出来事は、届いた順のまま、預かった listener の次から処理を続ける（ZMK_EVENT_RELEASE）。
 * キーマトリクスと右（子機）からのキーの出来事は、もともとシステムワークキューで出るので、そのまま通す。
 *
 * 右（子機）との接続が切れたら、右のトラックパッドの位置（tp_positions.h）で押されたままのものの「離した」を出す。
 * 右のキーは ZMK が離すが、右のトラックパッドは入力の出来事として届いて親機で位置に変えるので、ZMK は離さない
 * （ドラッグ中に切れると、クリックが押されたままになる）。切れている間に処理する右のトラックパッドの「押した」
 * （切れる前に入力の列や、この中継の列に残っていた分）は捨てる（離した後に押されたままにならないように）。
 * 押されていない位置の「離した」（離した後に届いた本物の「離した」、捨てた「押した」の「離した」）も捨てる
 * （ZMK は押されていない位置の「離した」でも binding を離すので、同じボタンを押している別の位置のクリックが
 * 離れてしまうため）。
 *
 * この listener は、ほかの自作モジュールより前に呼ばれるようにしている（CMakeLists.txt でこのファイルを最初に足す。
 * listener はリンクの順に呼ばれる）。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>

#include "tp_positions.h"

#define WATCH_SPLIT_LINK (IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#if WATCH_SPLIT_LINK
#include <zephyr/bluetooth/conn.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 預けておける出来事の数（トラックパッドのタップ・ジェスチャーの押した／離したが、続けて届く分）。
 * システムワークキューが長く止まらない限り、満杯にはならない */
#define RELAY_QUEUE_SIZE 64
/* 満杯のときに、空くのを待つ時間 */
#define RELAY_PUT_TIMEOUT_MS 50

K_MSGQ_DEFINE(relay_queue, sizeof(struct zmk_position_state_changed_event), RELAY_QUEUE_SIZE, 4);

/* 預かって処理した出来事で、いま押されている位置（右との接続が切れたときに離す分と、押されていない位置の
 * 「離した」を知る） */
static ATOMIC_DEFINE(relayed_pressed, ZMK_KEYMAP_LEN);
/* 列に預けきれなかった「離した」の位置（relay_work_handler が、列の分を処理した後に離す） */
static ATOMIC_DEFINE(deferred_release, ZMK_KEYMAP_LEN);
#if WATCH_SPLIT_LINK
/* 右との接続が切れているか */
static atomic_t split_lost;
#endif

/* position が押されていたら、「離した」を出す（システムワークキューで呼ぶ。出した出来事はこの listener を素通りする） */
static void release_if_pressed(uint32_t position) {
    if (!atomic_test_and_clear_bit(relayed_pressed, position)) {
        return;
    }
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        .state = false,
        .position = position,
        .timestamp = k_uptime_get(),
    });
}

/* 預かった出来事を、届いた順に、預かった listener の次から処理し直す（システムワークキュー）。
 * 列に預けきれなかった「離した」は、列の分（その位置の「押した」を含む）を処理した後に出す */
static void relay_work_handler(struct k_work *work) {
    struct zmk_position_state_changed_event ev;
    while (k_msgq_get(&relay_queue, &ev, K_NO_WAIT) == 0) {
        const uint32_t position = ev.data.position;
        if (position < ZMK_KEYMAP_LEN) {
            if (ev.data.state) {
#if WATCH_SPLIT_LINK
                // 切れた後に処理する右のトラックパッドの「押した」（切れる前に並んでいた分）。切れたときの処理と
                // 同じシステムワークキューで調べるので、取りこぼさない
                if (atomic_get(&split_lost) && tp_position_is_right(position)) {
                    LOG_DBG("Split link lost, dropped press of right trackpad position %u", position);
                    continue;
                }
#endif
                atomic_set_bit(relayed_pressed, position);
            } else if (!atomic_test_and_clear_bit(relayed_pressed, position)) {
                LOG_DBG("Dropped release of position %u that is not pressed", position);
                continue;
            }
        }
        ZMK_EVENT_RELEASE(ev);
    }
    for (uint32_t position = 0; position < ZMK_KEYMAP_LEN; position++) {
        // 離す処理の中で behavior が止まっている間に、列に新しく並んだ分があれば、先にそれを処理する（並べた側が
        // このワークを予約し直している。その位置の「押した」がまだ列にあるうちに、後で出す「離した」を使い切らない）
        if (k_msgq_num_used_get(&relay_queue) != 0) {
            break;
        }
        if (atomic_test_and_clear_bit(deferred_release, position)) {
            LOG_DBG("Releasing deferred position %u", position);
            release_if_pressed(position);
        }
    }
}

static K_WORK_DEFINE(relay_work, relay_work_handler);

static int position_relay_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos == NULL || k_current_get() == k_work_queue_thread_get(&k_sys_work_q)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
#if WATCH_SPLIT_LINK
    // 早めに捨てる分（同じことは relay_work_handler でも調べる）
    if (pos->state && atomic_get(&split_lost) && tp_position_is_right(pos->position)) {
        LOG_DBG("Split link lost, dropped press of right trackpad position %u", pos->position);
        return ZMK_EV_EVENT_HANDLED;
    }
#endif
    // 後で出す「離した」が残っている位置の「押した」は捨てる（列の分より後に、その「離した」を出すので、
    // 後から並べた「押した」を、すぐに離してしまわないように。捨てた「押した」の「離した」は、押されていない
    // 位置のものとして捨てられる）
    if (pos->state && pos->position < ZMK_KEYMAP_LEN &&
        atomic_test_bit(deferred_release, pos->position)) {
        LOG_WRN("Position relay: release of position %u still deferred, dropped press",
                pos->position);
        return ZMK_EV_EVENT_HANDLED;
    }
    struct zmk_position_state_changed_event ev = copy_raised_zmk_position_state_changed(pos);
    // 満杯なら、空くのを少しだけ待つ（入力スレッドを止めるので長くは待たない。K_FOREVER にすると、システム
    // ワークキューが入力イベントの列の空きを待っているときに、互いに待ち合って止まる）
    if (k_msgq_put(&relay_queue, &ev, K_MSEC(RELAY_PUT_TIMEOUT_MS)) != 0) {
        // それでも預けきれない（システムワークキューが長く止まったとき）。システムワークキューが止まる主な原因は、
        // behavior（&mkp など）が入力イベントの列の空きを K_FOREVER で待っていることで、&mkp は押すときも離すときも
        // 待つので、ここで入力スレッドのまま処理すると、その behavior が自分の列を待って止まる。そのため、この場では
        // 処理しない:
        // - 「押した」は捨てる（押されたままにはならない。後から届く「離した」は、押されていない位置のものとして捨てる）
        // - 「離した」は印だけ付けて、relay_work_handler が、列の分（その位置の「押した」を含む）を処理した後に出す
        //   （捨てると押されたままになるため。押した後に離す順番も守られる。印が残っている間に届いたその位置の
        //   「押した」は捨てる。ほかの位置の出来事との前後は、列の分の後にずれることがある）
        if (pos->state) {
            LOG_WRN("Position relay queue full, dropped press of position %u", pos->position);
        } else if (pos->position < ZMK_KEYMAP_LEN) {
            LOG_WRN("Position relay queue full, deferring release of position %u", pos->position);
            atomic_set_bit(deferred_release, pos->position);
            k_work_submit(&relay_work);
        }
        return ZMK_EV_EVENT_HANDLED;
    }
    k_work_submit(&relay_work);
    return ZMK_EV_EVENT_CAPTURED;
}

ZMK_LISTENER(position_relay, position_relay_listener);
ZMK_SUBSCRIPTION(position_relay, zmk_position_state_changed);

#if WATCH_SPLIT_LINK
/* 親機は、右とは自分が central 側（接続を始める側）、PC とは peripheral 側でつなぐので、接続の役割で見分ける */
static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

static void relay_split_connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0 && is_split_link(conn)) {
        atomic_set(&split_lost, 0);
    }
}

/* 右のトラックパッドの位置で押されたままのものを離す（システムワークキュー。出した「離した」はこの listener を
 * 素通りし、預かった出来事の処理（relay_work_handler）とも同じスレッドなので、順番が入れ替わらない） */
static void split_release_work_handler(struct k_work *work) {
    for (uint32_t position = TP_POS_FIRST; position <= TP_POS_LAST; position++) {
        if (tp_position_is_right(position)) {
            release_if_pressed(position);
        }
    }
}

static K_WORK_DEFINE(split_release_work, split_release_work_handler);

/* 右との接続が切れた（Zephyr 3.5 ではシステムワークキューから呼ばれるが、それに頼らず、離す処理はワークで行う） */
static void relay_split_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (!is_split_link(conn)) {
        return;
    }
    atomic_set(&split_lost, 1);
    k_work_submit(&split_release_work);
}

BT_CONN_CB_DEFINE(position_relay_split_callbacks) = {
    .connected = relay_split_connected,
    .disconnected = relay_split_disconnected,
};
#endif
