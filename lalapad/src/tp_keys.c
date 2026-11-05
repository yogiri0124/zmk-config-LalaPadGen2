/*
 * トラックパッドをキー操作に変える入力処理 (zmk,input-processor-tp-keys)。
 *
 * keymap の zmk,behavior-tp-keys・zmk,behavior-tp-stroke のキーを押している間だけ、トラックパッドの動きを
 * カーソル・スクロールの代わりに、決めておいたキー（behavior）の入力に変える。押していない間は何もせず通す。
 * トラックパッドのリスナーの入力処理の並びの先頭に入れる。
 * - タップ・ジェスチャー（BTN_0〜7。trackpad_key_behaviors がキー位置 52〜67 に変える）より前で、押している間の
 *   タップ・ジェスチャーを捨てる（矢印を出そうと軽く触れただけで、クリックにならないように）。捨てたボタンは
 *   「離した」も捨てる。押す前から押されていたボタンの「離した」は通す。
 * - 速度の調整（scaler）より前で、動きの量を、速度の設定に関係なく、生の値で数える。
 * - オートマウスレイヤーより前なので、変えた動きではマウスレイヤーはオンにならない。
 *
 * - zmk,behavior-tp-keys: 指を動かした量 step ごとに、その向きの binding を 1 回送る（矢印キー、ダイヤルなど）。
 *   向きは指を動かした向き（2 本指も、スクロールの向きではなく指の向き）。動き始めの向きで縦か横かを決め、
 *   指が触れ直す・離れるか、モードのキーを押す・離すまで変えない（斜めに動いても混ざらない）。
 *   送るのが追いつかないとき、待っている分は MAX_PENDING_STEPS までにする（指を止めた後に長く出続けないように）。
 *   逆向きに動かしたら、まだ送っていない分は捨てる。
 * - zmk,behavior-tp-stroke: 1 本指で描いた向きの並び（最大 4 つ）を覚え、指を離したとき（または先にキーを
 *   離したとき）に、並びが一致する binding を 1 回送る。どれにも一致しなければ何もしない。
 * - 指を離した後の慣性による動き（触れているかはドライバの INPUT_BTN_TOUCH で知る）は、キーにしない。
 *   変えた触れ方の後の慣性は、モードのキーを先に離していても捨てる（カーソルやスクロールが残って動かないように）。
 *   INPUT_BTN_TOUCH が一度も届いていないトラックパッドでは、慣性も動きとして数える。
 *   離れている間の動きが COAST_GAP_MS より長く途切れた後に届いたら、慣性ではなく、「触れた」が届かなかった
 *   （右のトラックパッドは分割の通信で取りこぼすことがある）新しい触れ方とみなし、次の INPUT_BTN_TOUCH まで
 *   触れているかわからない状態として扱う（その触れ方の間、ずっと慣性として扱って、動きを捨て続けないように）。
 *   離れてから COAST_MAX_MS より後の動きも同じ（慣性が途切れないうちに触れ直して「触れた」が届かなかったとき、
 *   ドライバの慣性の最長の時間で区切る。それまでの間は慣性として扱われる）。
 * - 「離れた」が届かなかったとき（右のトラックパッド）は、次の INPUT_BTN_TOUCH まで触れているとみなすので、
 *   離した後の慣性を指の動きとして扱う（モードのキーを押していると、キーに変わる。止まった指と慣性は見分けられない
 *   ので、時間では区切らない）。
 * - 2 本指でスクロールした後、1 本の指を残したまま止めると、ドライバはスクロールの慣性を送る（触れたままなので
 *   慣性と見分けられず、モードのキーを押していると、キーに変わる）。指をすべて離せば、慣性はキーにしない。
 *   カーソルとスクロールは別々に数える（一筆書きの後に 2 本指でスクロールしても、一筆書きの慣性は捨てる）。
 * - モードのキーを押したときのレイヤーが解除されたら（押したままでも）、そのモードは終わる（描きかけの
 *   一筆書きは送らずに捨てる）。トラックパッドが止まったままにならないように。
 * - INPUT_BTN_TOUCH は、見たあと常に止める（リスナーは使わず、通すと空のマウスのレポートを送るため。
 *   リスナーのレイヤーごとの上書き（子ノード）の中では STOP が伝わらないので、空のレポートが 1 回出る）。
 * - 捨てた動き・ボタンは、リスナーが何もしない入力（INPUT_EV_MSC、同期なし）に変えてから止める
 *   （ZMK_INPUT_PROC_STOP）。リスナーのレイヤーごとの上書き（子ノード。今は使っていない）の中では、STOP しても
 *   リスナー本体の集計・送信に進むため。
 * - ドライバが 1 組で送る動き（X と Y など）は、組の途中でモードが変わっても、組の最初と同じ扱いにする。
 *   組の続き（X の後の Y、横ホイールの後の縦ホイール）でない動きが届いたら、前の組の続き（同期つき）が
 *   届かなかったとみなして、新しい組にする。
 * - 2 本指のピンチ（BTN_7 と Ctrl＋ホイール）は考えていない（ドライバの設定でピンチを無効にしてある）。
 *
 * 入力処理は入力スレッドで呼ばれる。behavior はシステムワークキューで呼ぶ（自作の behavior は状態を
 * システムワークキューだけで触る前提のため）ので、送るものを列に預けてワークで送る。送るワークの予約も
 * システムワークキューだけで行う（入力スレッドから予約し直すと、押してから離すまでの待ち時間が飛ぶため）。
 * 押した behavior は、モードのキーを押したときのレイヤーと位置で呼ぶ（&swap_next などが、そのレイヤーの
 * 解除で終わるように）。まだ押していない分は、そのレイヤーが解除されていたら捨てる（解除の後に &swap_next を
 * 押し直して、Alt が押されたままにならないように）。動かした量で送る分は、そのモードのキーの押し方が
 * 終わっていても捨てる（一筆書きはキーを離して決めることがあるので捨てない）。押した分は必ず離す。
 */

#define DT_DRV_COMPAT zmk_input_processor_tp_keys

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <dt-bindings/lalapad/tp_keys.h>

#include "layer_lock.h"
#include "split_link.h"
#include "tp_keys.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))

/* 同時に押しておけるモードのキーの数 */
#define MAX_HELD_MODES 4
/* 見分ける入力デバイス（トラックパッド）の数 */
#define MAX_SLOTS 4
/* 一筆書きで覚える向きの数（dt-bindings/lalapad/tp_keys.h の TP_STROKE4 まで） */
#define MAX_STROKE_LEN 4
/* 送るのを待っておける数 */
#define QUEUE_LEN 16
/* 動かした量で送る分のうち、待っておける数 */
#define MAX_PENDING_STEPS 8
/* 離れている間の動きがこれより長く途切れたら、慣性ではない（ドライバの慣性は、続けて送られる） */
#define COAST_GAP_MS 300
/* 離れてからこれより長くたった動きも、慣性ではない（ドライバの慣性は最長 3000 ms。*_INERTIA_MAX_DURATION_MS） */
/* （ドライバは慣性の時間を少し多めに数え、送るのも少し遅れるので、余裕を足す） */
#define COAST_MAX_MS (3000 + COAST_GAP_MS)
/* 1 回の入力で数える回数の上限（大きな値が届いても、待っておける数を超えて数えない） */
#define MAX_STEPS_PER_EVENT MAX_PENDING_STEPS
/* 捨てるタップ・ジェスチャー（INPUT_BTN_0〜7） */
#define NUM_BUTTONS 8

struct held_mode {
    const struct tp_keys_mode *mode;
    int layer;
    uint32_t position;
    /* 押すたびに変わる番号（同じキーを離してすぐ押し直したとき、前の押し方の分と見分ける） */
    uint32_t seq;
    /* MOTION: 1 本指・2 本指を変えるか（その 4 つがすべて &trans なら変えずに通す） */
    bool convert_cursor;
    bool convert_scroll;
};

enum axis_lock { AXIS_NONE, AXIS_X, AXIS_Y };

struct axis_acc {
    int32_t x, y;
    enum axis_lock axis;
};

struct slot {
    /* この入力デバイスの分（NULL なら空き） */
    const struct device *dev;
    /* INPUT_BTN_TOUCH が届いたことがあるか、いま触れているか */
    bool touch_known;
    bool touching;
    /* 今の触れ方（指を離した後の慣性を含む）で、カーソル・スクロールの動きをキーに変えたか */
    bool converted_cursor;
    bool converted_scroll;
    /* 離れている間に最後に動きを受け取った時刻（慣性が途切れたかを見る）と、離れた時刻 */
    uint32_t last_coast_ms;
    uint32_t released_ms;
    /* 捨てたタップ・ジェスチャーのボタン（「離した」も捨てる。押すたびに決め直す） */
    uint8_t buttons_dropped;
    /* 同期（sync）のない動きを受け取った後で、同じ組の続きを待っているか、その組を捨てているか
     * （ドライバは X と Y、横と縦のホイールを 1 組で送る。組の途中でモードが変わっても、組ごとに同じ扱いにする） */
    bool in_frame;
    bool frame_dropped;
    /* 組の最初の動きの種類（INPUT_REL_*） */
    uint16_t frame_code;
    /* 最後に見たモードの切り替えの回数（変わったら、たまった量を捨てる） */
    uint32_t gen;
    struct axis_acc cursor;
    struct axis_acc scroll;

    /* 一筆書き: 描いているモード（mode が NULL なら描いていない） */
    struct held_mode stroke;
    int32_t sx, sy;
    uint16_t code;
    uint8_t len;
    uint8_t last_dir;
    bool overflow;
};

struct action {
    const struct zmk_behavior_binding *binding;
    int layer;
    uint32_t position;
    /* 送るきっかけになったモードのキーの押し方（held_mode の seq） */
    uint32_t seq;
    uint16_t tap_ms;
    /* 動かした量で送る分（そのキーを押している間だけ送る。待っておける数を限る） */
    bool motion;
};

/* 以下はすべて lock で守る（入力スレッドとシステムワークキューから触る） */
static struct k_spinlock lock;
static struct held_mode held[MAX_HELD_MODES];
static size_t held_len;
static uint32_t press_seq;
static uint32_t gen;
static struct slot slots[MAX_SLOTS];
static struct action queue[QUEUE_LEN];
static size_t queue_len;
/* 列がいっぱいで捨てた数（ログ用） */
static uint32_t dropped;

/* ---- 以下はロックを取った状態で呼ぶ ---- */

static bool is_trans(const struct zmk_behavior_binding *b) {
    return b->behavior_dev == NULL || strcmp(b->behavior_dev, TRANS_BEHAVIOR_NAME) == 0;
}

static bool any_converted(const struct zmk_behavior_binding *b) {
    for (int i = 0; i < TP_KEYS_DIRS; i++) {
        if (!is_trans(&b[i])) {
            return true;
        }
    }
    return false;
}

static const struct held_mode *active_mode(void) {
    return held_len > 0 ? &held[held_len - 1] : NULL;
}

static bool is_held(uint32_t seq) {
    for (size_t i = 0; i < held_len; i++) {
        if (held[i].seq == seq) {
            return true;
        }
    }
    return false;
}

static void remove_held(size_t i) {
    memmove(&held[i], &held[i + 1], (held_len - i - 1) * sizeof(held[0]));
    held_len--;
    gen++;
}

static void remove_queued(size_t i) {
    memmove(&queue[i], &queue[i + 1], (queue_len - i - 1) * sizeof(queue[0]));
    queue_len--;
}

/* 送る列から、動かした量で送る分（まだ送っていないもの）を捨てる */
static void drop_queued_motion(void) {
    for (size_t i = queue_len; i > 0; i--) {
        if (queue[i - 1].motion) {
            remove_queued(i - 1);
        }
    }
}

/* 送る列に足す。足したら true */
static bool enqueue(const struct held_mode *hm, const struct zmk_behavior_binding *b, bool motion) {
    if (!motion && is_trans(b)) {
        return false;
    }
    if (motion) {
        // 違う向き（または違う押し方）の分が待っていたら捨てる。同じ向きの分は MAX_PENDING_STEPS まで。
        // &trans の向きに動かしたときも、ほかの向きの待っている分は捨てる（下で、足さずに戻る）
        size_t pending = 0;
        bool other = false;
        for (size_t i = 0; i < queue_len; i++) {
            if (queue[i].motion) {
                pending++;
                other |= queue[i].binding != b || queue[i].seq != hm->seq;
            }
        }
        if (other) {
            drop_queued_motion();
            pending = 0;
        }
        if (is_trans(b) || pending >= MAX_PENDING_STEPS) {
            return false;
        }
    }
    if (queue_len >= QUEUE_LEN) {
        dropped++;
        return false;
    }
    queue[queue_len++] = (struct action){
        .binding = b,
        .layer = hm->layer,
        .position = hm->position,
        .seq = hm->seq,
        .tap_ms = hm->mode->tap_ms,
        .motion = motion,
    };
    return true;
}

static void reset_stroke(struct slot *s) {
    s->stroke.mode = NULL;
    s->sx = s->sy = 0;
    s->code = 0;
    s->len = 0;
    s->last_dir = 0;
    s->overflow = false;
}

/* 描いた並びに一致するものを送り、一筆書きを終える。送るものを足したら true */
static bool finish_stroke(struct slot *s) {
    const struct tp_keys_mode *mode = s->stroke.mode;
    bool queued = false;
    if (mode != NULL && s->len > 0 && !s->overflow) {
        for (size_t i = 0; i < mode->num_bindings; i++) {
            if (mode->strokes[i] == s->code) {
                queued = enqueue(&s->stroke, &mode->bindings[i], false);
                break;
            }
        }
    }
    reset_stroke(s);
    return queued;
}

/* ---- 送る（システムワークキュー） ---- */

static struct action current;
static bool current_pressed;

static void invoke(const struct action *a, bool pressed) {
    struct zmk_behavior_binding_event event = {
        .layer = a->layer,
        .position = a->position,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
    zmk_behavior_invoke_binding(a->binding, event, pressed);
}

/* 送ってよい次の分を取り出す。なければ false */
static bool next_action(struct action *out) {
    while (true) {
        k_spinlock_key_t key = k_spin_lock(&lock);
        if (queue_len == 0) {
            k_spin_unlock(&lock, key);
            return false;
        }
        *out = queue[0];
        remove_queued(0);
        const bool held_ok = !out->motion || is_held(out->seq);
        k_spin_unlock(&lock, key);
        if (held_ok && zmk_keymap_layer_active(out->layer)) {
            return true;
        }
    }
}

/* 1 回ごとに「押す → tap_ms → 離す → tap_ms → 次を押す」 */
static void runner_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(runner_work, runner_work_cb);

static void runner_work_cb(struct k_work *work) {
    if (current_pressed) {
        invoke(&current, false);
        current_pressed = false;
        // 次を押す前に必ず待つ（この間に届いた分も、この予約で送る）
        k_work_schedule(&runner_work, K_MSEC(current.tap_ms));
        return;
    }
    if (!next_action(&current)) {
        return;
    }
    invoke(&current, true);
    current_pressed = true;
    k_work_schedule(&runner_work, K_MSEC(current.tap_ms));
}

/* 送っていなければ送り始める（システムワークキュー。送っている途中なら、その予約のまま続けて送る） */
static void kick_work_cb(struct k_work *work) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const uint32_t n = dropped;
    dropped = 0;
    k_spin_unlock(&lock, key);
    if (n > 0) {
        LOG_WRN("Trackpad keys: queue full, dropped %u", n);
    }

    if (!current_pressed && !k_work_delayable_is_pending(&runner_work)) {
        k_work_schedule(&runner_work, K_NO_WAIT);
    }
}

static K_WORK_DEFINE(kick_work, kick_work_cb);

/* ---- モードのキー（システムワークキュー） ---- */

static void mode_pressed(const struct tp_keys_mode *mode,
                         const struct zmk_behavior_binding_event *event) {
    bool full = false;
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (held_len < MAX_HELD_MODES) {
        struct held_mode *hm = &held[held_len++];
        *hm = (struct held_mode){
            .mode = mode,
            .layer = event->layer,
            .position = event->position,
            .seq = ++press_seq,
        };
        if (mode->kind == TP_KEYS_KIND_MOTION) {
            hm->convert_cursor = any_converted(&mode->bindings[TP_KEYS_CURSOR_FIRST]);
            hm->convert_scroll = any_converted(&mode->bindings[TP_KEYS_SCROLL_FIRST]);
        }
        gen++;
        // 前のモードで動かした量で送るはずだった、まだ送っていない分は捨てる（後から押したモードが働いている間に、
        // 前のモードの矢印などが送られ続けないように）
        drop_queued_motion();
        // 描きかけの一筆書きは捨てる（後から押したモードが働くので、前のモードで描いた分を、指を離したときに
        // 送らないように）
        for (size_t i = 0; i < MAX_SLOTS; i++) {
            if (slots[i].stroke.mode != NULL) {
                reset_stroke(&slots[i]);
            }
        }
    } else {
        full = true;
    }
    k_spin_unlock(&lock, key);

    if (full) {
        LOG_WRN("Trackpad keys: too many mode keys held");
    }
}

static void mode_released(const struct tp_keys_mode *mode,
                          const struct zmk_behavior_binding_event *event) {
    bool queued = false;
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (size_t i = held_len; i > 0; i--) {
        if (held[i - 1].mode == mode && held[i - 1].position == event->position) {
            const uint32_t seq = held[i - 1].seq;
            remove_held(i - 1);
            // 指より先にキーを離したら、そこまでに描いた並びで決める
            for (size_t j = 0; j < MAX_SLOTS; j++) {
                if (slots[j].stroke.mode != NULL && slots[j].stroke.seq == seq) {
                    queued |= finish_stroke(&slots[j]);
                }
            }
            break;
        }
    }
    k_spin_unlock(&lock, key);

    if (queued) {
        k_work_submit(&kick_work);
    }
}

int tp_keys_binding_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    mode_pressed(dev->config, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

int tp_keys_binding_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    mode_released(dev->config, &event);
    return ZMK_BEHAVIOR_OPAQUE;
}

/* レイヤーが解除されたら、そのレイヤーで押したモードを終える（描きかけの一筆書きは捨てる。システムワークキュー） */
static int tp_keys_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (size_t i = held_len; i > 0; i--) {
        // 固定したレイヤーは、&to などで解除されても固定の listener が有効にし直す（listener の呼ばれる順番に
        // 頼らないように、固定しているかも見る）
        if (!zmk_keymap_layer_active(held[i - 1].layer) && !layer_lock_is_locked(held[i - 1].layer)) {
            const uint32_t seq = held[i - 1].seq;
            remove_held(i - 1);
            for (size_t j = 0; j < MAX_SLOTS; j++) {
                if (slots[j].stroke.mode != NULL && slots[j].stroke.seq == seq) {
                    reset_stroke(&slots[j]);
                }
            }
        }
    }
    k_spin_unlock(&lock, key);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tp_keys, tp_keys_layer_listener);
ZMK_SUBSCRIPTION(tp_keys, zmk_layer_state_changed);

// 入力処理は、zmk,input-processor-tp-keys のノードがあるときだけ（behavior だけのときに未使用の警告を出さない）
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* ---- 入力処理（入力スレッド。以下もロックを取った状態で呼ぶ） ---- */

static void reset_motion(struct slot *s) {
    s->cursor = (struct axis_acc){0};
    s->scroll = (struct axis_acc){0};
}

static struct slot *find_slot(const struct device *dev) {
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].dev == dev) {
            return &slots[i];
        }
    }
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].dev == NULL) {
            slots[i].dev = dev;
            return &slots[i];
        }
    }
    return NULL;
}

/* 縦か横を決めて、step ごとにその向きの binding を送る。dx・dy は指を動かした向き（右・下が正）。
 * first は bindings の上・下・左・右の先頭。送るものを足したら true */
static bool convert_motion(struct axis_acc *acc, int32_t dx, int32_t dy, uint16_t step,
                           const struct held_mode *hm, size_t first) {
    acc->x += dx;
    acc->y += dy;

    if (acc->axis == AXIS_NONE) {
        if (MAX(abs(acc->x), abs(acc->y)) < step) {
            return false;
        }
        acc->axis = abs(acc->x) >= abs(acc->y) ? AXIS_X : AXIS_Y;
    }
    // 決めた向きでないほうの動きは捨てる
    if (acc->axis == AXIS_X) {
        acc->y = 0;
    } else {
        acc->x = 0;
    }

    int32_t *v = acc->axis == AXIS_X ? &acc->x : &acc->y;
    const struct zmk_behavior_binding *b = &hm->mode->bindings[first];
    const struct zmk_behavior_binding *neg = acc->axis == AXIS_X ? &b[TP_KEYS_LEFT] : &b[TP_KEYS_UP];
    const struct zmk_behavior_binding *pos =
        acc->axis == AXIS_X ? &b[TP_KEYS_RIGHT] : &b[TP_KEYS_DOWN];
    bool queued = false;
    for (int i = 0; i < MAX_STEPS_PER_EVENT && *v >= step; i++) {
        *v -= step;
        queued |= enqueue(hm, pos, true);
    }
    for (int i = 0; i < MAX_STEPS_PER_EVENT && *v <= -(int32_t)step; i++) {
        *v += step;
        queued |= enqueue(hm, neg, true);
    }
    // 上限で打ち切った残りは捨てる（後からまとめて出ないように）
    if (abs(*v) >= step) {
        *v = 0;
    }
    return queued;
}

static void track_stroke(struct slot *s, const struct held_mode *hm, int32_t dx, int32_t dy) {
    if (s->stroke.mode == NULL || s->stroke.seq != hm->seq) {
        reset_stroke(s);
        s->stroke = *hm;
    }
    s->sx += dx;
    s->sy += dy;
    const uint16_t step = hm->mode->stroke_step;
    if (MAX(abs(s->sx), abs(s->sy)) < step) {
        return;
    }
    uint8_t dir;
    if (abs(s->sx) >= abs(s->sy)) {
        dir = s->sx > 0 ? TP_RIGHT : TP_LEFT;
    } else {
        dir = s->sy > 0 ? TP_DOWN : TP_UP;
    }
    s->sx = s->sy = 0;
    if (dir == s->last_dir) {
        return;
    }
    s->last_dir = dir;
    if (s->len >= MAX_STROKE_LEN) {
        s->overflow = true;
        return;
    }
    s->code |= (uint16_t)dir << (3 * s->len);
    s->len++;
}

/* REL の入力。捨てるなら true。*queued は送るものを足したら true にする */
static bool handle_rel(struct slot *s, const struct input_event *event, bool *queued) {
    // 指を動かした向き（右・下が正）。ドライバは、ホイールを指を下へ動かした量、
    // 横ホイールを指を左へ動かした量で送る
    int32_t dx = 0, dy = 0;
    bool is_cursor;
    switch (event->code) {
    case INPUT_REL_X:
        dx = event->value;
        is_cursor = true;
        break;
    case INPUT_REL_Y:
        dy = event->value;
        is_cursor = true;
        break;
    case INPUT_REL_WHEEL:
        dy = event->value;
        is_cursor = false;
        break;
    case INPUT_REL_HWHEEL:
        dx = -event->value;
        is_cursor = false;
        break;
    default:
        return false;
    }

    if (s->gen != gen) {
        s->gen = gen;
        reset_motion(s);
    }

    // 慣性（触れていない間の動き）か。途切れた後の動きは、「触れた」が届かなかった新しい触れ方とみなす
    if (s->touch_known && !s->touching) {
        const uint32_t now = k_uptime_get_32();
        if (now - s->last_coast_ms > COAST_GAP_MS || now - s->released_ms > COAST_MAX_MS) {
            // 触れているかわからない状態に戻す（次の INPUT_BTN_TOUCH で知り直す）。動きとして数える
            s->touch_known = false;
            s->converted_cursor = false;
            s->converted_scroll = false;
        } else {
            s->last_coast_ms = now;
        }
    }
    const bool coasting = s->touch_known && !s->touching;
    bool *converted = is_cursor ? &s->converted_cursor : &s->converted_scroll;
    const struct held_mode *hm = active_mode();

    if (hm != NULL && hm->mode->kind == TP_KEYS_KIND_STROKE && is_cursor) {
        if (!coasting) {
            track_stroke(s, hm, dx, dy);
            *converted = true;
        }
        return true;
    }
    if (hm != NULL && hm->mode->kind == TP_KEYS_KIND_MOTION &&
        (is_cursor ? hm->convert_cursor : hm->convert_scroll)) {
        if (!coasting) {
            *queued |= is_cursor ? convert_motion(&s->cursor, dx, dy, hm->mode->cursor_step, hm,
                                                  TP_KEYS_CURSOR_FIRST)
                                 : convert_motion(&s->scroll, dx, dy, hm->mode->scroll_step, hm,
                                                  TP_KEYS_SCROLL_FIRST);
            *converted = true;
        }
        return true;
    }
    if (!coasting) {
        // モードのキーを離した後に、触れたままふつうに動かしたら、その後の慣性はふつうに通す
        *converted = false;
        return false;
    }
    // 変えた触れ方の後の慣性は、モードのキーを離していても捨てる
    return *converted;
}

/* KEY の入力（触れている／離れた、タップ・ジェスチャー）。捨てるなら true */
static bool handle_key(struct slot *s, const struct input_event *event, bool *queued) {
    if (event->code == INPUT_BTN_TOUCH) {
        s->touch_known = true;
        s->touching = event->value != 0;
        // 動きの組の途中で続きが届かなかったときに、前の組の扱いを持ち越さないように
        s->in_frame = false;
        s->frame_dropped = false;
        // 触れ始め・離れたときに、たまった量と縦横の決定を捨てる。離れたら一筆書きを決める
        reset_motion(s);
        if (s->touching) {
            s->converted_cursor = false;
            s->converted_scroll = false;
        } else {
            s->last_coast_ms = k_uptime_get_32();
            s->released_ms = s->last_coast_ms;
            *queued |= finish_stroke(s);
        }
        // リスナーは INPUT_BTN_TOUCH を使わない（同期つきなので、通すと空のマウスのレポートを毎回送る）。
        // オートマウスレイヤーは、触れているかを入力デバイスから直接受け取る
        return true;
    }
    if (event->code < INPUT_BTN_0 || event->code >= INPUT_BTN_0 + NUM_BUTTONS) {
        return false;
    }
    const uint8_t bit = BIT(event->code - INPUT_BTN_0);
    if (event->value) {
        // 押すたびに決め直す（前に捨てた押下の「離した」が届かなかったときに、後の「離した」を捨てないように）
        WRITE_BIT(s->buttons_dropped, event->code - INPUT_BTN_0, active_mode() != NULL);
        return (s->buttons_dropped & bit) != 0;
    }
    if (s->buttons_dropped & bit) {
        s->buttons_dropped &= ~bit;
        return true;
    }
    return false;
}

static int tp_keys_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_KEY && event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    bool drop = false;
    bool queued = false;
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct slot *s = find_slot(event->dev);
    if (s != NULL && event->type == INPUT_EV_KEY) {
        drop = handle_key(s, event, &queued);
    } else if (s != NULL) {
        // 組の続き（X の後の Y、横ホイールの後の縦ホイール）でなければ、前の組の続きが届かなかった。新しい組として扱う
        if (s->in_frame &&
            !((s->frame_code == INPUT_REL_X && event->code == INPUT_REL_Y) ||
              (s->frame_code == INPUT_REL_HWHEEL && event->code == INPUT_REL_WHEEL))) {
            s->in_frame = false;
        }
        if (!s->in_frame) {
            s->frame_code = event->code;
        }
        // 組の途中なら、組の最初の扱いに合わせる（通した組は続きも通し、捨てた組は続きも捨てる）
        if (s->in_frame && !s->frame_dropped) {
            drop = false;
        } else {
            drop = handle_rel(s, event, &queued) || s->in_frame;
        }
        s->frame_dropped = drop;
        s->in_frame = !event->sync;
    }
    k_spin_unlock(&lock, key);

    if (queued) {
        k_work_submit(&kick_work);
    }
    if (!drop) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    // リスナーが何もしない入力（INPUT_EV_MSC）にして止める（レイヤーごとの上書きの中では STOP してもリスナー本体に
    // 進むため。動きは値を 0 にしてもスクロールの端数が足されることがあり、ボタンは値を 0 にすると「離した」になる）。
    // INPUT_BTN_TOUCH は書き換えずに止めるだけにする（レイヤーごとの上書きの中では空のレポートが 1 回出るが、害はない。
    // オートマウスレイヤーが入力デバイスから直接受け取る「触れている」を、呼ばれる順番に頼らずに守るため）
    if (event->type == INPUT_EV_REL || event->code != INPUT_BTN_TOUCH) {
        event->type = INPUT_EV_MSC;
        event->value = 0;
        event->sync = false;
    }
    return ZMK_INPUT_PROC_STOP;
}

#if WATCH_SPLIT_LINK
/* 右（子機）との接続が切れた: 右のトラックパッド（zmk,input-split で中継される入力デバイス）の分は、
 * 「離れた」が届かないまま止まるので、離れたとみなし、描きかけの一筆書きは捨てる（後でモードのキーを離したときに、
 * 描きかけの並びで送らないように） */
#define TP_KEYS_IS_SPLIT_DEV(node_id) || dev == DEVICE_DT_GET(node_id)

static bool is_split_dev(const struct device *dev) {
    return false DT_FOREACH_STATUS_OKAY(zmk_input_split, TP_KEYS_IS_SPLIT_DEV);
}

static void split_lost_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        struct slot *s = &slots[i];
        if (s->dev == NULL || !is_split_dev(s->dev)) {
            continue;
        }
        reset_stroke(s);
        if (s->touching) {
            s->touching = false;
            s->released_ms = k_uptime_get_32();
        }
        s->in_frame = false;
        s->frame_dropped = false;
    }
    k_spin_unlock(&lock, key);
}

static K_WORK_DEFINE(split_lost_work, split_lost_work_cb);

static void tp_keys_split_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (is_split_link(conn)) {
        k_work_submit(&split_lost_work);
    }
}

BT_CONN_CB_DEFINE(tp_keys_split_callbacks) = {
    .disconnected = tp_keys_split_disconnected,
};
#endif // WATCH_SPLIT_LINK

static struct zmk_input_processor_driver_api tp_keys_driver_api = {
    .handle_event = tp_keys_handle_event,
};

#define TP_KEYS_IP_INST(n)                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tp_keys_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_KEYS_IP_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
