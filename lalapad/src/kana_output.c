/*
 * かな入力モジュールの共通部分。
 *
 * - 送信はすべて ZMK の behavior queue（マクロと同じ仕組み）に &kp として並べ、順番を保つ。
 * - キューが満杯で「離す」を登録できなかったときは、登録できるまで再試行する（押しっぱなし防止）。
 * - かな配列モードのオフは「受付停止 → IME オフを並べる → 完了の合図 (&kana_sync) を並べる →
 *   合図がキューで実際に処理されたらレイヤー解除」の順で行う。時間の見積もりには頼らないので、
 *   処理が遅れても、解除前後の入力がローマ字のまま送られたり、残りのかなを追い越したりしない。
 * - 同じ合図の仕組みを、ほかのモジュール（Alt コード）にも「それより前に並べた送信がすべて終わったら
 *   知らせる」目印として貸す（kana_output_add_marker）。合図の番号で、どちらの合図かを見分ける。
 *
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので、状態の排他は不要。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>

#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KP_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kp))
#define SYNC_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kana_sync))
#define RETRY_INTERVAL_MS 10
/* 合図の番号（&kana_sync の引数）。かなモードの解除の合図は同時に1つしかない（解除中は解除も入り直しも
 * 受け付けない）。目印は並べた順に処理されるので、どの目印かは呼ぶ側が順番で分かる */
#define SYNC_KANA_OFF 0
#define SYNC_MARKER 1

/* ---- 送信キュー ---- */

/* 「押す」は登録済みで、キューが満杯のため「離す」をまだ登録できていないキー（登録できるまで順に再試行する）。
 * 押しっぱなしのキー（スペースなど）の「離す」はキー位置ごとに1つまで、タップの「離す」は再試行中に次の
 * タップを受け付けない（-EBUSY）ので1つまで。全キー位置に押しっぱなしのキーを置いても足りる数にする
 * （足りないと、下の「その場で離す」が、まだキューにある「押す」より先に実行されてしまう） */
#define PENDING_RELEASE_MAX (ZMK_KEYMAP_LEN + 2)

struct pending_release {
    uint32_t keycode;
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

static void add_pending_release(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                                uint32_t wait) {
    if (pending_count >= PENDING_RELEASE_MAX) {
        // 上の数え方では起きない（配列の範囲を守るための確認）。押しっぱなしにはせず、その場で離す
        LOG_ERR("Too many pending releases, releasing 0x%08X immediately", keycode);
        raise_zmk_keycode_state_changed_from_encoded(keycode, false, k_uptime_get());
        return;
    }
    pending_releases[pending_count++] = (struct pending_release){
        .keycode = keycode,
        .event = *event,
        .wait = wait,
    };
    k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
}

static void retry_release_work_handler(struct k_work *work) {
    // 先に失敗したものから順に登録する
    while (pending_count > 0) {
        struct pending_release *p = &pending_releases[0];
        if (zmk_behavior_queue_add(&p->event, kp_binding(p->keycode), false, p->wait) < 0) {
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
        add_pending_release(event, keycode, wait_ms);
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
    const struct zmk_behavior_binding kp = kp_binding(keycode);

    // 先に待っている「離す」があれば、順番を守るためその後ろに並べる
    if (release_pending() || zmk_behavior_queue_add(event, kp, false, wait_ms) < 0) {
        add_pending_release(event, keycode, wait_ms);
    }
}

/* ---- IME オンの送り直し ---- */

/* 最後にかな・記号を送った（kana_output_ime_on を呼んだ）時刻 */
static int64_t last_char_ms;
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
        const int ret = kana_output_tap(&off_event, LANGUAGE_2, off_tap_ms, off_wait_ms);
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
        struct zmk_behavior_binding sync = {
            .behavior_dev = SYNC_BEHAVIOR_NAME,
            .param1 = SYNC_KANA_OFF,
        };
        // キューが空だと zmk_behavior_queue_add の中で合図がすぐ処理されるので、先に待ち状態にする
        off_stage = KANA_OFF_WAIT_SYNC;
        if (zmk_behavior_queue_add(&off_event, sync, true, 0) < 0) {
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
    // 「離す」の再試行を待っている送信があれば、それより先に目印が処理されないよう登録しない
    if (release_pending()) {
        return -EBUSY;
    }
    struct zmk_behavior_binding sync = {
        .behavior_dev = SYNC_BEHAVIOR_NAME,
        .param1 = SYNC_MARKER,
    };
    // キューが空なら、この中で目印がすぐ処理されて marker_cb が呼ばれる
    if (zmk_behavior_queue_add(event, sync, true, 0) < 0) {
        return -ENOSPC;
    }
    return 0;
}

void kana_output_on_sync(uint32_t token) {
    if (token == SYNC_MARKER) {
        if (marker_cb != NULL) {
            marker_cb();
        }
        return;
    }
    if (token != SYNC_KANA_OFF || off_stage != KANA_OFF_WAIT_SYNC) {
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
 * IME はオンのまま残る（かな配列モードの解除の手順を通らないため）。解除処理中は、合図の処理に任せる */
static int kana_layer_listener(const zmk_event_t *eh) {
    if (kana_active && off_stage == KANA_OFF_NONE && !zmk_keymap_layer_active(kana_layer)) {
        kana_active = false;
        LOG_DBG("Kana layer deactivated elsewhere, kana mode off");
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kana_output, kana_layer_listener);
ZMK_SUBSCRIPTION(kana_output, zmk_layer_state_changed);

void kana_mode_on(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                  uint32_t tap_ms, uint32_t wait_ms) {
    if (kana_active) {
        return;
    }

    // IME オンを先に並べるので、この後に押したかなは必ず IME オンの後に送られる。
    // 並べられなかったら（キューが満杯、または「離す」の再試行中）、かな配列モードには入らない（もう一度押す）。
    // -EAGAIN は「押す」は並んでいる（「離す」は自動で再試行される）ので、そのまま入る
    const int ret = kana_output_tap(event, LANGUAGE_1, tap_ms, wait_ms);
    if (ret == -EBUSY || ret == -ENOSPC) {
        LOG_WRN("Could not queue IME on (err %d), kana mode not entered", ret);
        return;
    }

    kana_layer = layer;
    kana_active = true;
    zmk_keymap_layer_activate(kana_layer);
    LOG_DBG("Kana mode on (layer %d)", kana_layer);
}

void kana_mode_off(const struct zmk_behavior_binding_event *event,
                   const zmk_keymap_layer_id_t *sub_layers, size_t sub_layers_len, uint32_t tap_ms,
                   uint32_t wait_ms) {
    if (!kana_active || off_stage != KANA_OFF_NONE) {
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
