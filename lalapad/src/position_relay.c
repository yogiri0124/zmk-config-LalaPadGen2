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
 * この listener は、右が切れたときのトラックパッドの解放（split_trackpad_release.c。入力の列の順番で判断する
 * ため、入力スレッドで処理する必要がある）の次、ほかの自作モジュールより前に呼ばれるようにしている
 * （CMakeLists.txt の順番。listener はリンクの順に呼ばれる）。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 預けておける出来事の数（トラックパッドのタップ・ジェスチャーの押した／離したが、続けて届く分） */
#define RELAY_QUEUE_SIZE 32

K_MSGQ_DEFINE(relay_queue, sizeof(struct zmk_position_state_changed_event), RELAY_QUEUE_SIZE, 4);

/* 預かった出来事を、届いた順に、預かった listener の次から処理し直す（システムワークキュー） */
static void relay_work_handler(struct k_work *work) {
    struct zmk_position_state_changed_event ev;
    while (k_msgq_get(&relay_queue, &ev, K_NO_WAIT) == 0) {
        ZMK_EVENT_RELEASE(ev);
    }
}

static K_WORK_DEFINE(relay_work, relay_work_handler);

static int position_relay_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos == NULL || k_current_get() == k_work_queue_thread_get(&k_sys_work_q)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    struct zmk_position_state_changed_event ev = copy_raised_zmk_position_state_changed(pos);
    if (k_msgq_put(&relay_queue, &ev, K_NO_WAIT) != 0) {
        // 預けきれない（ふつうは起きない）: この場で処理する（押した／離したを落とさないことを優先する）
        LOG_WRN("Position relay queue full, processing position %d in place", pos->position);
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_work_submit(&relay_work);
    return ZMK_EV_EVENT_CAPTURED;
}

ZMK_LISTENER(position_relay, position_relay_listener);
ZMK_SUBSCRIPTION(position_relay, zmk_position_state_changed);
