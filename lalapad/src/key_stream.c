/*
 * キー位置の送信（key stream）の本体。
 *
 * キーが押された／離されたときに、その位置番号と押した／離したを、raw HID で PC に送る。
 * 右手側のキーも、親機（左）にはほかのキーと同じ zmk_position_state_changed として届くので、
 * 左右を区別せずにまとめて送る。どの文字が出たかは送らない。頼まれたら、今有効なレイヤーも送る
 * （アプリが今のレイヤーを出せるように。キーの押し離しからは、トラックパッドで入るオートマウスレイヤーなどが分からないため）。
 *
 * 送るのは、PC のアプリから「開始」を受けている間だけ（USB でも Bluetooth でも、OS の HID の仕組みが
 * 常にデータを読むので、キーボード側からはアプリが開いているかどうかが分からないため）。
 * アプリは開いている間「開始」を送り続け（生存確認）、それが途切れたら送るのをやめる。
 * 送る先は、最後に「開始」が届いた送り道（USB、または Bluetooth のその接続）。
 * ただし、「開始」は ZMK がいまキーの入力を送っている先（zmk_endpoints_selected()。Bluetooth なら、
 * 選んでいるプロファイルの接続）から届いたものだけを受け付け、ほかの PC からのものは黙って無視する（答えない）。
 * 入力先が変わったら（USB ⇔ Bluetooth の切り替え、プロファイルの切り替え）、送っている先が入力先でなくなった
 * 時点で送るのをやめる（別の PC で打った内容が、ほかの PC のアプリに見えないように）。入力先に戻れば、
 * アプリが送り続けている「開始」で、始め直しとしてまた始まる。
 *
 * レポートの形（32 バイト。足りない分は 0）:
 *   [0] 形式の版（KEY_STREAM_PROTOCOL_VERSION）  [1] 種類  [2..] 種類ごとの中身
 * キーボード → PC
 *   0x01 キー:  [2..3] 先頭の通し番号（u16 LE） [4] 件数 n（1〜4）
 *               [5 + 6*i] 位置  [6 + 6*i] 印（bit0: 押した、bit1: 開始したときに押されていたキー）
 *               [7 + 6*i .. 10 + 6*i] 時刻（起動してからのミリ秒、u32 LE）
 *               通し番号は1件ごとに1つ進む。飛んでいたら、その分を取りこぼしている
 *   0x02 状態:  [2] 右との Bluetooth の接続がある（1/0。右からキーが届く準備ができているとは限らない）
 *               [3] 送っている種類（開始で指定したもの）
 *               [4] キーの位置の数（この番号より前がキー） [5] 位置の数の合計 [6..7] 次の通し番号（u16 LE）
 *               [8] できること（bit0: 書き込み待ちの命令 0x84・0x85 を受け付ける）
 *               「開始」したとき、問い合わせに答えるとき、右とのつながりが変わったとき、
 *               送り終わったの知らせが来ない状態のレポートを見切ったときに送る
 *   0x03 押されているキーの一覧:  [2..3] 通し番号（u16 LE。この番号より前のキーまでを反映した一覧）
 *               [4] 位置の数の合計  [5..] 押されている位置のビット（位置 p は [5 + p/8] の bit (p%8)。
 *               送っている種類の位置だけ。ほかは 0）
 *               キーの列の順番どおりに（それより前のキーを送り終えてから）送るので、通し番号は前のキーの続きになる。
 *               「開始」したとき、問い合わせに答えるとき、送り待ちがあふれてキーを捨てたとき、
 *               送り終わったの知らせが来ないキー・一覧のレポートを見切ったときに送る
 *   0x04 レイヤー:  [2..5] 有効なレイヤー（u32 LE。レイヤー ID ごとのビット。Default と条件付きレイヤーを含む）
 *               [6] レイヤーの数  [7..10] 作った時刻（起動してからのミリ秒、u32 LE）
 *               [11] 押している修飾キー（HID の修飾キーのビット。bit0 左 Ctrl、bit1 左 Shift、bit2 左 Alt、bit3 左 GUI、
 *               bit4〜7 右の同じ順。修飾キーそのものを押しているもの（&kp LSHIFT、長押しの Shift、&sk など）だけ。
 *               LS(N1) のようにキーに付いた修飾キーは含まない）
 *               開始で bit2 を頼んだときだけ。「開始」したとき、問い合わせに答えるとき、レイヤー・修飾キーが変わったとき、
 *               送り終わったの知らせが来ないまま見切ったときに送る（そのときの状態。続けて変わったら最後のものだけ）
 * PC → キーボード
 *   0x81 開始・生存確認: [2] 送ってほしい種類（bit0: キー、bit1: トラックパッドのタップ・ジェスチャー、bit2: レイヤー）
 *   0x82 終了
 *   0x83 状態の問い合わせ（送っている間だけ答える。状態と、押されているキーの一覧（レイヤーを頼んでいればレイヤーも）を送る）
 *   0x84 書き込み待ちの準備: [2] どちら（0: 親機（左）、1: 子機（右））。3 秒の間だけ、次の 0x85 を受け付ける
 *   0x85 書き込み待ちにする: [2] どちら（0x84 と同じであること）。親機は自分で、子機には親機から &bootloader を押したことにして伝える
 *   （どちらも送っている間（入力先からの「開始」を受けている間）だけ。答えは 0x05）
 * キーボード → PC（命令の答え）
 *   0x05 書き込み待ちの答え: [2] どちら [3] 結果（0: 書き込み待ちにする（この答えを送り終えてから 0.3 秒後に入る）、1: 準備した、
 *               2: 準備していない・時間切れ・どちらが違う、3: 子機とつながっていない、4: できない（&bootloader がない））
 * 知らない種類は無視する（あとで種類を足しても、古いアプリ・ファームウェアと組み合わせられる）。
 *
 * ふだんの入力に影響しないよう、キーの出来事は見るだけで止めない。送る処理と命令の処理は専用の
 * ワークキュー（スレッド）で行い、送るのは常に1つずつ（送り終わってから次。Bluetooth の送信の空きを
 * キーボードの文字の入力のほうに残すため）。送り切れないほどたまったら、古いものから捨てる。
 * 送り終わったの知らせが来ないレポートは、時間で見切って次へ進む（押されているキーの一覧を送り直す）。
 * 送れない状態が続いたら、送るのをやめる（アプリの次の「開始」でまた始まる）。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/hid.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#include "split_link.h"

#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE) || WATCH_SPLIT_LINK
#include <zephyr/bluetooth/conn.h>
#endif

#include "key_stream.h"
#include "tp_positions.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* PC のアプリとの取り決め（docs/key-stream.md） */
#define KEY_STREAM_PROTOCOL_VERSION 1
#define USAGE_PAGE 0xFF4C /* ベンダー定義 */
#define USAGE 0x01
/* 「開始」（生存確認。アプリは2秒ごとに送る）がこの時間届かなければ、送るのをやめる */
#define KEEPALIVE_TIMEOUT_MS 6000

#define MSG_KEYS 0x01
#define MSG_STATUS 0x02
#define MSG_PRESSED 0x03
#define MSG_LAYERS 0x04
#define MSG_BOOT 0x05
#define CMD_START 0x81
#define CMD_STOP 0x82
#define CMD_STATUS 0x83
#define CMD_BOOT_ARM 0x84
#define CMD_BOOT_RUN 0x85

/* 書き込み待ち（0x84・0x85）。キーマップに &bootloader があるときだけ（子機にも同じキーマップが入るので、子機でも動く） */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(bootloader), okay)
#define KS_HAS_BOOT 1
#else
#define KS_HAS_BOOT 0
#endif
#define BOOT_TARGET_CENTRAL 0
#define BOOT_TARGET_PERIPHERAL 1
#define BOOT_OK 0
#define BOOT_ARMED 1
#define BOOT_NOT_ARMED 2
#define BOOT_NO_PERIPHERAL 3
#define BOOT_UNSUPPORTED 4
/* 準備（0x84）から実行（0x85）までに待つ時間。うっかり 1 つの命令で入らないように、2 つそろったときだけ入る */
#define BOOT_ARM_MS 3000
/* 答えを送ってから再起動するまでの時間（答えが PC に届くように） */
#define BOOT_DELAY_MS 300
#define CAP_BOOT BIT(0)
#define STREAM_KEYS BIT(0)
#define STREAM_TRACKPAD BIT(1)
#define STREAM_LAYERS BIT(2)
#define STREAM_ALL (STREAM_KEYS | STREAM_TRACKPAD | STREAM_LAYERS)

#define FLAG_PRESSED BIT(0)
#define FLAG_SNAPSHOT BIT(1)

/* 1つのレポートに入れるキーの件数 */
#define KEYS_PER_REPORT 4
/* 送り待ちにためておけるキーの件数 */
#define EVENT_QUEUE_SIZE 64
/* 送り道が「今は送れない」（-EAGAIN）と言ったときに、もう一度試すまでの時間 */
#define RETRY_MS 5
/* 送り道が「しばらく送れない」（-EBUSY。USB のサスペンド中・前の転送が終わっていないときなど）と
 * 言ったときに、もう一度試すまでの時間（Bluetooth の MTU が小さい・暗号化前も -EBUSY にしてあるが、32 バイトの
 * 「開始」は MTU が 35 以上の暗号化した接続でしか届かないので、念のための備え） */
#define RETRY_SLOW_MS 100
/* 送れない状態がこの時間続いたら、送るのをやめる（試し直し続けないように。アプリの次の「開始」でまた始まる） */
#define SEND_GIVE_UP_MS 5000
/* 送り終わったの知らせをこの時間待っても来なければ、そのレポートを見切って次へ進む
 * （USB のリセットなどで転送が取り消されると、知らせが来ないことがある）。Bluetooth は接続の間隔や
 * 送信待ちの分だけ遅れることがあるので長めにする */
#define IN_FLIGHT_TIMEOUT_USB_MS 500
#define IN_FLIGHT_TIMEOUT_BLE_MS 3000
/* 送る処理と命令の処理をする専用のワークキュー（スレッド） */
/* キーを押している間・修飾キーが残っている間に、修飾キーを見直す間隔（ZMK の hold-tap が預かって出し直した修飾キーの
 * 押し離しは、こちらの listener に届かないことがあるため） */
#define MODS_WATCH_MS 20
#define THREAD_STACK_SIZE 1536
#define THREAD_PRIORITY 6

/* ---- レポートの記述（ベンダー定義。入力・出力とも 32 バイト、レポート ID なし） ---- */

const uint8_t key_stream_report_desc[] = {
    HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2),
    USAGE_PAGE & 0xFF,
    (USAGE_PAGE >> 8) & 0xFF,
    HID_USAGE(USAGE),
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

BUILD_ASSERT(5 + 6 * KEYS_PER_REPORT <= KEY_STREAM_REPORT_SIZE, "keys report too large");
BUILD_ASSERT(ZMK_KEYMAP_LEN <= 255, "positions must fit in one byte");
BUILD_ASSERT(5 + (ZMK_KEYMAP_LEN + 7) / 8 <= KEY_STREAM_REPORT_SIZE, "pressed list report too large");
BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= 32, "layers must fit in 32 bits");

/* ---- 状態（lock で守る。キーの出来事・PC からの命令・送り終わりの知らせ・右とのつながりの変化は、
 *      それぞれ別のスレッドから届くため） ---- */

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
/* 最後に「開始」（生存確認）が届いた時刻（専用のワークキューだけで触る） */
static int64_t last_keepalive;
/* 送れない（-EAGAIN・-EBUSY）が続いているか、続き始めた時刻（送れたら、または送るのをやめたら外す） */
static bool retrying;
static int64_t retry_since;
/* 入力先が変わったの知らせを受けた回数と、照合を始めた時点の回数（違えば照合待ち）。照合待ちの間は送らない
 * （入力先が変わった後に押したキー、つまり別の PC で打ったキーを、照合の前に前の PC へ送らないように。
 *  キーは送り待ちにためておき、入力先のままなら照合の後に送り、入力先でなくなっていれば捨てる） */
static uint32_t route_requested;
static uint32_t route_checked;
/* 照合の前に、入力先が実際に変わった（zmk_endpoint_changed）か。変わったら、照合の時点で前の入力先に
 * 戻っていても、送っている分はやめる（A → B → A と切り替える間に B で打ったキーを、A へ送らないように） */
static bool route_left;

/* 送り待ちのキー（輪状の待ち行列） */
static struct key_event events[EVENT_QUEUE_SIZE];
static size_t events_head;
static size_t events_count;
static uint16_t next_seq;

/* 状態のレポートを頼まれた回数と、送り始めた時点の回数（違えば送り待ち）。
 * 送っている最中に頼まれ直しても、その分を落とさないよう回数で持つ */
static uint32_t status_requested;
static uint32_t status_sent;
/* 押されているキーの一覧も同じく回数で持つ（送り待ちのキーを送り終えてから送る） */
static uint32_t pressed_list_requested;
static uint32_t pressed_list_sent;
/* 有効なレイヤーも同じく回数で持つ（送るときの状態を送る。続けて変わっても、送るのは最後の状態の1つだけ） */
static uint32_t layers_requested;
static uint32_t layers_sent;
/* 最後に送った（または送るよう頼んだ）修飾キー。見直したときに、これと違えばレイヤーの報告を送る */
static uint8_t mods_seen;
/* 書き込み待ちの答え（送り待ちの回数と、送る中身）。準備（0x84）は、どちらと時刻を覚える（専用のワークキューだけで触る） */
static uint32_t boot_reply_requested;
static uint32_t boot_reply_sent;
static uint8_t boot_reply_target;
static uint8_t boot_reply_result;
static int boot_armed_target = -1;
static int64_t boot_armed_at;
static uint32_t boot_armed_session;
/* 送り先の世代（始め直す・やめるたびに 1 つ進む。lock で守る）。準備（0x84）はこの世代に結びつけ、世代が変われば使えない */
static uint32_t session_gen;
/* 書き込み待ちにする答え（結果 0）を送り待ち・送っている最中か（lock で守る）。送り終わったの知らせが来てから入る */
static bool boot_go_pending;
static bool in_flight_boot_go;
static void boot_go(void);
/* 送っている最中のレポートがある（送り終わるまで次を送らない）。送るたびに番号を付け、「送り終わった」は
 * その番号で照合する（送る先を変えた後や見切った後に、遅れて届いた「送り終わった」と取り違えないように）。
 * 送った送り道と接続も覚えておき、その送り道が切れたら待つのをやめる。接続は見分ける（ポインタを比べる）
 * だけで、参照は持たず、中身にも触らない（送るのをやめた後などは、参照を誰も持たないまま残ることがある）。
 * 接続が切れると、その接続の場所が別の接続に使われる前に、切れたの知らせ（key_stream_transport_lost）で
 * 外すので、別の接続と取り違えることはない */
static bool in_flight;
static uint32_t in_flight_id;
static uint8_t in_flight_kind;
static uint32_t last_send_id;
static enum key_stream_transport in_flight_transport;
static struct bt_conn *in_flight_conn;
/* 送り終わったの知らせを待つ時間を計っているレポートの番号と、見切る時刻（専用のワークキューだけで触る） */
static uint32_t flight_watch_id;
static int64_t flight_deadline;

/* いま押されている位置（開始したときや、押されているキーの一覧で知らせるため） */
static bool pressed[ZMK_KEYMAP_LEN];
/* 位置ごとに、最後に受け取った出来事の時刻（出し直された古い出来事を見分ける） */
static int64_t last_event_ts[ZMK_KEYMAP_LEN];

/* 右（子機）との接続の数（Bluetooth の接続・切断の通知で数える。分割していなければ 0 のまま） */
static atomic_t split_links;

/* ---- 命令（PC から届いたもの。写して、専用のワークキューで処理する） ---- */

struct command {
    enum key_stream_transport transport;
    struct bt_conn *conn; /* 参照を持つ */
    uint8_t data[4];
    uint8_t len;
};

K_MSGQ_DEFINE(command_queue, sizeof(struct command), 4, 4);

/* ---- 専用のワークキュー ---- */

static K_THREAD_STACK_DEFINE(key_stream_stack, THREAD_STACK_SIZE);
static struct k_work_q key_stream_q;

static void send_work_handler(struct k_work *work);
static void command_work_handler(struct k_work *work);
static void timeout_work_handler(struct k_work *work);
static void flight_work_handler(struct k_work *work);
static void route_work_handler(struct k_work *work);
static void mods_work_handler(struct k_work *work);
static void watch_mods(void);
static K_WORK_DELAYABLE_DEFINE(send_work, send_work_handler);
static K_WORK_DEFINE(command_work, command_work_handler);
static K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_work_handler);
static K_WORK_DELAYABLE_DEFINE(flight_work, flight_work_handler);
static K_WORK_DEFINE(route_work, route_work_handler);
static K_WORK_DELAYABLE_DEFINE(mods_work, mods_work_handler);

static void kick_send(k_timeout_t delay) { k_work_reschedule_for_queue(&key_stream_q, &send_work, delay); }

/* 送るものが増えた（キー・状態・一覧）: すぐ送る。ただし、試し直しを待っているときは、その時刻を早めない
 * （k_work_schedule は、すでに予約・待ちがあれば何もしない。送れない間に、キーを押すたびにすぐ試し直して
 *  Bluetooth の暗号化を頼み直したりしないように。予約がなければ、動いている最中でもすぐ並べる） */
static void request_send(void) { k_work_schedule_for_queue(&key_stream_q, &send_work, K_NO_WAIT); }

static bool position_wanted(uint8_t mask, uint32_t position) {
    const uint8_t kind = position < POS_TP_FIRST ? STREAM_KEYS : STREAM_TRACKPAD;
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
    layers_sent = layers_requested;
    boot_reply_sent = boot_reply_requested;
    // やめたら、準備も、書き込み待ちに入る予定も消す（別の送り先・別の始め方に引き継がない）
    session_gen++;
    boot_go_pending = false;
    in_flight_boot_go = false;
    retrying = false;
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

/* 送っている最中の印を外す（lock の中で呼ぶ） */
static void finish_in_flight_locked(void) {
    in_flight = false;
    in_flight_boot_go = false;
    in_flight_transport = KEY_STREAM_TRANSPORT_NONE;
    in_flight_conn = NULL;
}

static void stop(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct bt_conn *conn = stop_locked();
    k_spin_unlock(&lock, key);
    release_conn(conn);
    // 時間切れの予約は取り消さない（送っていなければ何もしない。取り消しは、ほかの送り道の「開始」が予約し直した分を
    // 消してしまうことがある）
    LOG_DBG("Key stream stopped");
}

/* ---- 送る（専用のワークキュー） ---- */

static void build_status(uint8_t *report) {
    report[1] = MSG_STATUS;
    report[2] = atomic_get(&split_links) > 0 ? 1 : 0;
    report[3] = stream_mask;
    report[4] = POS_TP_FIRST;
    report[5] = ZMK_KEYMAP_LEN;
    // 次に届くキーの通し番号（送り待ちがあれば、その先頭。状態は送り待ちのキーより先に送るため）
    sys_put_le16(events_count > 0 ? events[events_head].seq : next_seq, &report[6]);
    report[8] = KS_HAS_BOOT ? CAP_BOOT : 0;
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

/* 有効なレイヤーのレポートを作る */
static void build_layers(uint8_t *report) {
    report[1] = MSG_LAYERS;
    // Default は ZMK の状態のビットには入らず、常に有効として扱われるので、足して送る
    sys_put_le32(zmk_keymap_layer_state() | BIT(zmk_keymap_layer_default()), &report[2]);
    report[6] = ZMK_KEYMAP_LAYERS_LEN;
    sys_put_le32((uint32_t)k_uptime_get(), &report[7]);
    report[11] = zmk_hid_get_explicit_mods();
    mods_seen = report[11];
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
    // 入力先の照合待ちの間は送らない（照合の後に、入力先のままなら送り直しを頼まれる）
    if (!streaming || in_flight || route_requested != route_checked) {
        k_spin_unlock(&lock, key);
        return;
    }
    memset(report, 0, sizeof(report));
    report[0] = KEY_STREAM_PROTOCOL_VERSION;
    // 送る順: 状態 → レイヤー → 送り待ちのキー → 押されているキーの一覧（一覧は、それより前のキーを送り終えた時点のもの）。
    // レイヤーはキーより先に送る（アプリが今のレイヤーを早く出せるように。送るのは作った時点の状態）
    // レイヤーを頼まれていなければ、前に頼まれていた分（止める前・種類を変える前のもの、見切った送り直し）は送らない
    if (!(stream_mask & STREAM_LAYERS)) {
        layers_sent = layers_requested;
    }
    uint8_t kind;
    uint32_t gen = 0;
    uint16_t first_seq = 0;
    size_t n = 0;
    if (status_requested != status_sent) {
        build_status(report);
        kind = MSG_STATUS;
        gen = status_requested;
    } else if (boot_reply_requested != boot_reply_sent) {
        report[1] = MSG_BOOT;
        report[2] = boot_reply_target;
        report[3] = boot_reply_result;
        kind = MSG_BOOT;
        gen = boot_reply_requested;
        // この答えを送り終えたら、書き込み待ちに入る（送れなかった・見切ったときは入らない）
        in_flight_boot_go = boot_go_pending && boot_reply_result == BOOT_OK;
        boot_go_pending = false;
    } else if (layers_requested != layers_sent) {
        build_layers(report);
        kind = MSG_LAYERS;
        gen = layers_requested;
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
    // この関数で送り終えるまで接続を使うので、参照を持つ（送っている間に接続が切れて、stream_conn の参照が
    // ほかのスレッドで手放されても使えるように）
    struct bt_conn *conn = NULL;
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    if (stream_conn != NULL) {
        conn = bt_conn_ref(stream_conn);
    }
#endif
    const uint32_t id = ++last_send_id;
    in_flight = true;
    in_flight_id = id;
    in_flight_kind = kind;
    in_flight_transport = transport;
    in_flight_conn = conn; // 見分けるだけ（参照は持たない）
    k_spin_unlock(&lock, key);

    int err = -ENOTSUP;
    if (transport == KEY_STREAM_TRANSPORT_USB) {
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_USB)
        err = key_stream_usb_send(report, id);
#endif
    } else if (transport == KEY_STREAM_TRANSPORT_BLE) {
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
        err = key_stream_ble_send(conn, report, id);
#endif
    }

    key = k_spin_lock(&lock);
    if (err == 0) {
        retrying = false;
        // 送り始めた分を取り除く（送っている間にあふれて先頭が進んでいたら、その分は数えない）
        if (kind == MSG_STATUS) {
            status_sent = gen;
        } else if (kind == MSG_LAYERS) {
            layers_sent = gen;
        } else if (kind == MSG_BOOT) {
            boot_reply_sent = gen;
        } else if (kind == MSG_PRESSED) {
            pressed_list_sent = gen;
        } else {
            while (events_count > 0 && (int16_t)(events[events_head].seq - first_seq) < (int16_t)n) {
                events_head = (events_head + 1) % EVENT_QUEUE_SIZE;
                events_count--;
            }
        }
        k_spin_unlock(&lock, key);
        release_conn(conn);
        // 送り終わったの知らせが来なければ、時間で見切る（知らせが先に届いていれば、見切るときに番号が合わない）
        const int32_t timeout_ms = transport == KEY_STREAM_TRANSPORT_USB ? IN_FLIGHT_TIMEOUT_USB_MS
                                                                         : IN_FLIGHT_TIMEOUT_BLE_MS;
        flight_watch_id = id;
        flight_deadline = k_uptime_get() + timeout_ms;
        k_work_reschedule_for_queue(&key_stream_q, &flight_work, K_MSEC(timeout_ms));
        return;
    }
    // 送れなかった: 送っている最中の印を外す（送っている間に送り道が切れて、もう外れていることもある）
    if (in_flight && in_flight_id == id) {
        finish_in_flight_locked();
    }
    if (!streaming) {
        // 送っている間に送り道が切れて、送るのはもうやめている（key_stream_transport_lost。送れなかったのは
        // そのため）。やめる処理は済んでいるので、警告もやめ直しもしない
        k_spin_unlock(&lock, key);
        release_conn(conn);
        return;
    }
    const bool retry = err == -EAGAIN || err == -EBUSY;
    bool give_up = false;
    if (retry) {
        const int64_t now = k_uptime_get();
        if (!retrying) {
            retrying = true;
            retry_since = now;
        }
        give_up = now - retry_since >= SEND_GIVE_UP_MS;
    }
    k_spin_unlock(&lock, key);
    release_conn(conn);

    if (retry && !give_up) {
        kick_send(K_MSEC(err == -EBUSY ? RETRY_SLOW_MS : RETRY_MS));
    } else {
        LOG_WRN("Key stream send failed (%d%s), stopping", err, give_up ? ", timed out" : "");
        stop();
    }
}

void key_stream_sent(uint32_t id) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool finished = in_flight && in_flight_id == id;
    const bool go = finished && in_flight_kind == MSG_BOOT && in_flight_boot_go;
    if (finished) {
        finish_in_flight_locked();
    }
    k_spin_unlock(&lock, key);
    if (go) {
        boot_go();
    }
    if (finished) {
        kick_send(K_NO_WAIT);
    }
}

/* 送り終わったの知らせが来ないまま時間が過ぎた: そのレポートを見切って次へ進む（専用のワークキュー）。
 * 見切ったレポートは届いていないかもしれないので、状態・レイヤーならもう一度送り、キー・一覧なら押されているキーの一覧を
 * 送り直す（キーが届かなければ、アプリでは通し番号が飛ぶ）。後から「送り終わった」が届いても、番号が違うので無視する。
 * 前のレポートの分の予約が、次のレポートを送った後に動くこともある（予約し直しても、すでに並んだ処理は
 * 取り消されない）ので、見切る時刻も確かめる */
static void flight_work_handler(struct k_work *work) {
    const int64_t remaining = flight_deadline - k_uptime_get();
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool watching = in_flight && in_flight_id == flight_watch_id;
    const bool expired = watching && remaining <= 0;
    if (expired) {
        finish_in_flight_locked();
        if (streaming) {
            if (in_flight_kind == MSG_STATUS) {
                status_requested++;
            } else if (in_flight_kind == MSG_BOOT) {
                // 書き込み待ちの答えは送り直さない（アプリは答えが来なければ、時間で見切る）
            } else if (in_flight_kind == MSG_LAYERS) {
                // 今もレイヤーを頼まれているときだけ（種類を変えて始め直した後に、前の分を送り直さない）
                if (stream_mask & STREAM_LAYERS) {
                    layers_requested++;
                }
            } else {
                pressed_list_requested++;
            }
        }
    }
    k_spin_unlock(&lock, key);
    if (watching && !expired) {
        k_work_reschedule_for_queue(&key_stream_q, &flight_work, K_MSEC(remaining));
        return;
    }
    if (expired) {
        LOG_WRN("Key stream report %u not completed, giving up on it", (unsigned int)flight_watch_id);
        kick_send(K_NO_WAIT);
    }
}

/* ---- 入力先との照合 ---- */

/* その送り道（Bluetooth ならその接続）が、ZMK がいまキーの入力を送っている先か。専用のワークキューで、
 * lock の外で呼ぶ（Bluetooth の接続を探すため）。conn は呼び出し側が参照を持っていること */
static bool route_selected(enum key_stream_transport transport, struct bt_conn *conn) {
    const struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    switch (transport) {
    case KEY_STREAM_TRANSPORT_USB:
        return ep.transport == ZMK_TRANSPORT_USB;
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    case KEY_STREAM_TRANSPORT_BLE: {
        // どの送り道もつながっていないとき、ZMK は Bluetooth を選んだことにするので、選んでいるプロファイルが
        // つながっているかも確かめる（つながっていないと zmk_ble_active_profile_conn は警告を出す）
        if (ep.transport != ZMK_TRANSPORT_BLE || conn == NULL ||
            !zmk_ble_active_profile_is_connected()) {
            return false;
        }
        // 選んでいるプロファイルの接続（参照が付いて返るので手放す）
        struct bt_conn *active = zmk_ble_active_profile_conn();
        const bool same = active == conn;
        if (active != NULL) {
            bt_conn_unref(active);
        }
        return same;
    }
#endif
    default:
        ARG_UNUSED(conn);
        return false;
    }
}

/* 入力先が変わった（USB ⇔ Bluetooth、プロファイルの切り替え）: 送っている先が入力先でなくなっていたら、
 * 送るのをやめる（専用のワークキュー）。入力先に戻れば、アプリが送り続けている「開始」でまた始まる。
 * 入力先が実際に変わっていたら（route_left）、照合の時点で前の入力先に戻っていても、やめる。
 * 照合している間に次の知らせが来たら、この処理がもう一度並ぶ（照合待ちのまま、次の照合で決める） */
static void route_work_handler(struct k_work *work) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    // ここまでの知らせの分を照合する（ZMK は入力先を変えてから知らせるので、この後に読む入力先は新しい）
    const uint32_t gen = route_requested;
    const bool left = route_left;
    route_left = false;
    const bool active = streaming;
    const enum key_stream_transport transport = stream_transport;
    // 調べている間に接続が切れて、stream_conn の参照がほかのスレッドで手放されても使えるように、参照を持つ
    struct bt_conn *conn = NULL;
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
    if (stream_conn != NULL) {
        conn = bt_conn_ref(stream_conn);
    }
#endif
    k_spin_unlock(&lock, key);
    const bool keep = !active || (!left && route_selected(transport, conn));

    key = k_spin_lock(&lock);
    // 調べている間に、切れて（key_stream_transport_lost）もうやめていたら、やめ直さない
    const bool same = !keep && streaming && stream_transport == transport && stream_conn == conn;
    struct bt_conn *old_conn = NULL;
    if (same) {
        old_conn = stop_locked();
    }
    route_checked = gen;
    const bool resume = streaming && route_requested == route_checked;
    k_spin_unlock(&lock, key);
    release_conn(old_conn);
    release_conn(conn);
    if (same) {
        LOG_DBG("Key stream stopped: no longer the selected endpoint");
    }
    if (resume) {
        // 照合待ちの間にためたキーなどを送る
        kick_send(K_NO_WAIT);
    }
}

/* 入力先が変わったの知らせ。知らせを出したスレッドで呼ばれるので、照合待ちの印だけ付けて、専用のワークキューに渡す。
 * USB ⇔ Bluetooth の切り替え・プロファイルの切り替えは zmk_endpoint_changed で届く。入力先の番号が変わらないまま、
 * 選んでいるプロファイルの接続が変わる（つながった・切れた、BT_CLR で消した）ときは zmk_endpoint_changed が
 * 出ないことがあるので、zmk_ble_active_profile_changed も見る */
static int key_stream_endpoint_listener(const zmk_event_t *eh) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    route_requested++;
    // ZMK は入力先が実際に変わったときだけ zmk_endpoint_changed を出す
    if (as_zmk_endpoint_changed(eh) != NULL) {
        route_left = true;
    }
    k_spin_unlock(&lock, key);
    k_work_submit_to_queue(&key_stream_q, &route_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_stream_endpoint, key_stream_endpoint_listener);
ZMK_SUBSCRIPTION(key_stream_endpoint, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_KEY_STREAM_BLE)
ZMK_SUBSCRIPTION(key_stream_endpoint, zmk_ble_active_profile_changed);
#endif

/* ---- 命令（専用のワークキュー） ---- */

static void start(enum key_stream_transport transport, struct bt_conn *conn, uint8_t mask) {
    struct bt_conn *old_conn = NULL;
    const int64_t now = k_uptime_get();

    // ZMK がいまキーの入力を送っている先からの「開始」だけを受け付ける（ほかの PC には、別の PC で打った内容を
    // 送らない）。答えずに無視する（生存確認の時間も数え直さない）。入力先が変わって、今送っている先からの
    // 「開始」を無視したときは、入力先が変わったの知らせ（route_work）で送るのをやめる。
    // 照合の前に、ここまでに届いた「入力先が変わった」の知らせの数を覚えておく（下で使う）
    k_spinlock_key_t key = k_spin_lock(&lock);
    const uint32_t route_gen = route_requested;
    k_spin_unlock(&lock, key);
    if (!route_selected(transport, conn)) {
        LOG_DBG("Key stream start ignored: not the selected endpoint (transport %d)", transport);
        release_conn(conn);
        return;
    }

    key = k_spin_lock(&lock);
    const bool fresh = !streaming || stream_transport != transport || stream_conn != conn ||
                       stream_mask != mask;
    // 送っている最中のものが、この「開始」と同じ送り道のものか（止めた後に同じ送り道で始め直したときも含む）
    const bool same_route = in_flight_on_locked(transport, conn);
    if (fresh) {
        old_conn = stop_locked();
        // 送り道が変わったら、送っている最中のものの「送り終わった」は待たない（前の送り道のものや、届かないものを
        // 待ち続けないように。後から届いても、番号が違うので無視される。USB は、終わっていない転送のバッファを
        // 書き換えずに次を送る）。同じ送り道なら、ふつうに送り終わりを待つ（終わっていない転送に重ねて送ろうと
        // しないように。届かなければ送り終わりの見張りが打ち切る）
        if (in_flight && !same_route) {
            finish_in_flight_locked();
        }
        streaming = true;
        stream_transport = transport;
        stream_conn = conn; // 参照は命令から引き継ぐ
        conn = NULL;
        stream_mask = mask;
        // 照合の後に新しい知らせが来ていなければ、ここでの照合が最新（新しい入力先から始めた）。前の入力先から
        // 変わったという印（route_left）は、この送り道には当てはまらないので消す（並んでいる route_work が、
        // 始めたばかりの送信をやめないように）
        if (route_requested == route_gen) {
            route_left = false;
        }
        status_requested++;
        // 押されたままのキーを知らせる（アプリが途中から見ても、押しっぱなしの扱いを合わせられるように）。
        // 1件ずつの「押した」（印の bit1）と、その後に押されているキーの一覧の両方を送る
        for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
            if (pressed[pos] && position_wanted(mask, pos)) {
                push_event((uint8_t)pos, FLAG_PRESSED | FLAG_SNAPSHOT, (uint32_t)now);
            }
        }
        pressed_list_requested++;
        if (mask & STREAM_LAYERS) {
            layers_requested++;
        }
        LOG_DBG("Key stream started (transport %d, mask 0x%02x)", transport, mask);
    }
    k_spin_unlock(&lock, key);

    release_conn(old_conn);
    release_conn(conn); // 生存確認だけのとき: 命令の参照は要らない
    // 「開始」が届くたびに、送るのをやめるまでの時間を数え直す
    last_keepalive = now;
    k_work_reschedule_for_queue(&key_stream_q, &timeout_work, K_MSEC(KEEPALIVE_TIMEOUT_MS));
    if (fresh) {
        kick_send(K_NO_WAIT);
    }
}

/* 書き込み待ちの答えを送る（専用のワークキュー）。結果 0 なら、送り終わったの知らせが来てから入る（boot_go） */
static void boot_reply(uint8_t target, uint8_t result) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool send = streaming;
    if (send) {
        boot_reply_target = target;
        boot_reply_result = result;
        boot_reply_requested++;
        boot_go_pending = result == BOOT_OK;
    }
    k_spin_unlock(&lock, key);
    if (send) {
        request_send();
    }
}

#if KS_HAS_BOOT
/* 書き込み待ちにする（答えを送ってから。専用のワークキュー）。キーマップの &bootloader を押して離したことにする:
 * 親機は自分で（ZMK の reset behavior が、ブートローダーに入る印を付けて再起動する）、子機には ZMK の分割の仕組みで伝える
 * （子機の &bootloader のキーを押したときと同じ道。子機のファームウェアは変えなくてよい） */
static uint8_t boot_target;
static void boot_work_handler(struct k_work *work) {
    const struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(bootloader)),
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        // 子機は 1 つ（source 0）。親機は自分（LOCAL）
        .source = boot_target == BOOT_TARGET_PERIPHERAL ? 0 : ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
    LOG_INF("Key stream: entering bootloader (%s)", boot_target == BOOT_TARGET_PERIPHERAL ? "peripheral" : "central");
    zmk_behavior_invoke_binding(&binding, event, true);
    zmk_behavior_invoke_binding(&binding, event, false);
}
static K_WORK_DELAYABLE_DEFINE(boot_work, boot_work_handler);
#endif

/* 答えを送り終えた: 少し待ってから（PC が答えを読めるように）書き込み待ちに入る。どのスレッドから呼んでもよい */
static void boot_go(void) {
#if KS_HAS_BOOT
    k_work_reschedule_for_queue(&key_stream_q, &boot_work, K_MSEC(BOOT_DELAY_MS));
#endif
}

/* 書き込み待ちの命令（0x84・0x85。専用のワークキュー）。送っている相手（入力先）からのものだけ */
static void boot_command(const struct command *cmd) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    // 入力先が変わったの知らせを照合し終えていること（照合待ちの間は、前の入力先からの命令を受け付けない）
    const bool same = streaming && stream_transport == cmd->transport && stream_conn == cmd->conn && route_requested == route_checked;
    const uint32_t session = session_gen;
    k_spin_unlock(&lock, key);
    if (!same || cmd->len < 3) {
        return;
    }
    // 今も入力先であること（照合の後に入力先が変わっていても、知らせの処理より先にここへ来ることがあるため、確かめ直す）
    if (!route_selected(cmd->transport, cmd->conn)) {
        return;
    }
    const uint8_t target = cmd->data[2];
    if (target != BOOT_TARGET_CENTRAL && target != BOOT_TARGET_PERIPHERAL) {
        return;
    }
    if (!KS_HAS_BOOT || (target == BOOT_TARGET_PERIPHERAL && !IS_ENABLED(CONFIG_ZMK_SPLIT))) {
        boot_reply(target, BOOT_UNSUPPORTED);
        return;
    }
    const int64_t now = k_uptime_get();
    if (cmd->data[1] == CMD_BOOT_ARM) {
        boot_armed_target = target;
        boot_armed_at = now;
        boot_armed_session = session;
        boot_reply(target, BOOT_ARMED);
        return;
    }
    // 実行（0x85）: 同じ送り先・同じ始め方（世代）で、同じどちらの準備が、3 秒以内にあること。準備は 1 回で使い切る
    const bool armed = boot_armed_target == target && boot_armed_session == session && now - boot_armed_at <= BOOT_ARM_MS;
    boot_armed_target = -1;
    if (!armed) {
        boot_reply(target, BOOT_NOT_ARMED);
        return;
    }
    if (target == BOOT_TARGET_PERIPHERAL && atomic_get(&split_links) == 0) {
        boot_reply(target, BOOT_NO_PERIPHERAL);
        return;
    }
#if KS_HAS_BOOT
    boot_target = target;
#endif
    // 答えを送り終えてから入る（key_stream_sent → boot_go）
    boot_reply(target, BOOT_OK);
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
            const bool same = streaming && stream_transport == cmd.transport && stream_conn == cmd.conn;
            if (same) {
                status_requested++;
                pressed_list_requested++;
                if (stream_mask & STREAM_LAYERS) {
                    layers_requested++;
                }
            }
            k_spin_unlock(&lock, key);
            release_conn(cmd.conn);
            if (same) {
                request_send();
            }
            break;
        }
        case CMD_BOOT_ARM:
        case CMD_BOOT_RUN:
            boot_command(&cmd);
            release_conn(cmd.conn);
            break;
        default:
            release_conn(cmd.conn);
            break;
        }
    }
}

/* 「開始」（生存確認）が KEEPALIVE_TIMEOUT_MS の間届かなかった（届くたびに start で予約し直す）。
 * start を処理している間に時間が来ると、予約し直しても、すでに並んだこの処理は取り消されずに動くので、
 * 最後に届いた時刻で確かめ直す（専用のワークキューだけで触るので lock は要らない） */
static void timeout_work_handler(struct k_work *work) {
    const int64_t remaining = last_keepalive + KEEPALIVE_TIMEOUT_MS - k_uptime_get();
    if (remaining > 0) {
        k_work_reschedule_for_queue(&key_stream_q, &timeout_work, K_MSEC(remaining));
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool active = streaming;
    k_spin_unlock(&lock, key);
    if (active) {
        LOG_DBG("Key stream keepalive timed out");
        stop();
    }
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
    if (was_in_flight) {
        finish_in_flight_locked();
    }
    k_spin_unlock(&lock, key);
    release_conn(old_conn);
    if (was_in_flight) {
        kick_send(K_NO_WAIT);
    }
}

/* ---- キーの出来事（見るだけで、止めない） ---- */

static int key_stream_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos == NULL || pos->position >= ZMK_KEYMAP_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    // 押した状態が変わらない出来事と、その位置で前に受け取った出来事より古い出来事は送らない（ZMK の combo は、
    // 判定のために預かったキーを、元の時刻のまま listener の最初からもう一度出し直すので、同じキーの「押した」が
    // もう一度届いたり、「離した」の後に古い「押した」が届いたりする）
    if (pressed[pos->position] == pos->state || pos->timestamp < last_event_ts[pos->position]) {
        k_spin_unlock(&lock, key);
        return ZMK_EV_EVENT_BUBBLE;
    }
    pressed[pos->position] = pos->state;
    last_event_ts[pos->position] = pos->timestamp;
    const bool queued = streaming && position_wanted(stream_mask, pos->position);
    if (queued) {
        push_event((uint8_t)pos->position, pos->state ? FLAG_PRESSED : 0, (uint32_t)pos->timestamp);
    }
    const bool watch = streaming && (stream_mask & STREAM_LAYERS);
    k_spin_unlock(&lock, key);
    if (queued) {
        request_send();
    }
    if (watch) {
        watch_mods();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

/* 修飾キーを見直す（専用のワークキュー）。ZMK の hold-tap は、判定の間に預かったキーの出来事（修飾キーの押し離しを含む）を、
 * 判定の後に自分より後ろの listener にだけ出し直すので、こちらの listener には届かないことがある。そのため、キーを押している間と、
 * 修飾キーが残っている間（&sk など）は、短い間隔で修飾キーを読み、前に送ったものと違えばレイヤーの報告を送る */
static void mods_work_handler(struct k_work *work) {
    const uint8_t mods = zmk_hid_get_explicit_mods();
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool active = streaming && (stream_mask & STREAM_LAYERS);
    const bool changed = active && mods != mods_seen;
    if (changed) {
        mods_seen = mods;
        layers_requested++;
    }
    bool any = false;
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN && !any; pos++) {
        any = pressed[pos];
    }
    k_spin_unlock(&lock, key);
    if (changed) {
        request_send();
    }
    if (active && (any || mods != 0)) {
        k_work_schedule_for_queue(&key_stream_q, &mods_work, K_MSEC(MODS_WATCH_MS));
    }
}

/* 修飾キーの見直しを始める（すでに予約があれば何もしない） */
static void watch_mods(void) {
    k_work_schedule_for_queue(&key_stream_q, &mods_work, K_MSEC(MODS_WATCH_MS));
}

ZMK_LISTENER(key_stream, key_stream_listener);
ZMK_SUBSCRIPTION(key_stream, zmk_position_state_changed);

/* ---- レイヤー・修飾キーの変化（見るだけで、止めない） ----
 * 知らせを出したスレッドで呼ばれるので、送り待ちの印だけ付ける（状態は送るときに、専用のワークキューで読む）。
 * ZMK v0.3.0 は修飾キーが変わったの知らせ（zmk_modifiers_state_changed）を出さないので、修飾キーのキーコード
 * （キーボードのページの 0xE0〜0xE7）の押し離しで送る。修飾キーの状態は、この知らせを受けた ZMK の hid_listener が
 * 書き換えるので、送るときに読めば、書き換えた後の状態になる（専用のワークキューは、知らせを出したスレッドより後に動く） */
#define HID_PAGE_KEYBOARD 0x07
#define HID_KEY_FIRST_MOD 0xE0
#define HID_KEY_LAST_MOD 0xE7

static bool is_modifier_event(const struct zmk_keycode_state_changed *kc) {
    return kc->usage_page == HID_PAGE_KEYBOARD && kc->keycode >= HID_KEY_FIRST_MOD &&
           kc->keycode <= HID_KEY_LAST_MOD;
}

static int key_stream_layer_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (as_zmk_layer_state_changed(eh) == NULL && (kc == NULL || !is_modifier_event(kc))) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool queued = streaming && (stream_mask & STREAM_LAYERS);
    if (queued) {
        layers_requested++;
    }
    k_spin_unlock(&lock, key);
    if (queued) {
        request_send();
        // 修飾キーが残る間（&sk など）も見直す
        watch_mods();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_stream_layer, key_stream_layer_listener);
ZMK_SUBSCRIPTION(key_stream_layer, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(key_stream_layer, zmk_keycode_state_changed);

/* ---- 右（子機）とのつながり ----
 * 親機は、右とは自分が central 側（接続を始める側）、PC とは peripheral 側でつなぐので、接続の役割で
 * 右との接続を見分けて数える。ZMK v0.3.0 の親機は、右とのつながりの変化を出来事として出さない
 * （zmk_split_peripheral_status_changed は子機の側でだけ出る）ため、自分で見る */

#if WATCH_SPLIT_LINK

/* つながりが変わった: 送っている間なら状態を送り直す（値は送るときに調べる）。Bluetooth の接続・切断の通知
 * （つながったときは Bluetooth の受信のスレッド、切れたときはシステムワークキュー）から呼ばれる。
 * 右のキーの「離した」は ZMK が、右のトラックパッドの位置の「離した」は position_relay.c が出すので、
 * ふつうのキーの出来事として届く */
static void split_link_changed(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool send = streaming;
    if (send) {
        status_requested++;
    }
    k_spin_unlock(&lock, key);
    if (send) {
        request_send();
    }
}

static void split_connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0 && is_split_link(conn)) {
        atomic_inc(&split_links);
        split_link_changed();
    }
}

static void split_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (is_split_link(conn)) {
        atomic_dec(&split_links);
        split_link_changed();
    }
}

BT_CONN_CB_DEFINE(key_stream_split_callbacks) = {
    .connected = split_connected,
    .disconnected = split_disconnected,
};
#endif

static int key_stream_init(void) {
    static const struct k_work_queue_config config = {.name = "key_stream"};
    k_work_queue_start(&key_stream_q, key_stream_stack, K_THREAD_STACK_SIZEOF(key_stream_stack),
                       THREAD_PRIORITY, &config);
    // ワークキューを始める前に入力先が変わったの知らせが来ていたら、渡せていないので、ここで照合を済ませる
    // （照合待ちのままだと送れない）
    k_work_submit_to_queue(&key_stream_q, &route_work);
    return 0;
}

SYS_INIT(key_stream_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
