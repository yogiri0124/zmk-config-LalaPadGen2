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
 *
 * 離すのは、切れた接続の間に押された位置だけ。押したときに「それまでに右とつながった回数」を覚えておき、
 * 切れたという出来事の ended_connections 以下のものを離す（出来事が届く前につなぎ直して押した位置は、離さない）。
 * 切れる前に右から届いていて、切れた後に処理された押下も、つなぎ直す前ならこれで離せる。
 * 右とつながっていないときに処理された押下（切れたという出来事より後に処理された、古い接続の押下）は、
 * 捨てる（ほかに伝えない）。後から離すと、その間につなぎ直して押した新しい押下まで離してしまうため。
 * 捨てた押下をほかのモジュールの listener が数えないよう、この listener は自作モジュールの中で最初に呼ばれる
 * ようにしている（CMakeLists.txt でこのファイルを最初に足す。listener はリンクの順に呼ばれる）。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>

#include "split_link.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// DT のマクロではラベルを小文字で書く（lalapadgen2.dtsi の trackpad_key_behaviors_R）
#define RIGHT_TRACKPAD_NODE DT_NODELABEL(trackpad_key_behaviors_r)

#define RIGHT_TRACKPAD_POSITION(node, prop, idx) DT_PHA_BY_IDX(node, prop, idx, param1),

static const uint32_t right_trackpad_positions[] = {
    DT_FOREACH_PROP_ELEM(RIGHT_TRACKPAD_NODE, bindings, RIGHT_TRACKPAD_POSITION)};

/* 位置ごとの状態（right_trackpad_positions と同じ順番。入力スレッドとシステムワークキューの両方から触るので atomic）。
 * 押されていれば「押したときまでに右とつながった回数」（1 以上）、押されていなければ 0 */
static atomic_t pressed_at[ARRAY_SIZE(right_trackpad_positions)];

BUILD_ASSERT(ARRAY_SIZE(right_trackpad_positions) <= 32, "too many right trackpad positions");

/* 右のトラックパッドの位置なら、その順番。違えば -1 */
static int right_trackpad_index(uint32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        if (right_trackpad_positions[i] == position) {
            return (int)i;
        }
    }
    return -1;
}

static void release(size_t i) {
    LOG_DBG("Right half disconnected, releasing trackpad position %d", right_trackpad_positions[i]);
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        .state = false,
        .position = right_trackpad_positions[i],
        .timestamp = k_uptime_get(),
    });
}

/* 切れた: もう切れた接続（ended_connections 回目まで）の間に押されていた位置を離す（システムワークキュー） */
static void release_pressed_until(uint32_t ended_connections) {
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        const atomic_val_t at = atomic_get(&pressed_at[i]);
        if (at != 0 && (uint32_t)at <= ended_connections && atomic_cas(&pressed_at[i], at, 0)) {
            release(i);
        }
    }
}



static int split_trackpad_release_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        const int idx = right_trackpad_index(pos->position);
        if (idx >= 0) {
            if (pos->state && !lalapad_split_link_connected()) {
                // 右とつながっていないのに届いた押下（古い接続のもの）: 捨てる
                LOG_DBG("Dropped trackpad position %d pressed after the right half disconnected",
                        pos->position);
                return ZMK_EV_EVENT_HANDLED;
            }
            atomic_set(&pressed_at[idx], pos->state ? (atomic_val_t)lalapad_split_link_connections() : 0);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_lalapad_split_link_changed *split = as_zmk_lalapad_split_link_changed(eh);
    if (split != NULL && !split->connected) {
        release_pressed_until(split->ended_connections);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_trackpad_release, split_trackpad_release_listener);
ZMK_SUBSCRIPTION(split_trackpad_release, zmk_position_state_changed);
ZMK_SUBSCRIPTION(split_trackpad_release, zmk_lalapad_split_link_changed);
