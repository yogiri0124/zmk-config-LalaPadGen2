/*
 * キー位置の送信（key stream）の本体。
 *
 * キーが押された／離されたときに、その位置番号と押した／離したを、raw HID で PC に送る。
 * 右手側のキーも、親機（左）にはほかのキーと同じ zmk_position_state_changed として届くので、
 * 左右を区別せずにまとめて送る。どの文字が出たか・どのレイヤーかは送らない。
 *
 * 送るのは、PC のアプリから「開始」を受けている間だけ（USB でも Bluetooth でも、OS の HID の仕組みが
 * 常にデータを読むので、キーボード側からはアプリが開いているかどうかが分からないため）。
 * アプリは開いている間「開始」を送り続け（生存確認）、それが途切れたら送るのをやめる。
 * 送る先は、最後に「開始」が届いた送り道（USB、または Bluetooth のその接続）。
 *
 * レポートの形（32 バイト。足りない分は 0）:
 *   [0] 形式の版（KEY_STREAM_PROTOCOL_VERSION）  [1] 種類  [2..] 種類ごとの中身
 * キーボード → PC
 *   0x01 キー:  [2..3] 先頭の通し番号（u16 LE） [4] 件数 n（1〜4）
 *               [5 + 6*i] 位置  [6 + 6*i] 印（bit0: 押した、bit1: 開始したときに押されていたキー）
 *               [7 + 6*i .. 10 + 6*i] 時刻（起動してからのミリ秒、u32 LE）
 *               通し番号は1件ごとに1つ進む。飛んでいたら、その分を取りこぼしている
 *   0x02 状態:  [2] 右がつながっている（1/0） [3] 送っている種類（開始で指定したもの）
 *               [4] キーの位置の数（この番号より前がキー） [5] 位置の数の合計 [6..7] 次の通し番号（u16 LE）
 *   0x03 押されているキーの一覧:  [2..3] 通し番号（u16 LE。この番号より前のキーまでを反映した一覧）
 *               [4] 位置の数の合計  [5..] 押されている位置のビット（位置 p は [5 + p/8] の bit (p%8)。
 *               送っている種類の位置だけ。ほかは 0）
 *               キーの列の順番どおりに（それより前のキーを送り終えてから）送るので、通し番号は前のキーの続きになる。
 *               「開始」したとき、問い合わせに答えるとき、送り待ちがあふれてキーを捨てたときに送る
 * PC → キーボード
 *   0x81 開始・生存確認: [2] 送ってほしい種類（bit0: キー、bit1: トラックパッドのタップ・ジェスチャー）
 *   0x82 終了
 *   0x83 状態の問い合わせ（送っている間だけ答える。状態と、押されているキーの一覧を送る）
 * 知らない種類は無視する（あとで種類を足しても、古いアプリ・ファームウェアと組み合わせられる）。
 *
 * ふだんの入力に影響しないよう、キーの出来事は見るだけで止めない。送る処理と命令の処理は専用の
 * ワークキュー（スレッド）で行い、送るのは常に1つずつ（送り終わってから次。Bluetooth の送信の空きを
 * キーボードの文字の入力のほうに残すため）。送り切れないほどたまったら、古いものから捨てる。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/hid.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/matrix.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/events/split_peripheral_status_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
#include <zephyr/bluetooth/conn.h>
#endif

#include "key_stream.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEY_STREAM_PROTOCOL_VERSION 1

#define MSG_KEYS 0x01
#define MSG_STATUS 0x02
#define MSG_PRESSED 0x03
#define CMD_START 0x81
#define CMD_STOP 0x82
#define CMD_STATUS 0x83

#define STREAM_KEYS BIT(0)
#define STREAM_TRACKPAD BIT(1)
#define STREAM_ALL (STREAM_KEYS | STREAM_TRACKPAD)

#define FLAG_PRESSED BIT(0)
#define FLAG_SNAPSHOT BIT(1)

/* 1つのレポートに入れるキーの件数 */
#define KEYS_PER_REPORT 4
/* 送り待ちにためておけるキーの件数 */
#define EVENT_QUEUE_SIZE 64
/* 送り道が「今は送れない」と言ったときに、もう一度試すまでの時間 */
#define RETRY_MS 5

/* ---- レポートの記述（ベンダー定義。入力・出力とも 32 バイト、レポート ID なし） ---- */

const uint8_t key_stream_report_desc[] = {
    HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2),
    CONFIG_ZMK_KEY_STREAM_USAGE_PAGE & 0xFF,
    (CONFIG_ZMK_KEY_STREAM_USAGE_PAGE >> 8) & 0xFF,
    HID_USAGE(CONFIG_ZMK_KEY_STREAM_USAGE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_LOGICAL_MIN8(0x00),
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(KEY_STREAM_REPORT_SIZE),
    HID_USAGE(0x01),
    HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),
    HID_USAGE(0x02),
    HID_OUTPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),
    HID_END_COLLECTION,
};

const size_t key_stream_report_desc_size = sizeof(key_stream_report_desc);

BUILD_ASSERT(CONFIG_ZMK_KEY_STREAM_USAGE_PAGE >= 0xFF00 && CONFIG_ZMK_KEY_STREAM_USAGE_PAGE <= 0xFFFF,
             "usage page must be vendor defined (0xFF00-0xFFFF)");
BUILD_ASSERT(CONFIG_ZMK_KEY_STREAM_USAGE >= 0x01 && CONFIG_ZMK_KEY_STREAM_USAGE <= 0x7F,
             "usage must fit in one byte item");
BUILD_ASSERT(5 + 6 * KEYS_PER_REPORT <= KEY_STREAM_REPORT_SIZE, "keys report too large");
BUILD_ASSERT(ZMK_KEYMAP_LEN <= 255, "positions must fit in one byte");
BUILD_ASSERT(5 + (ZMK_KEYMAP_LEN + 7) / 8 <= KEY_STREAM_REPORT_SIZE, "pressed list report too large");

/* ---- 状態（lock で守る。キーの出来事はいろいろなスレッドから届くため） ---- */

struct key_event {
    uint16_t seq;
    uint8_t position;
    uint8_t flags;
    uint32_t time;
};

static struct k_spinlock lock;

/* 送っているか、送り先、送る種類 */
static bool streaming;
static enum key_stream_transport stream_transport;
static struct bt_conn *stream_conn; /* Bluetooth のときの接続（参照を持つ） */
static uint8_t stream_mask;
static int64_t last_keepalive;

/* 送り待ちのキー（輪状の待ち行列） */
static struct key_event events[EVENT_QUEUE_SIZE];
static size_t events_head;
static size_t events_count;
static uint16_t next_seq;

/* 状態のレポートを頼まれた回数と、送り始めた時点の回数（違えば送り待ち）。
 * 送っている最中に頼まれ直しても、その分を落とさないよう回数で持つ */
static uint8_t status_requested;
static uint8_t status_sent;
/* 押されているキーの一覧も同じく回数で持つ（送り待ちのキーを送り終えてから送る） */
static uint8_t pressed_list_requested;
static uint8_t pressed_list_sent;
/* 送っている最中のレポートがある（送り終わるまで次を送らない）。送った送り道と接続（参照を持つ）も覚えておき、
 * その送り道の「送り終わった」か「切れた」で解く（送る先が別の送り道に変わった後でも、前の送り道を待ち続けないように） */
static bool in_flight;
static enum key_stream_transport in_flight_transport;
static struct bt_conn *in_flight_conn;

/* いま押されている位置（開始したときや、押されているキーの一覧で知らせるため） */
static bool pressed[ZMK_KEYMAP_LEN];
/* 右がつながっているか */
static bool peripheral_connected;

/* ---- 命令（PC から届いたもの。写して、専用のワークキューで処理する） ---- */

struct command {
    enum key_stream_transport transport;
    struct bt_conn *conn; /* 参照を持つ */
    uint8_t data[4];
    uint8_t len;
};

K_MSGQ_DEFINE(command_queue, sizeof(struct command), 4, 4);

/* ---- 専用のワークキュー ---- */

K_THREAD_STACK_DEFINE(key_stream_stack, CONFIG_ZMK_KEY_STREAM_THREAD_STACK_SIZE);
static struct k_work_q key_stream_q;

static void send_work_handler(struct k_work *work);
static void command_work_handler(struct k_work *work);
static void timeout_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(send_work, send_work_handler);
static K_WORK_DEFINE(command_work, command_work_handler);
static K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_work_handler);

static void kick_send(k_timeout_t delay) { k_work_reschedule_for_queue(&key_stream_q, &send_work, delay); }

static bool position_wanted(uint8_t mask, uint32_t position) {
    const uint8_t kind =
        position < CONFIG_ZMK_KEY_STREAM_TRACKPAD_FIRST ? STREAM_KEYS : STREAM_TRACKPAD;
    return (mask & kind) != 0;
}

/* 送り待ちにキーを1件足す（あふれたら古いものを捨てる。通し番号は捨てた分も進んでいる）。lock の中で呼ぶ */
static void push_event(uint8_t position, uint8_t flags, uint32_t time) {
    if (events_count == EVENT_QUEUE_SIZE) {
        events_head = (events_head + 1) % EVENT_QUEUE_SIZE;
        events_count--;
        // 捨てた中に「離した」があると、アプリでは押されたままに見える。送り待ちを送り終えたら一覧で直してもらう
        pressed_list_requested++;
    }
    events[(events_head + events_count) % EVENT_QUEUE_SIZE] = (struct key_event){
        .seq = next_seq++,
        .position = position,
        .flags = flags,
        .time = time,
    };
    events_count++;
}

/* 送るのをやめる（lock の中で呼ぶ）。持っていた接続の参照は、戻り値で返して lock の外で手放す */
static struct bt_conn *stop_locked(void) {
    struct bt_conn *conn = stream_conn;
    streaming = false;
    stream_transport = KEY_STREAM_TRANSPORT_NONE;
    stream_conn = NULL;
    stream_mask = 0;
    events_count = 0;
    status_sent = status_requested;
    pressed_list_sent = pressed_list_requested;
    return conn;
}

static void release_conn(struct bt_conn *conn) {
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    if (conn != NULL) {
        bt_conn_unref(conn);
    }
#else
    ARG_UNUSED(conn);
#endif
}

/* 送っている最中のレポートが、その送り道（Bluetooth ならその接続）のものか。lock の中で呼ぶ */
static bool in_flight_on_locked(enum key_stream_transport transport, struct bt_conn *conn) {
    return in_flight && in_flight_transport == transport &&
           (transport != KEY_STREAM_TRANSPORT_BLE || in_flight_conn == conn);
}

/* 送っている最中の印を外す（lock の中で呼ぶ）。持っていた接続の参照は、戻り値で返して lock の外で手放す */
static struct bt_conn *finish_in_flight_locked(void) {
    struct bt_conn *conn = in_flight_conn;
    in_flight = false;
    in_flight_transport = KEY_STREAM_TRANSPORT_NONE;
    in_flight_conn = NULL;
    return conn;
}

static void stop(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct bt_conn *conn = stop_locked();
    k_spin_unlock(&lock, key);
    release_conn(conn);
    k_work_cancel_delayable(&timeout_work);
    LOG_DBG("Key stream stopped");
}

/* ---- 送る（専用のワークキュー） ---- */

static void build_status(uint8_t *report) {
    report[1] = MSG_STATUS;
    report[2] = peripheral_connected ? 1 : 0;
    report[3] = stream_mask;
    report[4] = MIN(CONFIG_ZMK_KEY_STREAM_TRACKPAD_FIRST, ZMK_KEYMAP_LEN);
    report[5] = ZMK_KEYMAP_LEN;
    // 次に届くキーの通し番号（送り待ちがあれば、その先頭。状態は送り待ちのキーより先に送るため）
    sys_put_le16(events_count > 0 ? events[events_head].seq : next_seq, &report[6]);
}

/* 押されているキーの一覧のレポートを作る。送り待ちのキーがない（すべて送り始めた）ときに呼ぶ */
static void build_pressed_list(uint8_t *report) {
    report[1] = MSG_PRESSED;
    sys_put_le16(next_seq, &report[2]);
    report[4] = ZMK_KEYMAP_LEN;
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
        if (pressed[pos] && position_wanted(stream_mask, pos)) {
            report[5 + pos / 8] |= BIT(pos % 8);
        }
    }
}

/* キーのレポートを作る。入れた件数を返す */
static size_t build_keys(uint8_t *report) {
    const size_t n = MIN(events_count, KEYS_PER_REPORT);
    report[1] = MSG_KEYS;
    sys_put_le16(events[events_head].seq, &report[2]);
    report[4] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) {
        const struct key_event *e = &events[(events_head + i) % EVENT_QUEUE_SIZE];
        uint8_t *p = &report[5 + 6 * i];
        p[0] = e->position;
        p[1] = e->flags;
        sys_put_le32(e->time, &p[2]);
    }
    return n;
}

static void send_work_handler(struct k_work *work) {
    static uint8_t report[KEY_STREAM_REPORT_SIZE];

    k_spinlock_key_t key = k_spin_lock(&lock);
    if (!streaming || in_flight) {
        k_spin_unlock(&lock, key);
        return;
    }
    memset(report, 0, sizeof(report));
    report[0] = KEY_STREAM_PROTOCOL_VERSION;
    // 送る順: 状態 → 送り待ちのキー → 押されているキーの一覧（一覧は、それより前のキーを送り終えた時点のもの）
    uint8_t kind;
    uint8_t gen = 0;
    uint16_t first_seq = 0;
    size_t n = 0;
    if (status_requested != status_sent) {
        build_status(report);
        kind = MSG_STATUS;
        gen = status_requested;
    } else if (events_count > 0) {
        first_seq = events[events_head].seq;
        n = build_keys(report);
        kind = MSG_KEYS;
    } else if (pressed_list_requested != pressed_list_sent) {
        build_pressed_list(report);
        kind = MSG_PRESSED;
        gen = pressed_list_requested;
    } else {
        k_spin_unlock(&lock, key);
        return;
    }
    const enum key_stream_transport transport = stream_transport;
    struct bt_conn *conn = NULL;
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    // 送っている間に接続が切れて本体の参照が手放されても使えるよう、送り終わるまで自分の参照を持つ
    if (stream_conn != NULL) {
        conn = bt_conn_ref(stream_conn);
    }
#endif
    in_flight = true;
    in_flight_transport = transport;
    in_flight_conn = conn;
    k_spin_unlock(&lock, key);

    int err = -ENOTSUP;
    if (transport == KEY_STREAM_TRANSPORT_USB) {
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_USB)
        err = key_stream_usb_send(report);
#endif
    } else if (transport == KEY_STREAM_TRANSPORT_BLE) {
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
        err = key_stream_ble_send(conn, report);
#endif
    }

    key = k_spin_lock(&lock);
    if (err == 0) {
        // 送り始めた分を取り除く（送っている間にあふれて先頭が進んでいたら、その分は数えない）
        if (kind == MSG_STATUS) {
            status_sent = gen;
        } else if (kind == MSG_PRESSED) {
            pressed_list_sent = gen;
        } else {
            while (events_count > 0 && (int16_t)(events[events_head].seq - first_seq) < (int16_t)n) {
                events_head = (events_head + 1) % EVENT_QUEUE_SIZE;
                events_count--;
            }
        }
        k_spin_unlock(&lock, key);
        return;
    }
    // 送れなかった: 送っている最中の印を外す（送っている間に送り道が切れて、もう外れていることもある）
    struct bt_conn *done = NULL;
    if (in_flight_on_locked(transport, conn)) {
        done = finish_in_flight_locked();
    }
    k_spin_unlock(&lock, key);
    release_conn(done);

    if (err == -EAGAIN) {
        kick_send(K_MSEC(RETRY_MS));
    } else {
        LOG_WRN("Key stream send failed (%d), stopping", err);
        stop();
    }
}

void key_stream_sent(enum key_stream_transport transport, struct bt_conn *conn) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool finished = in_flight_on_locked(transport, conn);
    struct bt_conn *done = finished ? finish_in_flight_locked() : NULL;
    k_spin_unlock(&lock, key);
    release_conn(done);
    if (finished) {
        kick_send(K_NO_WAIT);
    }
}

/* ---- 命令（専用のワークキュー） ---- */

static void start(enum key_stream_transport transport, struct bt_conn *conn, uint8_t mask) {
    struct bt_conn *old_conn = NULL;
    const int64_t now = k_uptime_get();

    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool fresh = !streaming || stream_transport != transport || stream_conn != conn ||
                       stream_mask != mask;
    if (fresh) {
        old_conn = stop_locked();
        streaming = true;
        stream_transport = transport;
        stream_conn = conn; // 参照は命令から引き継ぐ
        conn = NULL;
        stream_mask = mask;
        status_requested++;
        // 押されたままのキーを知らせる（アプリが途中から見ても、押しっぱなしの扱いを合わせられるように）。
        // 1件ずつの「押した」（印の bit1）と、その後に押されているキーの一覧の両方を送る
        for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
            if (pressed[pos] && position_wanted(mask, pos)) {
                push_event((uint8_t)pos, FLAG_PRESSED | FLAG_SNAPSHOT, (uint32_t)now);
            }
        }
        pressed_list_requested++;
        LOG_DBG("Key stream started (transport %d, mask 0x%02x)", transport, mask);
    }
    last_keepalive = now;
    k_spin_unlock(&lock, key);

    release_conn(old_conn);
    release_conn(conn); // 生存確認だけのとき: 命令の参照は要らない
    k_work_reschedule_for_queue(&key_stream_q, &timeout_work,
                                K_MSEC(CONFIG_ZMK_KEY_STREAM_KEEPALIVE_TIMEOUT_MS));
    if (fresh) {
        kick_send(K_NO_WAIT);
    }
}

static void command_work_handler(struct k_work *work) {
    struct command cmd;
    while (k_msgq_get(&command_queue, &cmd, K_NO_WAIT) == 0) {
        if (cmd.len < 2 || cmd.data[0] != KEY_STREAM_PROTOCOL_VERSION) {
            release_conn(cmd.conn);
            continue;
        }
        switch (cmd.data[1]) {
        case CMD_START: {
            const uint8_t mask = (cmd.len >= 3 ? cmd.data[2] : STREAM_KEYS) & STREAM_ALL;
            start(cmd.transport, cmd.conn, mask);
            break;
        }
        case CMD_STOP: {
            k_spinlock_key_t key = k_spin_lock(&lock);
            const bool same = streaming && stream_transport == cmd.transport && stream_conn == cmd.conn;
            k_spin_unlock(&lock, key);
            release_conn(cmd.conn);
            if (same) {
                stop();
            }
            break;
        }
        case CMD_STATUS: {
            k_spinlock_key_t key = k_spin_lock(&lock);
            if (streaming && stream_transport == cmd.transport && stream_conn == cmd.conn) {
                status_requested++;
                pressed_list_requested++;
            }
            k_spin_unlock(&lock, key);
            release_conn(cmd.conn);
            kick_send(K_NO_WAIT);
            break;
        }
        default:
            release_conn(cmd.conn);
            break;
        }
    }
}

static void timeout_work_handler(struct k_work *work) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool active = streaming;
    const int64_t remaining =
        last_keepalive + CONFIG_ZMK_KEY_STREAM_KEEPALIVE_TIMEOUT_MS - k_uptime_get();
    k_spin_unlock(&lock, key);
    if (!active) {
        return;
    }
    if (remaining > 0) {
        k_work_reschedule_for_queue(&key_stream_q, &timeout_work, K_MSEC(remaining));
        return;
    }
    LOG_DBG("Key stream keepalive timed out");
    stop();
}

void key_stream_received(enum key_stream_transport transport, struct bt_conn *conn,
                         const uint8_t *data, size_t len) {
    struct command cmd = {
        .transport = transport,
        .len = (uint8_t)MIN(len, sizeof(cmd.data)),
    };
    memcpy(cmd.data, data, cmd.len);
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    if (conn != NULL) {
        cmd.conn = bt_conn_ref(conn);
    }
#endif
    if (k_msgq_put(&command_queue, &cmd, K_NO_WAIT) != 0) {
        // 命令がたまりすぎ（ふつうは起きない。生存確認は2秒ごとなので、次で取り戻せる）
        release_conn(cmd.conn);
        return;
    }
    k_work_submit_to_queue(&key_stream_q, &command_work);
}

void key_stream_transport_lost(enum key_stream_transport transport, struct bt_conn *conn) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct bt_conn *old_conn = NULL;
    const bool same = streaming && stream_transport == transport &&
                      (transport != KEY_STREAM_TRANSPORT_BLE || stream_conn == conn);
    if (same) {
        old_conn = stop_locked();
    }
    // 切れた送り道で送っている最中だったら、その「送り終わった」はもう届かないので待つのをやめる
    // （送る先がすでに別の送り道に変わっていれば、そちらで次を送れるようになる）
    const bool was_in_flight = in_flight_on_locked(transport, conn);
    struct bt_conn *done = was_in_flight ? finish_in_flight_locked() : NULL;
    k_spin_unlock(&lock, key);
    release_conn(old_conn);
    release_conn(done);
    if (was_in_flight) {
        kick_send(K_NO_WAIT);
    }
}

/* ---- キーの出来事・右のつながり（見るだけで、止めない） ---- */

static int key_stream_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        if (pos->position >= ZMK_KEYMAP_LEN) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        k_spinlock_key_t key = k_spin_lock(&lock);
        pressed[pos->position] = pos->state;
        const bool queued = streaming && position_wanted(stream_mask, pos->position);
        if (queued) {
            push_event((uint8_t)pos->position, pos->state ? FLAG_PRESSED : 0,
                       (uint32_t)pos->timestamp);
        }
        k_spin_unlock(&lock, key);
        if (queued) {
            kick_send(K_NO_WAIT);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    const struct zmk_split_peripheral_status_changed *split =
        as_zmk_split_peripheral_status_changed(eh);
    if (split != NULL) {
        k_spinlock_key_t key = k_spin_lock(&lock);
        peripheral_connected = split->connected;
        const bool send = streaming;
        if (send) {
            status_requested++;
        }
        k_spin_unlock(&lock, key);
        if (send) {
            kick_send(K_NO_WAIT);
        }
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_stream, key_stream_listener);
ZMK_SUBSCRIPTION(key_stream, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
ZMK_SUBSCRIPTION(key_stream, zmk_split_peripheral_status_changed);
#endif

static int key_stream_init(void) {
    static const struct k_work_queue_config config = {.name = "key_stream"};
    k_work_queue_start(&key_stream_q, key_stream_stack, K_THREAD_STACK_SIZEOF(key_stream_stack),
                       CONFIG_ZMK_KEY_STREAM_THREAD_PRIORITY, &config);
    return 0;
}

SYS_INIT(key_stream_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
