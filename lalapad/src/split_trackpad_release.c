/*
 * 右（子機）が切れたときに、右のトラックパッドのタップ・ジェスチャーで押されたままの位置を離す。
 *
 * トラックパッドのタップ・ジェスチャーは、親機の入力処理（zmk,input-processor-behaviors-consume の
 * &tp_to_pos）がキー位置の「押した／離した」として出す。右のトラックパッドの入力は子機から中継されるが、
 * ZMK v0.3.0 は子機が切れたときにキー（キーマトリクス）の位置は離すものの、中継中の入力は購読をやめるだけで
 * 「離した」を出さない。そのため、右のトラックパッドを押したまま（ドラッグ中など）切れると、その位置が
 * 押されたままになる（マウスボタンが押されたまま、オートマウスレイヤーが時間切れにならない、など）。
 * ここで、その位置の「離した」を出して、ほかのキーと同じように元に戻す。
 *
 * 対象の位置は、右のトラックパッドの入力処理（lalapadgen2.dtsi の trackpad_key_behaviors_R）の
 * bindings の引数（&tp_to_pos の位置番号）から、ビルド時に取り出す。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define RIGHT_TRACKPAD_NODE DT_NODELABEL(trackpad_key_behaviors_R)

#define RIGHT_TRACKPAD_POSITION(node, prop, idx) DT_PHA_BY_IDX(node, prop, idx, param1),

static const uint32_t right_trackpad_positions[] = {
    DT_FOREACH_PROP_ELEM(RIGHT_TRACKPAD_NODE, bindings, RIGHT_TRACKPAD_POSITION)};

/* 押されている位置（入力スレッドとシステムワークキューの両方から触るので atomic） */
static ATOMIC_DEFINE(pressed, ZMK_KEYMAP_LEN);

static bool is_right_trackpad_position(uint32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        if (right_trackpad_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static void release_work_handler(struct k_work *work) {
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        const uint32_t position = right_trackpad_positions[i];
        if (position < ZMK_KEYMAP_LEN && atomic_test_and_clear_bit(pressed, position)) {
            LOG_DBG("Right half disconnected, releasing trackpad position %d", position);
            raise_zmk_position_state_changed((struct zmk_position_state_changed){
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                .state = false,
                .position = position,
                .timestamp = k_uptime_get(),
            });
        }
    }
}

static K_WORK_DEFINE(release_work, release_work_handler);

static int split_trackpad_release_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        if (pos->position < ZMK_KEYMAP_LEN && is_right_trackpad_position(pos->position)) {
            atomic_set_bit_to(pressed, pos->position, pos->state);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_split_peripheral_status_changed *split =
        as_zmk_split_peripheral_status_changed(eh);
    if (split != NULL && !split->connected) {
        // 「離した」はワークで出す（切れたことの通知の処理の中から、さらに出来事を出さないように）
        k_work_submit(&release_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_trackpad_release, split_trackpad_release_listener);
ZMK_SUBSCRIPTION(split_trackpad_release, zmk_position_state_changed);
ZMK_SUBSCRIPTION(split_trackpad_release, zmk_split_peripheral_status_changed);
