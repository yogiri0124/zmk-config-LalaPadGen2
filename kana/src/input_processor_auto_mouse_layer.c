/*
 * オートマウスレイヤーの入力処理 (zmk,input-processor-auto-mouse-layer)。
 *
 * トラックパッドのリスナーに <&zip_auto_mouse_layer 4 700> のように入れると、
 * トラックパッドの操作でレイヤー param1 を有効にし、操作が止まって param2 ms たったら解除する。
 * ZMK 標準の zmk,input-processor-temp-layer と同じ使い方で、次の点が違う。
 *
 * - マウスレイヤー上で何か割り当ててあるキー（&trans / &none 以外）と excluded-positions のキーは
 *   「マウス操作」として扱う。押してもレイヤーを解除せず、押している間は時間切れにしない。
 *   離したときから param2 ms を数え直す（クリック → ダブルクリックの間に解除されないように）。
 * - それ以外のキーを押すと、すぐに解除する。
 * - 直前のキー入力から require-prior-idle-ms 以内のトラックパッド操作では有効にしない（打鍵中の誤動作防止）。
 * - 状態はインスタンスごとに持つので、左右のリスナーに別々のインスタンスを入れれば別々に動く。
 *   1つのインスタンスで扱うレイヤーは1つだけ。別のレイヤーを使うリスナーには別のインスタンスを入れること
 *   （有効中に違う param1 が来ても、有効にしたレイヤーのまま扱う）。
 *
 * 入力処理は入力スレッドで呼ばれるので、レイヤーの切り替えはワーク（システムワークキュー）で行う。
 */

#define DT_DRV_COMPAT zmk_input_processor_auto_mouse_layer

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))
#define NONE_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(none))

struct auto_mouse_config {
    int32_t require_prior_idle_ms;
    const uint16_t *excluded_positions;
    size_t num_positions;
};

struct auto_mouse_data {
    const struct device *dev;
    struct k_mutex lock;
    struct k_work activity_work;
    struct k_work_delayable disable_work;

    /* トラックパッドの操作で指定された値（入力スレッドから書く） */
    uint8_t requested_layer;
    uint32_t timeout_ms;

    /* レイヤーの状態 */
    bool active;
    uint8_t layer; /* 有効にしたレイヤーの番号（keymap の宣言順） */
    int64_t last_tapped;

    /* 押しているキー（レイヤーの状態に関係なく記録する） */
    bool pressed[ZMK_KEYMAP_LEN];
    /* そのうち「マウス操作」として扱っているキー。押している間は時間切れにしない */
    bool held[ZMK_KEYMAP_LEN];
    size_t held_count;
};

static bool position_is_excluded(const struct auto_mouse_config *cfg, uint32_t position) {
    for (size_t i = 0; i < cfg->num_positions; i++) {
        if (cfg->excluded_positions[i] == position) {
            return true;
        }
    }
    return false;
}

/* マウスレイヤー上で何か割り当ててあるキーか（&trans / &none は割り当てなしとみなす） */
static bool position_is_bound(uint8_t layer, uint32_t position) {
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_index_to_id(layer), position);
    if (binding == NULL || binding->behavior_dev == NULL) {
        return false;
    }
    return strcmp(binding->behavior_dev, TRANS_BEHAVIOR_NAME) != 0 &&
           strcmp(binding->behavior_dev, NONE_BEHAVIOR_NAME) != 0;
}

static bool is_mouse_key(const struct device *dev, uint32_t position) {
    const struct auto_mouse_config *cfg = dev->config;
    const struct auto_mouse_data *data = dev->data;
    return position_is_excluded(cfg, position) || position_is_bound(data->layer, position);
}

/* 以下はロックを取った状態で呼ぶ */

static void deactivate(struct auto_mouse_data *data) {
    if (!data->active) {
        return;
    }
    data->active = false;
    k_work_cancel_delayable(&data->disable_work);
    zmk_keymap_layer_deactivate(zmk_keymap_layer_index_to_id(data->layer));
    LOG_DBG("Auto mouse layer %d deactivated", data->layer);
}

/* 有効にしたときに、それより前から押しているキーのうちマウス操作のものを数え直す */
static void recount_held(const struct device *dev, struct auto_mouse_data *data) {
    data->held_count = 0;
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
        data->held[pos] = data->pressed[pos] && is_mouse_key(dev, pos);
        if (data->held[pos]) {
            data->held_count++;
        }
    }
}

/* 押しているマウス操作のキーがなければ、時間切れを数え直す */
static void restart_timeout(struct auto_mouse_data *data) {
    if (!data->active) {
        return;
    }
    if (data->held_count > 0) {
        k_work_cancel_delayable(&data->disable_work);
    } else if (data->timeout_ms > 0) {
        k_work_reschedule(&data->disable_work, K_MSEC(data->timeout_ms));
    }
}

/* ---- ワーク（システムワークキュー） ---- */

static void activity_work_cb(struct k_work *work) {
    struct auto_mouse_data *data = CONTAINER_OF(work, struct auto_mouse_data, activity_work);
    const struct auto_mouse_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->active) {
        // 直前にキーを打っていたら有効にしない
        if (cfg->require_prior_idle_ms > 0 &&
            data->last_tapped + cfg->require_prior_idle_ms > k_uptime_get()) {
            k_mutex_unlock(&data->lock);
            return;
        }
        data->layer = data->requested_layer;
        data->active = true;
        recount_held(data->dev, data);
        zmk_keymap_layer_activate(zmk_keymap_layer_index_to_id(data->layer));
        LOG_DBG("Auto mouse layer %d activated", data->layer);
    } else if (data->requested_layer != data->layer) {
        LOG_WRN("Auto mouse: layer %d requested while layer %d is active (use separate instances)",
                data->requested_layer, data->layer);
    }
    restart_timeout(data);
    k_mutex_unlock(&data->lock);
}

static void disable_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct auto_mouse_data *data = CONTAINER_OF(d_work, struct auto_mouse_data, disable_work);

    k_mutex_lock(&data->lock, K_FOREVER);
    if (data->held_count == 0) {
        deactivate(data);
    }
    k_mutex_unlock(&data->lock);
}

/* ---- 入力処理（入力スレッド） ---- */

static int auto_mouse_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t param1, uint32_t param2,
                                   struct zmk_input_processor_state *state) {
    if (param1 >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_ERR("Invalid layer index: %d", param1);
        return -EINVAL;
    }

    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    data->requested_layer = param1;
    data->timeout_ms = param2;
    k_mutex_unlock(&data->lock);

    k_work_submit(&data->activity_work);
    return ZMK_INPUT_PROC_CONTINUE;
}

/* ---- キー・レイヤーのイベント ---- */

static void handle_position(const struct device *dev,
                            const struct zmk_position_state_changed *ev) {
    struct auto_mouse_data *data = dev->data;
    if (ev->position >= ZMK_KEYMAP_LEN) {
        return;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    data->pressed[ev->position] = ev->state;
    if (ev->state) {
        if (data->active) {
            if (is_mouse_key(dev, ev->position)) {
                if (!data->held[ev->position]) {
                    data->held[ev->position] = true;
                    data->held_count++;
                }
                restart_timeout(data);
            } else {
                deactivate(data);
            }
        }
    } else if (data->held[ev->position]) {
        data->held[ev->position] = false;
        data->held_count--;
        restart_timeout(data);
    }
    k_mutex_unlock(&data->lock);
}

static void handle_keycode(const struct device *dev, const struct zmk_keycode_state_changed *ev) {
    struct auto_mouse_data *data = dev->data;
    if (!ev->state) {
        return;
    }
    k_mutex_lock(&data->lock, K_FOREVER);
    data->last_tapped = ev->timestamp;
    k_mutex_unlock(&data->lock);
}

static void handle_layer_state(const struct device *dev) {
    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    // ほかの操作（&tog や &to など）でレイヤーが解除されたら、こちらの状態も合わせる
    if (data->active && !zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(data->layer))) {
        data->active = false;
        k_work_cancel_delayable(&data->disable_work);
    }
    k_mutex_unlock(&data->lock);
}

static void dispatch(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        handle_position(dev, pos);
        return;
    }
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        handle_keycode(dev, kc);
        return;
    }
    if (as_zmk_layer_state_changed(eh) != NULL) {
        handle_layer_state(dev);
    }
}

#define AUTO_MOUSE_DISPATCH(n) dispatch(DEVICE_DT_INST_GET(n), eh);

static int auto_mouse_listener(const zmk_event_t *eh) {
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_DISPATCH)
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(auto_mouse_layer, auto_mouse_listener);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_position_state_changed);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_layer_state_changed);

static int auto_mouse_init(const struct device *dev) {
    struct auto_mouse_data *data = dev->data;
    data->dev = dev;
    k_mutex_init(&data->lock);
    k_work_init(&data->activity_work, activity_work_cb);
    k_work_init_delayable(&data->disable_work, disable_work_cb);
    return 0;
}

static struct zmk_input_processor_driver_api auto_mouse_driver_api = {
    .handle_event = auto_mouse_handle_event,
};

#define AUTO_MOUSE_INST(n)                                                                         \
    static struct auto_mouse_data auto_mouse_data_##n;                                             \
    static const uint16_t auto_mouse_excluded_##n[] = DT_INST_PROP(n, excluded_positions);         \
    static const struct auto_mouse_config auto_mouse_config_##n = {                                \
        .require_prior_idle_ms = DT_INST_PROP(n, require_prior_idle_ms),                           \
        .excluded_positions = auto_mouse_excluded_##n,                                             \
        .num_positions = DT_INST_PROP_LEN(n, excluded_positions),                                  \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, auto_mouse_init, NULL, &auto_mouse_data_##n,                          \
                          &auto_mouse_config_##n, POST_KERNEL,                                     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &auto_mouse_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_INST)
