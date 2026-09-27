/*
 * かな入力モジュールの共通部分。
 *
 * - 送信はすべて ZMK の behavior queue（マクロと同じ仕組み）に &kp として並べ、順番を保つ。
 * - キューが満杯で「離す」を登録できなかったときは、登録できるまで再試行する（押しっぱなし防止）。
 * - かな配列モードのオフは「受付停止 → IME オフを並べる → 完了の合図 (&kana_sync) を並べる →
 *   合図がキューで実際に処理されたらレイヤー解除」の順で行う。時間の見積もりには頼らないので、
 *   処理が遅れても、解除前後の入力がローマ字のまま送られたり、残りのかなを追い越したりしない。
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
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/keys.h>

#include "kana_output.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KP_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kp))
#define SYNC_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kana_sync))
#define RETRY_INTERVAL_MS 10

/* ---- 送信キュー ---- */

/* 「押す」は登録済みで、キューが満杯のため「離す」をまだ登録できていないキー（登録できるまで順に再試行する） */
#define PENDING_RELEASE_MAX 8

struct pending_release {
    struct zmk_behavior_binding binding;
    struct zmk_behavior_binding_event event;
    uint32_t wait;
};

static struct pending_release pending_releases[PENDING_RELEASE_MAX];
static size_t pending_count;

static void retry_release_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_release_work, retry_release_work_handler);

static bool release_pending(void) { return pending_count > 0; }

static void add_pending_release(const struct zmk_behavior_binding_event *event,
                                struct zmk_behavior_binding binding, uint32_t wait) {
    if (pending_count >= PENDING_RELEASE_MAX) {
        // 通常は起きない。押しっぱなしを避けるため、順番より解放を優先してその場で離す
        LOG_ERR("Too many pending releases, releasing 0x%08X immediately", binding.param1);
        raise_zmk_keycode_state_changed_from_encoded(binding.param1, false, k_uptime_get());
        return;
    }
    pending_releases[pending_count++] = (struct pending_release){
        .binding = binding,
        .event = *event,
        .wait = wait,
    };
    k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
}

static void retry_release_work_handler(struct k_work *work) {
    // 先に失敗したものから順に登録する
    while (pending_count > 0) {
        struct pending_release *p = &pending_releases[0];
        if (zmk_behavior_queue_add(&p->event, p->binding, false, p->wait) < 0) {
            k_work_schedule(&retry_release_work, K_MSEC(RETRY_INTERVAL_MS));
            return;
        }
        pending_count--;
        memmove(&pending_releases[0], &pending_releases[1],
                pending_count * sizeof(pending_releases[0]));
    }
}

static struct zmk_behavior_binding kp_binding(uint32_t keycode) {
    return (struct zmk_behavior_binding){
        .behavior_dev = KP_BEHAVIOR_NAME,
        .param1 = keycode,
    };
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
        add_pending_release(event, kp, wait_ms);
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
        add_pending_release(event, kp, wait_ms);
    }
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
static uint32_t off_tap_ms;
static uint32_t off_wait_ms;
static uint32_t sync_token;

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
        sync_token++;
        struct zmk_behavior_binding sync = {
            .behavior_dev = SYNC_BEHAVIOR_NAME,
            .param1 = sync_token,
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

void kana_output_on_sync(uint32_t token) {
    if (off_stage != KANA_OFF_WAIT_SYNC || token != sync_token) {
        return;
    }

    // ここに来た時点で、合図より前に並べた送信（かな・IME オフ）はすべて実行済み
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
