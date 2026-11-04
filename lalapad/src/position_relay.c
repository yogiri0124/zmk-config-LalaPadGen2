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
 * この listener は、ほかの自作モジュールより前に呼ばれるようにしている（CMakeLists.txt でこのファイルを最初に足す。
 * listener はリンクの順に呼ばれる）。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 預けておける出来事の数（トラックパッドのタップ・ジェスチャーの押した／離したが、続けて届く分）。
 * システムワークキューが長く止まらない限り、満杯にはならない */
#define RELAY_QUEUE_SIZE 64
/* 満杯のときに、空くのを待つ時間 */
#define RELAY_PUT_TIMEOUT_MS 50

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
    // 満杯なら、空くのを少しだけ待つ（入力スレッドを止めるので長くは待たない。K_FOREVER にすると、システム
    // ワークキューが入力イベントの列の空きを待っているときに、互いに待ち合って止まる）
    if (k_msgq_put(&relay_queue, &ev, K_MSEC(RELAY_PUT_TIMEOUT_MS)) != 0) {
        // それでも預けきれない（システムワークキューが長く止まったとき）: この場で処理する。まだ預けてある
        // 出来事より先に処理されるので、順番が入れ替わることがある（同じ位置の「離した」が「押した」より先に
        // 処理されると、押されたままになる）。落とせば確実に押されたまま・離されたままになるので、こちらを選ぶ。
        // この場で処理すると、キーマップと behavior が入力スレッドで動くので、上に書いた問題が残る（その位置の
        // behavior が入力イベントの列の空きを K_FOREVER で待つと、入力スレッドが自分の列を待って止まりうる）。
        // 預ける列が満杯のまま 50 ms 空かない（システムワークキューが長く止まった）ときだけで、止まるのはさらに
        // 入力イベントの列も満杯のときなので、まれな場合の妥協とする
        LOG_WRN("Position relay queue full, processing position %d in place", pos->position);
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_work_submit(&relay_work);
    return ZMK_EV_EVENT_CAPTURED;
}

ZMK_LISTENER(position_relay, position_relay_listener);
ZMK_SUBSCRIPTION(position_relay, zmk_position_state_changed);
