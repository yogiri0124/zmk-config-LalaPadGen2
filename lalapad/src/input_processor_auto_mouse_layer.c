/*
 * オートマウスレイヤーの入力処理 (zmk,input-processor-auto-mouse-layer)。
 *
 * トラックパッドのリスナーに <&zip_auto_mouse_layer> のように入れると、トラックパッドの操作でレイヤー layer を
 * 有効にし、操作が止まって timeout-ms たったら解除する（layer・timeout-ms はノードのプロパティ。keymap で、
 * マウスレイヤーの #define と一緒に書けるように、引数にはしない）。
 * ZMK 標準の zmk,input-processor-temp-layer と同じ考え方で、次の点が違う。
 *
 * - マウスレイヤー上で &trans 以外のキー（&none を含む）と excluded-positions のキーは
 *   「マウス操作」として扱う。押してもレイヤーを解除せず、押している間は時間切れにしない。
 *   離したときから timeout-ms を数え直す（クリック → ダブルクリックの間に解除されないように）。
 *   &none のキーは「マウスレイヤーの間は何もしないキー」になる。
 *   ただし、マウスレイヤーより上の有効なレイヤー（かなのレイヤーなど）に &trans 以外があるキーは、
 *   そちらが働くので「マウス操作」ではない（excluded-positions のキーを除く）。
 * - passive-positions のキーは、マウスレイヤーに割り当てがあって働く場合でも「マウス操作」として扱わない。
 *   押しても解除せず、時間切れも延ばさない（押している間も時間切れを数え続ける）。マウスレイヤーで &trans など、
 *   下や上のレイヤーのキーが働く場合は、ほかのキーと同じく、押すとすぐに解除する。
 * - 「マウス操作」でないキーを押すと、すぐに解除する（&trans のキーは下のレイヤーのキーとして入力される）。
 * - 有効にする前から押していたキーは、excluded-positions のキー（トラックパッドのクリックなど）だけを「マウス操作」として数える
 *   （ほかのキーは下のレイヤーのキーとして押されているので、押したままでもレイヤーを保たない）。
 * - 有効にするきっかけは、カーソル移動・スクロール（値が 0 でない REL）だけ。タップ・ジェスチャー
 *   （クリックの直後に打ち始めても文字がマウスレイヤーのキーにならないように）や、触れただけでは有効にしない。
 * - touch-devices に指定した入力デバイスから INPUT_BTN_TOUCH（トラックパッドに触れている／離れた）が届く場合、
 *   触れている間は時間切れにしない。離れてから timeout-ms で解除する（触れただけでは有効にしない）。
 *   そのデバイスからの入力は、触れている間のものだけを有効化・延長のきっかけにする
 *   （指を離した後の慣性による移動では延長しない。離してすぐの打鍵がクリックにならないように）。
 *   触れている合図はリスナーを通さず入力デバイスから直接受け取るので、どのレイヤーでも取りこぼさない。
 *   「離れた」が届かなかったときに備え、そのデバイスから touch-stale-ms の間なにも届かなければ離れたとみなす。
 *   この場合は触れているか「分からない」状態として、次に入力が届いたら触れているとみなし直す
 *   （指を置いたまま止めていただけなら、動かせばまた使えるように）。
 *   右（子機）との接続が切れたときは、子機からの中継（zmk,input-split）のデバイスを、すぐに離れたとみなす
 *   （「離れた」は届かないので、touch-stale-ms を待つと、その間マウスレイヤーが残り、打った文字がマウスレイヤーの
 *   キーになるため）。
 *   「触れた」が届かなかったとき（子機からの中継は取りこぼすことがある）に備え、触れていないはずの中継のデバイスから
 *   TOUCH_GAP_MS より長く途切れた後に動きが届いたら、触れているとみなす（指を離した後の慣性は途切れずに届く）。
 *   推測なので、TOUCH_INFERRED_STALE_MS の間なにも届かなければ離れたとみなす（止まっていた慣性の再開を取り違えても、
 *   マウスレイヤーが長く残らないように）。
 * - 直前のキー入力から require-prior-idle-ms 以内のトラックパッド操作では有効にしない（打鍵中の誤動作防止）。
 *   キー入力は、キーコードの「押す」と、トラックパッド由来でない位置の「押す」（レイヤーのキー、Alt コードの
 *   キーなど、キーコードを出さないものも含める。Alt コードが自分で送るキーは、この listener に届かないため）。
 *   Alt コードを送っている間も有効にしない（alt_code_is_sending）。
 * - excluded-layers のレイヤーが有効な間は、有効にしない（数字・記号レイヤーの間にトラックパッドを触っても、
 *   マウスレイヤーにしない。有効化を予約したときと、実際に有効にするときの両方で確かめる）。
 * - 状態はインスタンスごとに持つので、左右のリスナーに別々のインスタンスを入れれば別々に動く。
 *   1つのインスタンスで扱うレイヤーは、layer の1つだけ。別のレイヤーを使うリスナーには別のインスタンスを入れること。
 * - ほかの操作（&mo・&tog など）ですでに有効なレイヤーは、引き取らない（自分の時間切れで解除しない）。
 *
 * - マウスレイヤーを固定しているとき（zmk,behavior-layer-lock のキー。layer_lock.h）は、時間切れでも、
 *   「マウス操作」でないキーを押しても解除しない（&trans のキーは、レイヤーを残したまま下のレイヤーのキーとして
 *   入力される）。固定をやめると、ふだんどおり時間を数え直す（auto_mouse_layer_unlocked）。
 *
 * 入力処理は入力スレッドで呼ばれるので、レイヤーの切り替えはワーク（システムワークキュー）で行う。
 * 「マウス操作」でないキーでの解除は、キーの出来事の listener（auto_mouse_listener。システムワークキュー）の中で行う。
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

#include "alt_code.h"
#include "auto_mouse_layer.h"
#include "layer_lock.h"
#include "split_link.h"
#include "tp_positions.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))

/* touch-devices に指定できる入力デバイスの数 */
#define MAX_TOUCH_DEVICES 8
/* 触れていないはずのデバイスからの入力が、これより長く途切れた後に届いたら、「触れた」を取りこぼしたとみなす
 * （ドライバは、指を離した後の慣性を 10 ms ごとに途切れずに送る） */
#define TOUCH_GAP_MS 300
/* 「触れた」の取りこぼしとみなして触れていることにしたデバイスは、この時間なにも届かなければ離れたとみなす
 * （止まっていた慣性が再開したのを、新しい触れ方と取り違えたときに、マウスレイヤーが長く残らないように） */
#define TOUCH_INFERRED_STALE_MS 500

/* 初期化の優先度。touch-devices（トラックパッドのドライバ・左右の中継）より後にする必要がある
 * （Zephyr のビルド時チェック）。ロックとワークは静的に初期化しているので、これより前に入力が届いても動く */
#define AUTO_MOUSE_INIT_PRIORITY 99

struct auto_mouse_config {
    /* 有効にするレイヤーの ID（keymap の宣言順の番号）と、解除までの時間（0 なら時間では解除しない） */
    zmk_keymap_layer_id_t layer;
    uint32_t timeout_ms;
    int32_t require_prior_idle_ms;
    const zmk_keymap_layer_id_t *excluded_layers;
    size_t num_excluded_layers;
    const uint16_t *excluded_positions;
    size_t num_excluded_positions;
    const uint16_t *passive_positions;
    size_t num_passive_positions;
    uint32_t touch_stale_ms;
    const struct device *const *touch_devs;
    size_t num_touch_devs;
    /* touch_devs のうち、子機からの中継（zmk,input-split）のもの（touch-devices の順番のビット） */
    uint8_t touch_split_mask;
};

struct auto_mouse_data {
    const struct device *dev;
    struct k_mutex lock;
    struct k_work activity_work;
    /* 「マウス操作」でないキーを押したので、その出来事をほかの listener に流し終えたら解除する */
    bool deactivate_pending;
    struct k_work_delayable disable_work;

    /* レイヤーの状態 */
    bool active;
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
    /* 「触れた」を受け取らずに、途切れた後の動きから触れているとみなしたデバイス（TOUCH_INFERRED_STALE_MS で切れる） */
    uint8_t touch_inferred_mask;
    int64_t touch_last_ms[MAX_TOUCH_DEVICES];
    /* そのデバイスから最後に何かの入力が届いた時刻（触れているかに関係なく。「触れた」の取りこぼしを見る） */
    int64_t touch_input_ms[MAX_TOUCH_DEVICES];
};

static bool position_is_excluded(const struct auto_mouse_config *cfg, uint32_t position) {
    for (size_t i = 0; i < cfg->num_excluded_positions; i++) {
        if (cfg->excluded_positions[i] == position) {
            return true;
        }
    }
    return false;
}

static bool position_is_passive(const struct auto_mouse_config *cfg, uint32_t position) {
    for (size_t i = 0; i < cfg->num_passive_positions; i++) {
        if (cfg->passive_positions[i] == position) {
            return true;
        }
    }
    return false;
}

/* 有効にするレイヤーの ID（layer。keymap の宣言順の番号。&mo などと同じく ID として扱うので、
 * ZMK Studio でレイヤーの順番を入れ替えても同じレイヤーを指す） */
static zmk_keymap_layer_id_t mouse_layer_id(const struct auto_mouse_data *data) {
    return ((const struct auto_mouse_config *)data->dev->config)->layer;
}

/* そのレイヤーで &trans 以外が割り当ててあるキーか（&none も割り当てありとみなす。
 * keymap で &none にした位置は、マウスレイヤーの間は押しても何もせず、レイヤーも解除しない） */
static bool position_is_bound(zmk_keymap_layer_id_t layer, uint32_t position) {
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(layer, position);
    if (binding == NULL || binding->behavior_dev == NULL) {
        return false;
    }
    return strcmp(binding->behavior_dev, TRANS_BEHAVIOR_NAME) != 0;
}

/* そのキーを押したとき、マウスレイヤーの割り当てが働くか（マウスレイヤーより上の有効なレイヤーに
 * &trans 以外があれば、そちらが働く） */
static bool mouse_layer_wins(zmk_keymap_layer_id_t mouse_layer, uint32_t position) {
    for (int i = ZMK_KEYMAP_LAYERS_LEN - 1; i >= 0; i--) {
        const zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(i);
        if (id == mouse_layer) {
            return true;
        }
        if (id != ZMK_KEYMAP_LAYER_ID_INVAL && zmk_keymap_layer_active(id) &&
            position_is_bound(id, position)) {
            return false;
        }
    }
    return true;
}

/* excluded-layers のどれかが有効か（有効なら、マウスレイヤーを新しく有効にしない） */
static bool excluded_layer_active(const struct auto_mouse_config *cfg) {
    for (size_t i = 0; i < cfg->num_excluded_layers; i++) {
        if (zmk_keymap_layer_active(cfg->excluded_layers[i])) {
            return true;
        }
    }
    return false;
}

/* マウスレイヤーの割り当てが働くキーか（&trans でなく、上の有効なレイヤーに隠されていない） */
static bool mouse_binding_works(const struct device *dev, uint32_t position) {
    const struct auto_mouse_config *cfg = dev->config;
    return position_is_bound(cfg->layer, position) && mouse_layer_wins(cfg->layer, position);
}

static bool is_mouse_key(const struct device *dev, uint32_t position) {
    const struct auto_mouse_config *cfg = dev->config;
    return position_is_excluded(cfg, position) ||
           (!position_is_passive(cfg, position) && mouse_binding_works(dev, position));
}

/* 押しても解除も延長もしないキーか（passive-positions で、マウスレイヤーの割り当てが働くもの） */
static bool is_passive_key(const struct device *dev, uint32_t position) {
    const struct auto_mouse_config *cfg = dev->config;
    return !position_is_excluded(cfg, position) && position_is_passive(cfg, position) &&
           mouse_binding_works(dev, position);
}

/* 有効にしたレイヤーを固定しているか（zmk,behavior-layer-lock） */
static bool is_locked(const struct auto_mouse_data *data) {
    return data->active && layer_lock_is_locked(mouse_layer_id(data));
}

/* 以下はロックを取った状態で呼ぶ */

static void deactivate(struct auto_mouse_data *data) {
    if (!data->active) {
        return;
    }
    data->active = false;
    k_work_cancel_delayable(&data->disable_work);
    zmk_keymap_layer_deactivate(mouse_layer_id(data));
    LOG_DBG("Auto mouse layer %d deactivated", mouse_layer_id(data));
}

/* 有効にしたときに、それより前から押しているキーのうち excluded-positions のもの（トラックパッドのクリックなど）を数え直す
 * （ほかのキーは下のレイヤーのキーとして押されているので数えない） */
static void recount_held(const struct device *dev, struct auto_mouse_data *data) {
    const struct auto_mouse_config *cfg = dev->config;
    data->held_count = 0;
    for (uint32_t pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
        data->held[pos] = data->pressed[pos] && position_is_excluded(cfg, pos);
        if (data->held[pos]) {
            data->held_count++;
        }
    }
}

/* 「触れている」が古くなるまでの時間（0 なら古くならない）。推測で触れていることにしたデバイスは短い */
static uint32_t touch_stale_limit(const struct auto_mouse_config *cfg,
                                  const struct auto_mouse_data *data, int idx) {
    return (data->touch_inferred_mask & BIT(idx)) ? TOUCH_INFERRED_STALE_MS : cfg->touch_stale_ms;
}

/* 古くなった（その時間なにも届いていない）「触れている」デバイスを、離れたとみなす。
 * 「離れた」を受け取っていたデバイスは、触れているかわからない状態にして、次の入力で触れているに戻す。
 * 推測で触れていることにしたデバイスは、ただ離れたとみなす（慣性を取り違えていたら、慣性では戻さない） */
static void expire_stale_touches(const struct auto_mouse_config *cfg,
                                 struct auto_mouse_data *data) {
    const int64_t now = k_uptime_get();
    for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
        const uint32_t limit = touch_stale_limit(cfg, data, i);
        if ((data->touch_mask & BIT(i)) && limit > 0 && now - data->touch_last_ms[i] >= limit) {
            data->touch_mask &= ~BIT(i);
            if (data->touch_inferred_mask & BIT(i)) {
                data->touch_inferred_mask &= ~BIT(i);
                LOG_DBG("Auto mouse: inferred touch on device %d expired", i);
            } else {
                data->touch_unknown_mask |= BIT(i);
                LOG_WRN("Auto mouse: no input from touch device %d for %u ms, treating as released",
                        i, limit);
            }
        }
    }
}

/* 押しているマウス操作のキーがなく、トラックパッドにも触れていなければ、時間切れを数え直す */
static void restart_timeout(const struct auto_mouse_config *cfg, struct auto_mouse_data *data) {
    if (!data->active) {
        return;
    }
    if (is_locked(data)) {
        // 固定している間は時間では解除しない
        k_work_cancel_delayable(&data->disable_work);
        return;
    }
    if (data->held_count > 0) {
        // 押している間は解除しない（すでにキューに入った解除ワークは held_count を見て何もしない）
        data->deadline = INT64_MAX;
        k_work_cancel_delayable(&data->disable_work);
    } else if (data->touch_mask != 0) {
        // 触れている間は解除しない。「離れた」の取りこぼしに備えて、最も早く古くなる時刻に確かめる
        int64_t check_at = INT64_MAX;
        for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
            const uint32_t limit = touch_stale_limit(cfg, data, i);
            if ((data->touch_mask & BIT(i)) && limit > 0) {
                check_at = MIN(check_at, data->touch_last_ms[i] + limit);
            }
        }
        data->deadline = check_at;
        if (check_at == INT64_MAX) {
            k_work_cancel_delayable(&data->disable_work);
        } else {
            k_work_reschedule(&data->disable_work, K_MSEC(MAX(check_at - k_uptime_get(), 0)));
        }
    } else if (cfg->timeout_ms > 0) {
        data->deadline = k_uptime_get() + cfg->timeout_ms;
        k_work_reschedule(&data->disable_work, K_MSEC(cfg->timeout_ms));
    } else {
        // 時間では解除しない（触れている間の確かめの予約が残っていたら取り消す。取り消せずに動いた解除ワークは、
        // 期限がないのを見て何もしない）
        data->deadline = INT64_MAX;
        k_work_cancel_delayable(&data->disable_work);
    }
}

/* ---- ワーク（システムワークキュー） ---- */

static void activity_work_cb(struct k_work *work) {
    struct auto_mouse_data *data = CONTAINER_OF(work, struct auto_mouse_data, activity_work);
    const struct auto_mouse_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (!data->active) {
        // 有効にしないレイヤーにいたら有効にしない（予約の後に、そのレイヤーに入ったときのため）
        if (excluded_layer_active(cfg)) {
            k_mutex_unlock(&data->lock);
            return;
        }
        // 直前にキーを打っていたら有効にしない。Alt コードを送っている間も、打っている最中とみなす（自分で送る
        // キーはこの listener に届かず、送信は require-prior-idle-ms より長くかかることがあるため）
        if ((cfg->require_prior_idle_ms > 0 &&
             data->last_tapped + cfg->require_prior_idle_ms > k_uptime_get()) ||
            alt_code_is_sending()) {
            k_mutex_unlock(&data->lock);
            return;
        }
        // ほかの操作（&mo・&tog など）ですでに有効なら、引き取らない（自分の時間切れで、そのレイヤーを解除しない）
        if (zmk_keymap_layer_active(cfg->layer)) {
            k_mutex_unlock(&data->lock);
            return;
        }
        data->active = true;
        recount_held(data->dev, data);
        zmk_keymap_layer_activate(cfg->layer);
        LOG_DBG("Auto mouse layer %d activated", cfg->layer);
    }
    restart_timeout(cfg, data);
    k_mutex_unlock(&data->lock);
}

static void disable_work_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct auto_mouse_data *data = CONTAINER_OF(d_work, struct auto_mouse_data, disable_work);
    const struct auto_mouse_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    if (is_locked(data)) {
        // 固定している（固定の前に予約されていた解除ワークが遅れて動いた）
    } else if (data->active && data->held_count == 0 && data->touch_mask != 0) {
        // 触れている間は解除しない。古くなった「触れている」を外し、残りの状態で時間切れを数え直す
        expire_stale_touches(cfg, data);
        restart_timeout(cfg, data);
    } else if (data->active && data->held_count == 0 && data->deadline != INT64_MAX) {
        // 延長の前に予約されていた解除ワークが遅れて動いた場合は、残り時間で予約し直す
        // （期限がない（時間では解除しない）ときは何もしない）
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
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    // きっかけはカーソル移動・スクロールだけ（タップ・ジェスチャー、触れている／離れたの合図ではない。
    // 触れている／離れたは touch-devices から直接受け取る）
    if (event->type != INPUT_EV_REL || event->value == 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    // 有効にしないレイヤーにいる間は、きっかけにしない
    if (excluded_layer_active(dev->config)) {
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

    k_mutex_unlock(&data->lock);

    k_work_submit(&data->activity_work);
    return ZMK_INPUT_PROC_CONTINUE;
}

/* ---- トラックパッドに触れているか（入力デバイスから直接受け取る。入力スレッド） ---- */

static void handle_touch_input(const struct device *dev, int idx, struct input_event *evt) {
    struct auto_mouse_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);
    const int64_t now = k_uptime_get();
    const int64_t gap = now - data->touch_input_ms[idx];
    data->touch_input_ms[idx] = now;
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH) {
        if (evt->value) {
            data->touch_mask |= BIT(idx);
        } else {
            data->touch_mask &= ~BIT(idx);
        }
        data->touch_unknown_mask &= ~BIT(idx);
        data->touch_inferred_mask &= ~BIT(idx);
        data->touch_last_ms[idx] = now;
        restart_timeout(dev->config, data);
    } else if (data->touch_unknown_mask & BIT(idx)) {
        // 古くなって離れたとみなしていたが、「離れた」は届いていない。入力が来たので触れているに戻す
        data->touch_unknown_mask &= ~BIT(idx);
        data->touch_mask |= BIT(idx);
        data->touch_last_ms[idx] = now;
        restart_timeout(dev->config, data);
    } else if (data->touch_mask & BIT(idx)) {
        // 触れている間に届いた入力（カーソル移動など）で、まだ触れていることがわかる
        data->touch_last_ms[idx] = now;
    } else if (evt->type == INPUT_EV_REL && evt->value != 0 && gap > TOUCH_GAP_MS &&
               (((const struct auto_mouse_config *)dev->config)->touch_split_mask & BIT(idx))) {
        // 子機からの中継で、触れていないはずなのに、途切れた後に動きが届いた: 「触れた」を取りこぼしたとみなす
        // （直接つながったトラックパッドは「触れた」を取りこぼさない）。推測なので、短い時間で切れるようにする
        LOG_DBG("Auto mouse: motion from touch device %d after %lld ms, treating as touched", idx,
                gap);
        data->touch_mask |= BIT(idx);
        data->touch_inferred_mask |= BIT(idx);
        data->touch_last_ms[idx] = now;
        restart_timeout(dev->config, data);
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
        if ((ev->position < TP_POS_FIRST || ev->position > TP_POS_LAST) &&
            ev->timestamp > data->last_tapped) {
            data->last_tapped = ev->timestamp;
        }
        if (data->active) {
            if (is_mouse_key(dev, ev->position)) {
                if (!data->held[ev->position]) {
                    data->held[ev->position] = true;
                    data->held_count++;
                }
                restart_timeout(dev->config, data);
            } else if (!is_locked(data) && !is_passive_key(dev, ev->position)) {
                // この出来事をほかの listener（ZMK の combo・keymap）に流し終えてから、すぐに解除する
                // （auto_mouse_listener）。combo の判定中に預かっていた、マウスレイヤーで押したキーが、
                // 解除の後に処理されて下のレイヤーのキーとして入力されないように。押したこのキーは、
                // マウスレイヤーで &trans か、上のレイヤーのキーが働くので、解除の前でも後でも同じキーになる。
                // ワークで後から解除すると、同じ処理でまとめて届いた次のキーがマウスレイヤーで処理されてしまう
                data->deactivate_pending = true;
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
    // 新しいほうだけを残す（hold-tap が決まったときのキーは、押した時刻で届くので、後から押した位置より古いことがある）
    k_mutex_lock(&data->lock, K_FOREVER);
    if (ev->timestamp > data->last_tapped) {
        data->last_tapped = ev->timestamp;
    }
    k_mutex_unlock(&data->lock);
}

static void handle_layer_state(const struct device *dev) {
    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    // ほかの操作（&tog や &to など）でレイヤーが解除されたら、こちらの状態も合わせる。
    // 固定しているレイヤーは、すぐに有効にし直されるので、有効のままとして扱う（behavior_layer_lock.c）
    if (data->active && !zmk_keymap_layer_active(mouse_layer_id(data)) &&
        !layer_lock_is_locked(mouse_layer_id(data))) {
        data->active = false;
        k_work_cancel_delayable(&data->disable_work);
    }
    k_mutex_unlock(&data->lock);
}

/* 固定をやめた（システムワークキュー）: このインスタンスがそのレイヤーを有効にしていれば、時間を数え直す */
static bool unlocked(const struct device *dev, zmk_keymap_layer_id_t layer) {
    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    const bool mine = data->active && mouse_layer_id(data) == layer;
    if (mine) {
        restart_timeout(dev->config, data);
    }
    k_mutex_unlock(&data->lock);
    return mine;
}

#define AUTO_MOUSE_UNLOCKED(n) handled |= unlocked(DEVICE_DT_INST_GET(n), layer);

bool auto_mouse_layer_unlocked(zmk_keymap_layer_id_t layer) {
    bool handled = false;
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_UNLOCKED)
    return handled;
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

/* 解除の予約を取り出す（予約を消す） */
static bool take_deactivate_pending(const struct device *dev) {
    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    const bool pending = data->deactivate_pending;
    data->deactivate_pending = false;
    k_mutex_unlock(&data->lock);
    return pending;
}

static void finish_deactivate(const struct device *dev) {
    struct auto_mouse_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    if (!is_locked(data)) {
        deactivate(data);
    }
    k_mutex_unlock(&data->lock);
}

#define AUTO_MOUSE_DISPATCH(n) dispatch(DEVICE_DT_INST_GET(n), eh);
#define AUTO_MOUSE_TAKE_PENDING(n) pending_mask |= take_deactivate_pending(DEVICE_DT_INST_GET(n)) ? BIT(n) : 0;
#define AUTO_MOUSE_FINISH(n)                                                                       \
    if (pending_mask & BIT(n)) {                                                                   \
        finish_deactivate(DEVICE_DT_INST_GET(n));                                                  \
    }

static int auto_mouse_listener(const zmk_event_t *eh) {
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_DISPATCH)
    // 「マウス操作」でないキーを押した: この出来事を、この listener の次からほかの listener に流し終えてから
    // 解除する（予約は先に取り出して消す。流している間に、ZMK の combo が預かっていたキーをこの listener に
    // もう一度届けても、そのキーで二重に流さないように）
    uint32_t pending_mask = 0;
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_TAKE_PENDING)
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pending_mask == 0 || pos == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    struct zmk_position_state_changed_event copy = copy_raised_zmk_position_state_changed(pos);
    ZMK_EVENT_RELEASE(copy);
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_FINISH)
    return ZMK_EV_EVENT_CAPTURED;
}

ZMK_LISTENER(auto_mouse_layer, auto_mouse_listener);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_position_state_changed);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(auto_mouse_layer, zmk_layer_state_changed);

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
#define AUTO_MOUSE_SPLIT_BIT(node_id, prop, idx)                                                   \
    | (DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node_id, prop, idx), zmk_input_split) ? BIT(idx) : 0)

/* excluded-layers・excluded-positions の値が範囲内か（設定の誤りをビルドで見つける） */
#define AUTO_MOUSE_CHECK_LAYER(node_id, prop, idx)                                                 \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LAYERS_LEN,                       \
                 "auto mouse layer: excluded-layers is out of range");
#define AUTO_MOUSE_CHECK_POSITION(node_id, prop, idx)                                              \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LEN,                              \
                 "auto mouse layer: excluded-positions / passive-positions is out of range");

#define AUTO_MOUSE_INST(n)                                                                         \
    BUILD_ASSERT(DT_INST_PROP(n, layer) >= 0 && DT_INST_PROP(n, layer) < ZMK_KEYMAP_LAYERS_LEN,    \
                 "auto mouse layer: layer is out of range");                                       \
    BUILD_ASSERT(DT_INST_PROP(n, timeout_ms) >= 0, "auto mouse layer: timeout-ms must be >= 0");   \
    BUILD_ASSERT(DT_INST_PROP(n, require_prior_idle_ms) >= 0 &&                                    \
                     DT_INST_PROP(n, touch_stale_ms) >= 0,                                         \
                 "auto mouse layer: require-prior-idle-ms and touch-stale-ms must be >= 0");       \
    BUILD_ASSERT(DT_INST_PROP_LEN_OR(n, touch_devices, 0) <= MAX_TOUCH_DEVICES,                    \
                 "too many touch-devices");                                                        \
    DT_INST_FOREACH_PROP_ELEM(n, excluded_layers, AUTO_MOUSE_CHECK_LAYER)                          \
    DT_INST_FOREACH_PROP_ELEM(n, excluded_positions, AUTO_MOUSE_CHECK_POSITION)                    \
    DT_INST_FOREACH_PROP_ELEM(n, passive_positions, AUTO_MOUSE_CHECK_POSITION)                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, touch_devices),                                           \
                (static const struct device *const auto_mouse_touch_devs_##n[] = {                 \
                     DT_INST_FOREACH_PROP_ELEM(n, touch_devices, AUTO_MOUSE_TOUCH_DEV)};),         \
                ())                                                                                \
    static struct auto_mouse_data auto_mouse_data_##n = {                                          \
        .dev = DEVICE_DT_INST_GET(n),                                                              \
        .lock = Z_MUTEX_INITIALIZER(auto_mouse_data_##n.lock),                                     \
        .activity_work = Z_WORK_INITIALIZER(activity_work_cb),                                     \
        .disable_work = Z_WORK_DELAYABLE_INITIALIZER(disable_work_cb),                             \
    };                                                                                             \
    static const uint16_t auto_mouse_excluded_##n[] = DT_INST_PROP(n, excluded_positions);         \
    static const uint16_t auto_mouse_passive_##n[] = DT_INST_PROP(n, passive_positions);           \
    static const zmk_keymap_layer_id_t auto_mouse_excluded_layers_##n[] = {                        \
        DT_INST_FOREACH_PROP_ELEM_SEP(n, excluded_layers, DT_PROP_BY_IDX, (, ))};                   \
    static const struct auto_mouse_config auto_mouse_config_##n = {                                \
        .layer = DT_INST_PROP(n, layer),                                                           \
        .timeout_ms = DT_INST_PROP(n, timeout_ms),                                                 \
        .require_prior_idle_ms = DT_INST_PROP(n, require_prior_idle_ms),                           \
        .excluded_layers = auto_mouse_excluded_layers_##n,                                         \
        .num_excluded_layers = DT_INST_PROP_LEN(n, excluded_layers),                               \
        .excluded_positions = auto_mouse_excluded_##n,                                             \
        .num_excluded_positions = DT_INST_PROP_LEN(n, excluded_positions),                         \
        .passive_positions = auto_mouse_passive_##n,                                               \
        .num_passive_positions = DT_INST_PROP_LEN(n, passive_positions),                           \
        .touch_stale_ms = DT_INST_PROP(n, touch_stale_ms),                                         \
        .touch_devs = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, touch_devices),                         \
                                  (auto_mouse_touch_devs_##n), (NULL)),                            \
        .num_touch_devs = DT_INST_PROP_LEN_OR(n, touch_devices, 0),                                \
        .touch_split_mask = 0 IF_ENABLED(DT_INST_NODE_HAS_PROP(n, touch_devices),                  \
                                         (DT_INST_FOREACH_PROP_ELEM(n, touch_devices,              \
                                                                    AUTO_MOUSE_SPLIT_BIT))),       \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &auto_mouse_data_##n, &auto_mouse_config_##n,             \
                          POST_KERNEL, AUTO_MOUSE_INIT_PRIORITY, &auto_mouse_driver_api);          \
    IF_ENABLED(DT_INST_NODE_HAS_PROP(n, touch_devices),                                            \
               (DT_INST_FOREACH_PROP_ELEM_VARGS(n, touch_devices, AUTO_MOUSE_TOUCH_CB, n)))

DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_INST)

#if WATCH_SPLIT_LINK
/* 右（子機）との接続が切れた: 子機からの中継のデバイスを、すぐに離れたとみなす（システムワークキュー） */
#define AUTO_MOUSE_DATA_PTR(n) &auto_mouse_data_##n,
static struct auto_mouse_data *const auto_mouse_instances[] = {
    DT_INST_FOREACH_STATUS_OKAY(AUTO_MOUSE_DATA_PTR)};

static void split_lost_work_cb(struct k_work *work) {
    for (size_t i = 0; i < ARRAY_SIZE(auto_mouse_instances); i++) {
        struct auto_mouse_data *data = auto_mouse_instances[i];
        const struct auto_mouse_config *cfg = data->dev->config;
        k_mutex_lock(&data->lock, K_FOREVER);
        if (data->touch_mask & cfg->touch_split_mask) {
            LOG_DBG("Auto mouse: split link lost, treating relayed touch devices as released");
            data->touch_mask &= ~cfg->touch_split_mask;
            restart_timeout(cfg, data);
        }
        data->touch_unknown_mask &= ~cfg->touch_split_mask;
        data->touch_inferred_mask &= ~cfg->touch_split_mask;
        k_mutex_unlock(&data->lock);
    }
}

static K_WORK_DEFINE(split_lost_work, split_lost_work_cb);

static void auto_mouse_split_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (is_split_link(conn)) {
        k_work_submit(&split_lost_work);
    }
}

BT_CONN_CB_DEFINE(auto_mouse_split_callbacks) = {
    .disconnected = auto_mouse_split_disconnected,
};
#endif
