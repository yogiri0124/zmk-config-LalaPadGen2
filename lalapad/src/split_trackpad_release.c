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
 * 順番を崩さないため、すべて入力スレッドの1つの列で処理する:
 * - 中継された右の入力は、ZMK が受け取った順にシステムワークキューで入力イベントの列に並べ
 *   （split/bluetooth/central.c の peripheral_event_work）、入力スレッドでキー位置の「押した／離した」になる。
 * - 右とつながった・切れたら、合図も同じくシステムワークキューから同じ列の末尾に並べる（Bluetooth のスレッドから
 *   直接並べると、ZMK がまだ列に並べていない右の入力を追い越してしまう）。右の入力は、届いた接続の合図の後ろ、
 *   次の合図の前に並ぶので、列の順番で「どの接続の入力か」が分かる（合図を受け取るたびに数える）。
 * - 「切れた」の合図を受け取ったら、押されている右のトラックパッドの位置を離す。それより後に処理される、
 *   切れた接続の押下は捨てる（ほかに伝えない）。押していない位置の「離した」も捨てる（ZMK が数える押した数が
 *   合わなくならないように）。
 * 解放の「離した」は、合図を受け取った入力スレッドの中で出す（別のスレッドに回すと、上の順番が崩れる）。
 * 出した「離した」は、この listener で記録を合わせた後、次の position_relay.c が預かり、キーマップと behavior の
 * 処理はシステムワークキューで行われる（入力スレッドの中で &mkp などが同じ入力の列の空きを待たないように）。
 * 捨てた入力をほかのモジュールの listener が数えないよう、この listener は自作モジュールの中で最初に呼ばれる
 * ようにしている（CMakeLists.txt でこのファイルを最初に足す。listener はリンクの順に呼ばれる）。
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// DT のマクロではラベルを小文字で書く（lalapadgen2.dtsi の trackpad_key_behaviors_R）
#define RIGHT_TRACKPAD_NODE DT_NODELABEL(trackpad_key_behaviors_r)

#define RIGHT_TRACKPAD_POSITION(node, prop, idx) DT_PHA_BY_IDX(node, prop, idx, param1),

static const uint32_t right_trackpad_positions[] = {
    DT_FOREACH_PROP_ELEM(RIGHT_TRACKPAD_NODE, bindings, RIGHT_TRACKPAD_POSITION)};

/* ---- 入力スレッドでだけ触る状態 ---- */

/* 列の中で、いまの入力がどの接続のものか（「つながった」の合図を受け取った回数） */
static uint32_t input_connection;
/* 位置ごとに、押されていれば押したときの input_connection（1 以上）、押されていなければ 0
 * （right_trackpad_positions と同じ順番） */
static uint32_t pressed_at[ARRAY_SIZE(right_trackpad_positions)];
/* 「切れた」の合図を受け取った接続の回数（この回数までの接続の押下は、もう受け付けない） */
static uint32_t ended_connections;

/* 右のトラックパッドの位置なら、その順番。違えば -1 */
static int right_trackpad_index(uint32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        if (right_trackpad_positions[i] == position) {
            return (int)i;
        }
    }
    return -1;
}

static int split_trackpad_release_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    const int idx = right_trackpad_index(pos->position);
    if (idx < 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (pos->state) {
        const uint32_t connections = input_connection;
        if (connections <= ended_connections) {
            // 切れた接続の押下（「切れた」の合図より後に処理された）: 捨てる
            LOG_DBG("Dropped trackpad position %d pressed on a disconnected link", pos->position);
            return ZMK_EV_EVENT_HANDLED;
        }
        pressed_at[idx] = connections;
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (pressed_at[idx] == 0) {
        // 押していない（離し済み・捨てた押下の）「離した」: 捨てる
        return ZMK_EV_EVENT_HANDLED;
    }
    pressed_at[idx] = 0;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_trackpad_release, split_trackpad_release_listener);
ZMK_SUBSCRIPTION(split_trackpad_release, zmk_position_state_changed);

/* ---- 「つながった」「切れた」の合図（入力イベントの列に並べて、入力スレッドで受け取る） ---- */

/* 合図を並べるためだけの入力デバイス（DT を使わない） */
DEVICE_DEFINE(lalapad_split_release_input, "lalapad_split_release", NULL, NULL, NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

#define SPLIT_RELEASE_INPUT_DEV DEVICE_GET(lalapad_split_release_input)

/* 合図の種類（INPUT_EV_MSC の code）。value は、その接続が何回目の接続か */
#define SIGNAL_CONNECTED 0
#define SIGNAL_DISCONNECTED 1

/* 合図を受け取った（入力スレッド） */
static void split_release_callback(struct input_event *evt) {
    if (evt->type != INPUT_EV_MSC) {
        return;
    }
    if (evt->code == SIGNAL_CONNECTED) {
        // この後に並んでいる右の入力は、この接続のもの
        input_connection = (uint32_t)evt->value;
        return;
    }
    // 切れた: value 回目までの接続の間に押されていた位置を離す
    const uint32_t ended = (uint32_t)evt->value;
    if (ended > ended_connections) {
        ended_connections = ended;
    }
    for (size_t i = 0; i < ARRAY_SIZE(right_trackpad_positions); i++) {
        if (pressed_at[i] == 0 || pressed_at[i] > ended) {
            continue;
        }
        // 出した「離した」は、上の listener で押されている扱いの位置なので通り、pressed_at も 0 に戻る
        LOG_DBG("Right half disconnected, releasing trackpad position %d", right_trackpad_positions[i]);
        raise_zmk_position_state_changed((struct zmk_position_state_changed){
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
            .state = false,
            .position = right_trackpad_positions[i],
            .timestamp = k_uptime_get(),
        });
    }
}

INPUT_CALLBACK_DEFINE(SPLIT_RELEASE_INPUT_DEV, split_release_callback);

/* 右とつながった回数（Bluetooth のスレッドで数える） */
static atomic_t connect_count;

struct signal {
    uint16_t code;
    uint32_t connection;
};

/* 合図の待ち行列（Bluetooth のスレッドで並べ、システムワークキューで入力イベントの列へ移す） */
K_MSGQ_DEFINE(signal_queue, sizeof(struct signal), 8, 4);

static void signal_work_handler(struct k_work *work) {
    struct signal sig;
    while (k_msgq_get(&signal_queue, &sig, K_NO_WAIT) == 0) {
        // 列が満杯のときは待つ（合図を落とすと、どの接続の入力か分からなくなるため。入力スレッドは
        // この列の空きを待つことがないので、順に取り出されてすぐに空く）
        input_report(SPLIT_RELEASE_INPUT_DEV, INPUT_EV_MSC, sig.code,
                     (int32_t)(sig.connection & INT32_MAX), true, K_FOREVER);
    }
}

static K_WORK_DEFINE(signal_work, signal_work_handler);

/* 合図を並べる（Bluetooth のスレッド）。システムワークキューで、それより前に届いた右の入力の後ろに並ぶ */
static void queue_signal(uint16_t code, uint32_t connection) {
    const struct signal sig = {.code = code, .connection = connection};
    if (k_msgq_put(&signal_queue, &sig, K_NO_WAIT) != 0) {
        // つながる・切れるが 8 回続けて処理されないことはない（念のため記録を残す）
        LOG_ERR("Split release signal queue full, dropped signal %d", code);
        return;
    }
    k_work_submit(&signal_work);
}

static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0 || !is_split_link(conn)) {
        return;
    }
    queue_signal(SIGNAL_CONNECTED, (uint32_t)atomic_inc(&connect_count) + 1);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (!is_split_link(conn)) {
        return;
    }
    // ここまでにつながった接続は、すべて切れている
    queue_signal(SIGNAL_DISCONNECTED, (uint32_t)atomic_get(&connect_count));
}

BT_CONN_CB_DEFINE(split_trackpad_release_conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};
