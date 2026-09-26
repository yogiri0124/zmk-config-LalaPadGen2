/*
 * かな入力モジュールの共通部分。
 *
 * - 送信はすべて ZMK の behavior queue（マクロと同じ仕組み）に &kp として並べ、順番を保つ。
 * - キューが満杯で「離す」を登録できなかったときは、登録できるまで再試行する（押しっぱなし防止）。
 * - かな配列モードのオフは「受付停止 → IME オフを並べる → キューが空になるのを待つ → レイヤー解除」
 *   の順で行い、解除前後の入力がローマ字のまま送られたり、残りのかなを追い越したりしないようにする。
 *
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので、状態の排他は不要。
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KP_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kp))
#define RETRY_INTERVAL_MS 10
/* キューの実際の処理はスケジューリングで少し遅れることがあるので、見積もりに余裕を足す */
#define DRAIN_MARGIN_MS 30

/* ---- 送信キュー ---- */

/* このモジュールが並べた送信がすべて終わる見込み時刻 (k_uptime_get 基準) */
static int64_t busy_until_ms;

static bool release_pending;
static struct zmk_behavior_binding pending_release_binding;
static struct zmk_behavior_binding_event pending_release_event;
static uint32_t pending_release_wait;

static void note_busy(uint32_t ms) {
    const int64_t now = k_uptime_get();
    if (busy_until_ms < now) {
        busy_until_ms = now;
    }
    busy_until_ms += ms;
}

static void retry_release_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_release_work, retry_release_work_handler);

static void retry_release_work_handler(struct k_work *work) {
    if (!release_pending) {
        return;
    }
    if (zmk_behavior_queue_add(&pending_release_event, pending_release_binding, false,
                               pending_release_wait) < 0) {
        k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
        return;
    }
    note_busy(pending_release_wait);
    release_pending = false;
}

int kana_output_tap(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                    uint32_t tap_ms, uint32_t wait_ms) {
    if (release_pending) {
        return -EBUSY;
    }

    struct zmk_behavior_binding kp = {
        .behavior_dev = KP_BEHAVIOR_NAME,
        .param1 = keycode,
    };

    if (zmk_behavior_queue_add(event, kp, true, tap_ms) < 0) {
        return -ENOSPC;
    }
    note_busy(tap_ms);

    if (zmk_behavior_queue_add(event, kp, false, wait_ms) < 0) {
        release_pending = true;
        pending_release_binding = kp;
        pending_release_event = *event;
        pending_release_wait = wait_ms;
        k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
        return -EAGAIN;
    }
    note_busy(wait_ms);

    return 0;
}

/* ---- かな配列モード ---- */

static bool kana_active;
static zmk_keymap_layer_id_t kana_layer;

enum kana_off_stage {
    KANA_OFF_NONE,
    KANA_OFF_SEND_IME_OFF, /* IME オフ (LANGUAGE_2) をキューに並べる */
    KANA_OFF_DRAIN,        /* キューの送信が終わるのを待ってからレイヤーを解除する */
};

static enum kana_off_stage off_stage = KANA_OFF_NONE;
static struct zmk_behavior_binding_event off_event;
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
        off_stage = KANA_OFF_DRAIN;
    }

    if (off_stage != KANA_OFF_DRAIN) {
        return;
    }

    const int64_t remaining = busy_until_ms + DRAIN_MARGIN_MS - k_uptime_get();
    if (release_pending || remaining > 0) {
        k_work_schedule(&kana_off_work, K_MSEC(remaining > 0 ? remaining : RETRY_INTERVAL_MS));
        return;
    }

    zmk_keymap_layer_deactivate(kana_layer);
    kana_active = false;
    off_stage = KANA_OFF_NONE;
    LOG_DBG("Kana mode off");
}

bool kana_input_enabled(void) { return kana_active && off_stage == KANA_OFF_NONE; }

void kana_mode_on(const struct zmk_behavior_binding_event *event, uint8_t layer_index,
                  uint32_t tap_ms, uint32_t wait_ms) {
    if (kana_active) {
        return;
    }

    kana_layer = zmk_keymap_layer_index_to_id(layer_index);
    kana_active = true;

    // IME オンを先に並べるので、この後に押したかなは必ず IME オンの後に送られる
    if (kana_output_tap(event, LANGUAGE_1, tap_ms, wait_ms) < 0) {
        LOG_WRN("Could not queue IME on");
    }
    zmk_keymap_layer_activate(kana_layer);
    LOG_DBG("Kana mode on (layer %d)", kana_layer);
}

void kana_mode_off(const struct zmk_behavior_binding_event *event, uint32_t tap_ms,
                   uint32_t wait_ms) {
    if (!kana_active || off_stage != KANA_OFF_NONE) {
        return;
    }

    // この時点から &kana の入力は受け付けない（レイヤー解除までの間の入力は捨てる）
    off_stage = KANA_OFF_SEND_IME_OFF;
    off_event = *event;
    off_tap_ms = tap_ms;
    off_wait_ms = wait_ms;
    kana_off_work_handler(&kana_off_work.work);
}
