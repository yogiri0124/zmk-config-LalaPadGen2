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
 * - touch-devices に指定した入力デバイスから INPUT_BTN_TOUCH（トラックパッドに触れている／離れた）が届く場合、
 *   触れている間は時間切れにしない。離れてから param2 ms で解除する（触れただけでは有効にしない）。
 *   そのデバイスからの入力は、触れている間のものだけを有効化・延長のきっかけにする
 *   （指を離した後の慣性による移動では延長しない。離してすぐの打鍵がクリックにならないように）。
 *   触れている合図はリスナーを通さず入力デバイスから直接受け取るので、どのレイヤーでも取りこぼさない。
 *   「離れた」が届かなかったときに備え、そのデバイスから touch-stale-ms の間なにも届かなければ離れたとみなす。
 *   この場合は触れているか「分からない」状態として、次に入力が届いたら触れているとみなし直す
 *   （指を置いたまま止めていただけなら、動かせばまた使えるように）。
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
#include <zephyr/input/input.h>
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

/* touch-devices に指定できる入力デバイスの数 */
#define MAX_TOUCH_DEVICES 8

struct auto_mouse_config {
    int32_t require_prior_idle_ms;
    const uint16_t *excluded_positions;
    size_t num_positions;
    uint32_t touch_stale_ms;
    const struct device *const *touch_devs;
    size_t num_touch_devs;
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
    /* 解除する時刻。解除ワークが遅れて動いても、この時刻まではまだ解除しない */
    int64_t deadline;

    /* 押しているキー（レイヤーの状態に関係なく記録する） */
    bool pressed[ZMK_KEYMAP_LEN];
    /* そのうち「マウス操作」として扱っているキー。押している間は時間切れにしない */
    bool held[ZMK_KEYMAP_LEN];
    size_t held_count;

    /* トラックパッドに触れている入力デバイス（touch-devices の順番のビット）と、最後に入力が届いた時刻 */
    uint8_t touch_mask;
    /* touch-stale-ms で離れたとみなしたデバイス。「離れた」を実際には受け取っていないので、次の入力で触れているに戻す */
    uint8_t touch_unknown_mask;
    int64_t touch_last_ms[MAX_TOUCH_DEVICES];
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

/* touch_stale_ms の間なにも届いていない「触れている」デバイスを、離れたとみなす */
static void expire_stale_touches(const struct auto_mouse_config *cfg,
                                 struct auto_mouse_data *data) {
    if (cfg->touch_stale_ms == 0) {
        return;
    }
    const int64_t now = k_uptime_get();
    for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
        if ((data->touch_mask & BIT(i)) && now - data->touch_last_ms[i] >= cfg->touch_stale_ms) {
            data->touch_mask &= ~BIT(i);
            data->touch_unknown_mask |= BIT(i);
            LOG_WRN("Auto mouse: no input from touch device %d for %d ms, treating as released", i,
                    cfg->touch_stale_ms);
        }
    }
}

/* 押しているマウス操作のキーがなく、トラックパッドにも触れていなければ、時間切れを数え直す */
static void restart_timeout(const struct auto_mouse_config *cfg, struct auto_mouse_data *data) {
    if (!data->active) {
        return;
    }
    if (data->held_count > 0) {
        // 押している間は解除しない（すでにキューに入った解除ワークは held_count を見て何もしない）
        k_work_cancel_delayable(&data->disable_work);
    } else if (data->touch_mask != 0) {
        // 触れている間は解除しない。「離れた」の取りこぼしに備えて、最も早く古くなる時刻に確かめる
        if (cfg->touch_stale_ms == 0) {
            k_work_cancel_delayable(&data->disable_work);
            return;
        }
        int64_t check_at = INT64_MAX;
        for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
            if (data->touch_mask & BIT(i)) {
                check_at = MIN(check_at, data->touch_last_ms[i] + cfg->touch_stale_ms);
            }
        }
        data->deadline = check_at;
        k_work_reschedule(&data->disable_work, K_MSEC(MAX(check_at - k_uptime_get(), 0)));
    } else if (data->timeout_ms > 0) {
        data->deadline = k_uptime_get() + data->timeout_ms;
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
    restart_timeout(cfg, data);
    k_mutex_unlock(&data->lock);
}

static void disable_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct auto_mouse_data *data = CONTAINER_OF(d_work, struct auto_mouse_data, disable_work);
    const struct auto_mouse_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (data->active && data->held_count == 0 && data->touch_mask != 0) {
        // 触れている間は解除しない。古くなった「触れている」を外し、残りの状態で時間切れを数え直す
        expire_stale_touches(cfg, data);
        restart_timeout(cfg, data);
    } else if (data->active && data->held_count == 0) {
        // 延長の前に予約されていた解除ワークが遅れて動いた場合は、残り時間で予約し直す
        const int64_t remaining = data->deadline - k_uptime_get();
        if (remaining > 0) {
            k_work_reschedule(&data->disable_work, K_MSEC(remaining));
        } else {
            deactivate(data);
        }
    }
    k_mutex_unlock(&data->lock);
}

/* ---- 入力処理（入力スレッド） ---- */

/* touch-devices の何番目か。含まれなければ -1 */
static int touch_device_index(const struct auto_mouse_config *cfg, const struct device *input_dev) {
    for (size_t i = 0; i < cfg->num_touch_devs; i++) {
        if (cfg->touch_devs[i] == input_dev) {
            return (int)i;
        }
    }
    return -1;
}

static int auto_mouse_handle_event(const struct device *dev, struct input_event *event,
                                   uint32_t param1, uint32_t param2,
                                   struct zmk_input_processor_state *state) {
    if (param1 >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_ERR("Invalid layer index: %d", param1);
        return -EINVAL;
    }

    // 触れている／離れたの合図は touch-devices から直接受け取るので、ここでは有効化のきっかけにしない
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);

    // 触れているかを送ってくるデバイスで、いま触れていなければ（指を離した後の慣性など）きっかけにしない
    const int touch_idx = touch_device_index(dev->config, event->dev);
    if (touch_idx >= 0 && !(data->touch_mask & BIT(touch_idx))) {
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    data->requested_layer = param1;
    data->timeout_ms = param2;
    k_mutex_unlock(&data->lock);

    k_work_submit(&data->activity_work);
    return ZMK_INPUT_PROC_CONTINUE;
}

/* ---- トラックパッドに触れているか（入力デバイスから直接受け取る。入力スレッド） ---- */

static void handle_touch_input(const struct device *dev, int idx, struct input_event *evt) {
    struct auto_mouse_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH) {
        if (evt->value) {
            data->touch_mask |= BIT(idx);
        } else {
            data->touch_mask &= ~BIT(idx);
        }
        data->touch_unknown_mask &= ~BIT(idx);
        data->touch_last_ms[idx] = k_uptime_get();
        restart_timeout(dev->config, data);
    } else if (data->touch_unknown_mask & BIT(idx)) {
        // 古くなって離れたとみなしていたが、「離れた」は届いていない。入力が来たので触れているに戻す
        data->touch_unknown_mask &= ~BIT(idx);
        data->touch_mask |= BIT(idx);
        data->touch_last_ms[idx] = k_uptime_get();
        restart_timeout(dev->config, data);
    } else if (data->touch_mask & BIT(idx)) {
        // 触れている間に届いた入力（カーソル移動など）で、まだ触れていることがわかる
        data->touch_last_ms[idx] = k_uptime_get();
    }
    k_mutex_unlock(&data->lock);
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
                restart_timeout(dev->config, data);
            } else {
                deactivate(data);
            }
        }
    } else if (data->held[ev->position]) {
        data->held[ev->position] = false;
        data->held_count--;
        restart_timeout(dev->config, data);
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

#define AUTO_MOUSE_TOUCH_CB(node_id, prop, idx, n)                                                 \
    static void auto_mouse_touch_cb_##n##_##idx(struct input_event *evt) {                         \
        handle_touch_input(DEVICE_DT_INST_GET(n), idx, evt);                                       \
    }                                                                                              \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                    \
                          auto_mouse_touch_cb_##n##_##idx);

#define AUTO_MOUSE_TOUCH_DEV(node_id, prop, idx) DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define AUTO_MOUSE_INST(n)                                                                         \
    BUILD_ASSERT(DT_INST_PROP_LEN_OR(n, touch_devices, 0) <= MAX_TOUCH_DEVICES,                    \
                 "too many touch-devices");                                                        \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, touch_devices),                                           \
                (static const struct device *const auto_mouse_touch_devs_##n[] = {                 \
                     DT_INST_FOREACH_PROP_ELEM(n, touch_devices, AUTO_MOUSE_TOUCH_DEV)};),         \
                ())                                                                                \
    static struct auto_mouse_data auto_mouse_data_##n;                                             \
    static const uint16_t auto_mouse_excluded_##n[] = DT_INST_PROP(n, excluded_positions);         \
    static const struct auto_mouse_config auto_mouse_config_##n = {                                \
        .require_prior_idle_ms = DT_INST_PROP(n, require_prior_idle_ms),                           \
        .excluded_positions = auto_mouse_excluded_##n,                                             \
        .num_positions = DT_INST_PROP_LEN(n, excluded_positions),                                  \
        .touch_stale_ms = DT_INST_PROP(n, touch_stale_ms),                                         \
        .touch_devs = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, touch_devices),                         \
                                  (auto_mouse_touch_devs_##n), (NULL)),                            \
        .num_touch_devs = DT_INST_PROP_LEN_OR(n, touch_devices, 0),                                \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, auto_mouse_init, NULL, &auto_mouse_data_##n,                          \
                          &auto_mouse_config_##n, POST_KERNEL,                                     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &auto_mouse_driver_api);           \
    IF_ENABLED(DT_INST_NODE_HAS_PROP(n, touch_devices),                                            \
               (DT_INST_FOREACH_PROP_ELEM_VARGS(n, touch_devices, AUTO_MOUSE_TOUCH_CB, n)))

DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_INST)
