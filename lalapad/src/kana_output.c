/*
 * かな入力モジュールの共通部分。
 *
 * - 送信はすべて ZMK の behavior queue（マクロと同じ仕組み）に &kp として並べ、順番を保つ。
 * - キューが満杯で「離す」を登録できなかったときは、登録できるまで再試行する（押しっぱなし防止）。
 *   修飾キーを戻す合図（下）も同じく、登録できるまで再試行する。
 * - かな配列モードのオフは「受付停止 → IME オフを並べる → 完了の合図 (&kana_sync) を並べる →
 *   合図がキューで実際に処理されたらレイヤー解除」の順で行う。時間の見積もりには頼らないので、
 *   処理が遅れても、解除前後の入力がローマ字のまま送られたり、残りのかなを追い越したりしない。
 * - 同じ合図の仕組みを、ほかのモジュール（Alt コード）にも「それより前に並べた送信がすべて終わったら
 *   知らせる」目印として貸す（kana_output_add_marker）。下の「修飾キーを隠す／戻す」合図も同じ仕組み。
 *   合図の番号で、どの合図（解除・目印・隠す・戻すの4種類）かを見分ける。
 * - かな・記号の打鍵の列は、前後に「修飾キーを隠す／戻す」合図を並べて送る（kana_output_send_keys）。
 *   合図はキューの中で打鍵と同じ順に処理されるので、押している Shift・Ctrl が、その列の打鍵にかからない。
 *   続けて打ったかなの間では、押したままの Shift・Ctrl が一瞬 PC に届く（戻してから次の列で隠し直すため。
 *   次の列が並んでいるときに戻さずにおくと、その間に並んだ Space・Enter などにも Shift・Ctrl がかからなく
 *   なるので、そうはしない）。
 *   隠すのは Shift・Ctrl だけ（KANA_MASKED_MODS）。Alt・Win は隠さない（押したままの Alt・Win を隠すと、
 *   PC には Alt・Win を単独で押して離したように見え、メニューバーやスタートメニューが開くため）。
 *   制約: 隠している間（その列を送り終えるまで）に、キューを通らないほかのキーを Shift・Ctrl といっしょに
 *   押すと、そのキーにも Shift・Ctrl がかからない（隠すのは PC へ送るレポート全体のため）。
 *   隠すのは ZMK の masked modifiers（mod-morph と同じ仕組み）。キーマップで mod-morph を使うと、
 *   互いに隠す設定を上書きし合うので、併用しない（このキーマップでは使っていない）。
 *
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので、状態の排他は不要
 * （接続先の切り替えの知らせはほかのスレッドから届くことがあるが、bool を1つ書くだけ）。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"
#include "tp_positions.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 合図の behavior（&kana_sync）は lalapad/dts/behaviors/kana.dtsi で定義している */
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(kana_sync)),
             "kana: the kana_sync node is missing (include <behaviors/kana.dtsi> in the keymap)");

#define KP_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kp))
#define SYNC_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kana_sync))
#define RETRY_INTERVAL_MS 10
/* 合図の番号（&kana_sync の引数）。かなモードの解除の合図は同時に1つしかない（解除中は解除も入り直しも
 * 受け付けない）。目印は並べた順に処理されるので、どの目印かは呼ぶ側が順番で分かる。
 * 修飾キーを隠す／戻す合図は、打鍵の列ごとに必ず対で並ぶ（戻す合図の再試行中は次の列を並べない）。
 * 0 は使わない（キーマップに &kana_sync 0 と直接書いて押されても何もしない。&kana_sync は ZMK Studio の
 * 一覧には出るが、引数の情報を持たないので Studio からは割り当てられない） */
#define SYNC_KANA_OFF 1
#define SYNC_MARKER 2
#define SYNC_MASK_ON 3
#define SYNC_MASK_OFF 4

/* ---- 送信キュー ---- */

/* キューが満杯のため、まだ登録できていない「必ず送る」もの（キーの「離す」と、修飾キーを戻す合図）。
 * 登録できるまで順に再試行する。
 * 押しっぱなしのキー（スペースなど）の「離す」はキー位置ごとに1つまで。タップの「離す」と修飾キーを戻す
 * 合図は、再試行中に次のタップや列を受け付けない（-EBUSY）ので、1つの列の分（それぞれ1つ）まで。
 * 全キー位置に押しっぱなしのキーを置いても足りる数にする
 * （足りないと、下の「その場で実行」が、まだキューにある「押す」より先に実行されてしまう） */
#define PENDING_RELEASE_MAX (ZMK_KEYMAP_LEN + 2)

struct pending_release {
    struct zmk_behavior_binding binding;
    bool press;
    struct zmk_behavior_binding_event event;
    uint32_t wait;
};

static struct pending_release pending_releases[PENDING_RELEASE_MAX];
static size_t pending_count;

static void retry_release_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_release_work, retry_release_work_handler);

static bool release_pending(void) { return pending_count > 0; }

static struct zmk_behavior_binding kp_binding(uint32_t keycode) {
    return (struct zmk_behavior_binding){
        .behavior_dev = KP_BEHAVIOR_NAME,
        .param1 = keycode,
    };
}

static struct zmk_behavior_binding sync_binding(uint32_t token) {
    return (struct zmk_behavior_binding){
        .behavior_dev = SYNC_BEHAVIOR_NAME,
        .param1 = token,
    };
}

static void cancel_kana_mask(void);

static void add_pending_release(const struct zmk_behavior_binding_event *event,
                                struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    if (pending_count >= PENDING_RELEASE_MAX) {
        // 上の数え方では起きない（配列の範囲を守るための確認。下の後始末は、起きたときに押しっぱなし・
        // 隠したままにしないための最低限のもので、順番や隠す範囲は守れない）
        if (strcmp(binding.behavior_dev, SYNC_BEHAVIOR_NAME) == 0 &&
            binding.param1 == SYNC_MASK_OFF) {
            // 修飾キーを戻す合図: その場で実行すると、同じ列の「隠す」合図がまだキューにあれば、後から
            // 処理されて隠したままになる。実行はせず、その列の「隠す」を取り消す（隠したままにしない）
            LOG_ERR("Too many pending releases, cancelling modifier mask");
            cancel_kana_mask();
            return;
        }
        // キーの「離す」: 押しっぱなしにはせず、その場で実行する
        LOG_ERR("Too many pending releases, invoking 0x%08X immediately", binding.param1);
        struct zmk_behavior_binding_event now = *event;
        now.timestamp = k_uptime_get();
        zmk_behavior_invoke_binding(&binding, now, press);
        return;
    }
    pending_releases[pending_count++] = (struct pending_release){
        .binding = binding,
        .press = press,
        .event = *event,
        .wait = wait,
    };
    k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
}

/* 必ず送るもの（キーの「離す」、修飾キーを戻す合図）を並べる。先に待っているものがあれば、順番を守るため
 * その後ろに並べる。満杯なら登録できるまで自動で再試行する */
static void queue_must(const struct zmk_behavior_binding_event *event,
                       struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    if (release_pending() || zmk_behavior_queue_add(event, binding, press, wait) < 0) {
        add_pending_release(event, binding, press, wait);
    }
}

static void retry_release_work_handler(struct k_work *work) {
    // 先に失敗したものから順に登録する
    while (pending_count > 0) {
        struct pending_release *p = &pending_releases[0];
        if (zmk_behavior_queue_add(&p->event, p->binding, p->press, p->wait) < 0) {
            k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
            return;
        }
        pending_count--;
        memmove(&pending_releases[0], &pending_releases[1],
                pending_count * sizeof(pending_releases[0]));
    }
}

int kana_output_tap(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                    uint32_t tap_ms, uint32_t wait_ms) {
    if (release_pending()) {
        return -EBUSY;
    }

    const struct zmk_behavior_binding kp = kp_binding(keycode);

    if (zmk_behavior_queue_add(event, kp, true, tap_ms) < 0) {
        return -ENOSPC;
    }

    if (zmk_behavior_queue_add(event, kp, false, wait_ms) < 0) {
        add_pending_release(event, kp, false, wait_ms);
        return -EAGAIN;
    }

    return 0;
}

int kana_output_press(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                      uint32_t tap_ms) {
    if (release_pending()) {
        return -EBUSY;
    }
    if (zmk_behavior_queue_add(event, kp_binding(keycode), true, tap_ms) < 0) {
        return -ENOSPC;
    }
    return 0;
}

void kana_output_release(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                         uint32_t wait_ms) {
    queue_must(event, kp_binding(keycode), false, wait_ms);
}

/* ---- 修飾キーを隠す ---- */

/* 使い手ごとの隠したい修飾キー（0 = 隠していない） */
static uint8_t mask_mods[KANA_MASK_OWNER_COUNT];

/* かなの「隠す」合図を並べた数と、処理した数（取り消すときに、まだキューにあるかを見分ける） */
static uint32_t mask_on_queued;
static uint32_t mask_on_done;
/* mask_on_cancelled なら、mask_on_cancel_seq 番目に処理される「隠す」合図では隠さない */
static bool mask_on_cancelled;
static uint32_t mask_on_cancel_seq;

void kana_output_mask_mods(enum kana_mask_owner owner, uint8_t mods) {
    mask_mods[owner] = mods;

    // どれかの使い手が隠したいものはすべて隠す（和集合）
    uint8_t combined = 0;
    for (size_t i = 0; i < ARRAY_SIZE(mask_mods); i++) {
        combined |= mask_mods[i];
    }

    const int changed =
        combined != 0 ? zmk_hid_masked_modifiers_set(combined) : zmk_hid_masked_modifiers_clear();
    if (changed > 0) {
        // PC へ送る修飾キーが変わった: 次のキーを待たずに知らせる
        // （隠すのをやめたとき、押したままの Shift などを PC 側でも押したままに戻す）
        zmk_endpoints_send_report(HID_USAGE_KEY);
    }
}

/* 修飾キーを戻す合図を並べられなかったときに、その列の「隠す」を取り消す（add_pending_release から呼ぶ。
 * 戻す合図が再試行に回っている間は次の列を並べないので、その列の「隠す」合図は最後に並べたもの） */
static void cancel_kana_mask(void) {
    if (mask_on_done != mask_on_queued) {
        // その列の「隠す」合図がまだキューにある: 処理されても隠さない（それより前の列は、対の合図で戻る）
        mask_on_cancelled = true;
        mask_on_cancel_seq = mask_on_queued;
        return;
    }
    // その列の「隠す」合図は処理済み（いま隠しているのはその列の分）: すぐ戻す。
    // 注意: その列の打鍵がまだキューに残っていれば、それらは隠さずに送られる（押したままの Shift・Ctrl が
    // かかる）。隠したままになるよりはよいとして、起きないはずのこの場合には備えない
    kana_output_mask_mods(KANA_MASK_KANA, 0);
}

/* ---- IME オンの送り直し ---- */

/* 最後にかな・記号を送った（kana_output_ime_on を呼んだ、またはかな配列モードに入って IME オンを送った）時刻 */
static int64_t last_char_ms;
/* last_char_ms が有効か（false なら、次の kana_output_ime_on で必ず送る）。接続先の切り替えと、
 * トラックパッドのクリック・ジェスチャーで false に戻す */
static bool char_sent;

int kana_output_ime_on(const struct zmk_behavior_binding_event *event, uint32_t tap_ms,
                       uint32_t wait_ms, uint32_t resync_ms) {
    const int64_t now = k_uptime_get();

    if (resync_ms > 0 && char_sent && now - last_char_ms < resync_ms) {
        last_char_ms = now;
        return 0;
    }

    const int ret = kana_output_tap(event, LANGUAGE_1, tap_ms, wait_ms);
    if (ret == -EBUSY || ret == -ENOSPC) {
        // 登録できなかった: 時刻は更新しない（次に呼ばれたときに改めて送る）
        return ret;
    }
    last_char_ms = now;
    char_sent = true;
    return ret;
}

int kana_output_send_keys(const struct zmk_behavior_binding_event *event, const uint32_t *keys,
                          size_t keys_len, uint32_t tap_ms, uint32_t wait_ms, bool ime_on,
                          uint32_t resync_ms) {
    if (release_pending()) {
        return -EBUSY;
    }
    // 修飾キーを隠す合図。キューの処理が待ち時間で止まっていなければ、この中ですぐ処理されるので、
    // 並べた数は先に数える
    mask_on_queued++;
    if (zmk_behavior_queue_add(event, sync_binding(SYNC_MASK_ON), true, 0) < 0) {
        mask_on_queued--;
        return -ENOSPC;
    }

    int ret = 0;
    if (ime_on) {
        ret = kana_output_ime_on(event, tap_ms, wait_ms, resync_ms);
    }
    for (size_t i = 0; ret == 0 && i < keys_len; i++) {
        // -EBUSY / -ENOSPC: 何も登録していない。-EAGAIN: 「離す」は自動で再試行される。
        // いずれも残りは送らない（順番が崩れないように）
        ret = kana_output_tap(event, keys[i], tap_ms, wait_ms);
    }

    // 隠す合図を並べたので、途中で打ち切っても、戻す合図は必ず並べる（「離す」の再試行中なら、その後ろ）
    queue_must(event, sync_binding(SYNC_MASK_OFF), true, 0);
    return ret;
}

/* ---- かな配列モード ---- */

static bool kana_active;
static zmk_keymap_layer_id_t kana_layer;

enum kana_off_stage {
    KANA_OFF_NONE,
    KANA_OFF_SEND_IME_OFF, /* IME オフ (LANGUAGE_2) をキューに並べる */
    KANA_OFF_SEND_SYNC,    /* その後ろに完了の合図 (&kana_sync) を並べる */
    KANA_OFF_WAIT_SYNC,    /* 合図がキューで処理されるのを待つ（処理されたらレイヤー解除） */
};

static enum kana_off_stage off_stage = KANA_OFF_NONE;
static struct zmk_behavior_binding_event off_event;
/* かなモードを抜けるときに、かな単打面といっしょに解除するレイヤー（keymap の &kana_sw の sub-layers） */
static const zmk_keymap_layer_id_t *off_sub_layers;
static size_t off_sub_layers_len;
static uint32_t off_tap_ms;
static uint32_t off_wait_ms;

static void kana_off_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(kana_off_work, kana_off_work_handler);

static void kana_off_work_handler(struct k_work *work) {
    if (off_stage == KANA_OFF_SEND_IME_OFF) {
        // かなと同じく、押している Shift・Ctrl を隠して送る（Shift+無変換などにならないように）。
        // -ENOSPC のときに隠す／戻す合図の対だけが並ぶことがあるが、何も送らないので害はない
        static const uint32_t ime_off_key = LANGUAGE_2;
        const int ret =
            kana_output_send_keys(&off_event, &ime_off_key, 1, off_tap_ms, off_wait_ms, false, 0);
        if (ret == -EBUSY || ret == -ENOSPC) {
            k_work_schedule(&kana_off_work, K_MSEC(RETRY_INTERVAL_MS));
            return;
        }
        off_stage = KANA_OFF_SEND_SYNC;
    }

    if (off_stage == KANA_OFF_SEND_SYNC) {
        // IME オフの「離す」を再試行中なら、その登録が済んでから合図を並べる
        if (release_pending()) {
            k_work_schedule(&kana_off_work, K_MSEC(RETRY_INTERVAL_MS));
            return;
        }
        // キューの処理が待ち時間で止まっていなければ zmk_behavior_queue_add の中で合図がすぐ処理されるので、
        // 先に待ち状態にする
        off_stage = KANA_OFF_WAIT_SYNC;
        if (zmk_behavior_queue_add(&off_event, sync_binding(SYNC_KANA_OFF), true, 0) < 0) {
            off_stage = KANA_OFF_SEND_SYNC;
            k_work_schedule(&kana_off_work, K_MSEC(RETRY_INTERVAL_MS));
            return;
        }
    }
}

/* ---- 目印（それより前に並べた送信がすべて終わったら知らせる） ---- */

static kana_output_marker_cb_t marker_cb;

void kana_output_set_marker_handler(kana_output_marker_cb_t cb) { marker_cb = cb; }

int kana_output_add_marker(const struct zmk_behavior_binding_event *event) {
    // 「離す」などの再試行を待っている送信があれば、それより先に目印が処理されないよう登録しない
    if (release_pending()) {
        return -EBUSY;
    }
    // キューの処理が待ち時間で止まっていなければ、この中で目印がすぐ処理されて marker_cb が呼ばれる
    if (zmk_behavior_queue_add(event, sync_binding(SYNC_MARKER), true, 0) < 0) {
        return -ENOSPC;
    }
    return 0;
}

void kana_output_on_sync(uint32_t token) {
    switch (token) {
    case SYNC_MARKER:
        if (marker_cb != NULL) {
            marker_cb();
        }
        return;
    case SYNC_MASK_ON:
        if (mask_on_done == mask_on_queued) {
            // 並べていない合図（キーマップに直接書いた &kana_sync など）: 数を合わせたまま、何もしない
            return;
        }
        mask_on_done++;
        if (mask_on_cancelled && mask_on_done == mask_on_cancel_seq) {
            // 戻す合図を並べられなかった列（cancel_kana_mask）: 隠さない
            mask_on_cancelled = false;
            return;
        }
        kana_output_mask_mods(KANA_MASK_KANA, KANA_MASKED_MODS);
        return;
    case SYNC_MASK_OFF:
        kana_output_mask_mods(KANA_MASK_KANA, 0);
        return;
    case SYNC_KANA_OFF:
        break;
    default:
        // 0（キーマップに直接書いた &kana_sync 0 など）や未知の番号: 何もしない
        return;
    }
    if (off_stage != KANA_OFF_WAIT_SYNC) {
        return;
    }

    // ここに来た時点で、合図より前に並べた送信（かな・IME オフ）はすべて実行済み。
    // 親指シフト面などを押したまま抜けた場合に備えて、それらも解除する
    // （後で親指を離したときの &mo の解除は、すでに解除済みなので何も起きない）
    for (size_t i = 0; i < off_sub_layers_len; i++) {
        zmk_keymap_layer_deactivate(off_sub_layers[i]); // 有効でなければ何も起きない
    }
    zmk_keymap_layer_deactivate(kana_layer);
    kana_active = false;
    off_stage = KANA_OFF_NONE;
    LOG_DBG("Kana mode off");
}

bool kana_input_enabled(void) { return kana_active && off_stage == KANA_OFF_NONE; }

bool kana_mode_exiting(void) { return off_stage != KANA_OFF_NONE; }

/* かなのレイヤーがほかの操作（Studio で割り当てた &to や &tog など）で解除されたら、かな配列モードも抜けたことにする。
 * そうしないと「かな配列モード中」のまま残り、次の KANA_SW_ON で入り直せなくなる。
 * IME はオンのまま残る（かな配列モードの解除の手順を通らないため）。解除処理中は、合図の処理に任せる。
 * 接続先が変わったときと、トラックパッドのクリック・ジェスチャーの後は、次のかな・記号の前に IME オンを送り直す
 * （ほかの PC・ウィンドウ・入力欄に移って、IME がオフになっているかもしれないため） */
static int kana_output_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) != NULL) {
        if (kana_active && off_stage == KANA_OFF_NONE && !zmk_keymap_layer_active(kana_layer)) {
            kana_active = false;
            LOG_DBG("Kana layer deactivated elsewhere, kana mode off");
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_endpoint_changed(eh) != NULL) {
        char_sent = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL && pos->state && pos->position >= TP_POS_FIRST &&
        pos->position <= TP_POS_LAST) {
        char_sent = false;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kana_output, kana_output_listener);
ZMK_SUBSCRIPTION(kana_output, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(kana_output, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(kana_output, zmk_position_state_changed);

void kana_mode_on(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                  uint32_t tap_ms, uint32_t wait_ms) {
    if (kana_active) {
        return;
    }

    // IME オンを先に並べるので、この後に押したかなは必ず IME オンの後に送られる。
    // 並べられなかったら（キューが満杯、または「離す」の再試行中）、かな配列モードには入らない（もう一度押す）。
    // -EAGAIN は「押す」は並んでいる（「離す」は自動で再試行される）ので、そのまま入る。
    // かなと同じく、押している Shift・Ctrl を隠して送る（Shift+変換などにならないように）。
    // -ENOSPC のときに隠す／戻す合図の対だけが並ぶことがあるが、何も送らないので害はない
    static const uint32_t ime_on_key = LANGUAGE_1;
    const int ret = kana_output_send_keys(event, &ime_on_key, 1, tap_ms, wait_ms, false, 0);
    if (ret == -EBUSY || ret == -ENOSPC) {
        LOG_WRN("Could not queue IME on (err %d), kana mode not entered", ret);
        return;
    }
    // いま IME オンを送ったので、すぐ後のかなの前には送り直さない
    last_char_ms = k_uptime_get();
    char_sent = true;

    kana_layer = layer;
    kana_active = true;
    zmk_keymap_layer_activate(kana_layer);
    LOG_DBG("Kana mode on (layer %d)", kana_layer);
}

void kana_mode_off(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                   const zmk_keymap_layer_id_t *sub_layers, size_t sub_layers_len, uint32_t tap_ms,
                   uint32_t wait_ms) {
    if (off_stage != KANA_OFF_NONE) {
        return;
    }

    if (!kana_active) {
        // かな配列モードではないのに、かなのレイヤーが（Studio で割り当てた &tog などで）有効になっている:
        // &kana_key は何も送らないので、レイヤーだけ解除して抜けられるようにする（有効でなければ何も起きない）
        for (size_t i = 0; i < sub_layers_len; i++) {
            zmk_keymap_layer_deactivate(sub_layers[i]);
        }
        zmk_keymap_layer_deactivate(layer);
        return;
    }

    // この時点から &kana_key の入力は受け付けない（レイヤー解除までの間の入力は捨てる）
    off_stage = KANA_OFF_SEND_IME_OFF;
    off_event = *event;
    off_sub_layers = sub_layers;
    off_sub_layers_len = sub_layers_len;
    off_tap_ms = tap_ms;
    off_wait_ms = wait_ms;
    kana_off_work_handler(&kana_off_work.work);
}
