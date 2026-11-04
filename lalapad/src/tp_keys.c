/*
 * トラックパッドをキー操作に変える入力処理 (zmk,input-processor-tp-keys)。
 *
 * keymap の zmk,behavior-tp-keys・zmk,behavior-tp-stroke のキーを押している間だけ、トラックパッドの動きを
 * カーソル・スクロールの代わりに、決めておいたキー（behavior）の入力に変える。押していない間は何もせず通す。
 * トラックパッドのリスナーの入力処理の並びの、速度の調整（scaler）より前に入れる（動きの量を、速度の設定や
 * 数字・記号レイヤーの低速に関係なく、トラックパッドの生の値で数えるため）。オートマウスレイヤーより前に
 * 入れるので、変えた動きではマウスレイヤーはオンにならない。
 *
 * - zmk,behavior-tp-keys: 動かした量 step ごとに、その向きの binding を 1 回送る（矢印キー、ダイヤルなど）。
 *   動き始めの向きで縦か横かを決め、指を離すまで変えない（斜めに動いても混ざらない）。
 * - zmk,behavior-tp-stroke: 1 本指で描いた向きの並び（最大 4 つ）を覚え、指を離したとき（または先にキーを
 *   離したとき）に、並びが一致する binding を 1 回送る。どれにも一致しなければ何もしない。
 * - 変えている間は、指を離した後の慣性による動きを捨てる（触れているかはドライバの INPUT_BTN_TOUCH で知る。
 *   一度も届いていないトラックパッドでは、慣性も動きとして数える）。
 *
 * - 変えた動き・捨てた動きは、値を 0 にしてから止める（ZMK_INPUT_PROC_STOP）。リスナーのレイヤーごとの
 *   上書き（lowspeedmode）の中では STOP が伝わらず、後ろの処理とカーソルの移動に進むため。
 *
 * 入力処理は入力スレッドで呼ばれる。behavior はシステムワークキューで呼ぶ（自作の behavior は状態を
 * システムワークキューだけで触る前提のため）ので、送るものを列に預けてワークで送る。送るワークの予約も
 * システムワークキューだけで行う（入力スレッドから予約し直すと、押してから離すまでの待ち時間が飛ぶため）。
 * 押した behavior は、モードのキーを押したときのレイヤーと位置で呼ぶ（&swap_next などが、そのレイヤーの
 * 解除で終わるように）。まだ押していない分は、そのモードのキーを離したか、そのレイヤーが解除されていたら捨てる
 * （解除の後に &swap_next を押し直して、Alt が押されたままにならないように）。押した分は必ず離す。
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
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <dt-bindings/lalapad/tp_keys.h>

#include "tp_keys.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))

/* 同時に押しておけるモードのキーの数 */
#define MAX_HELD_MODES 4
/* 見分けるトラックパッド（リスナー）の数 */
#define MAX_SLOTS 4
/* 一筆書きで覚える向きの数（dt-bindings/lalapad/tp_keys.h の TP_STROKE4 まで） */
#define MAX_STROKE_LEN 4
/* 送るのを待っておける数（大きく払ったときの分）。あふれた分は捨てる */
#define ACTION_QUEUE_SIZE 32
/* 1 回の入力で送る数の上限（大きな値が届いても、列をあふれさせない） */
#define MAX_STEPS_PER_EVENT 8

struct held_mode {
    const struct tp_keys_mode *mode;
    int layer;
    uint32_t position;
};

enum axis_lock { AXIS_NONE, AXIS_X, AXIS_Y };

struct axis_acc {
    int32_t x, y;
    enum axis_lock axis;
};

struct slot {
    /* INPUT_BTN_TOUCH が届いたことがあるか、いま触れているか */
    bool touch_known;
    bool touching;
    /* 最後に見たモードの切り替えの回数（変わったら、たまった量を捨てる） */
    uint32_t gen;
    struct axis_acc cursor;
    struct axis_acc scroll;

    /* 一筆書き: 描いているモード（そのキーを押したときのレイヤー・位置も覚える） */
    struct held_mode stroke;
    int32_t sx, sy;
    uint16_t code;
    uint8_t len;
    uint8_t last_dir;
    bool overflow;
};

struct action {
    struct zmk_behavior_binding binding;
    /* 送るきっかけになったモードのキー（まだ押していない分を捨てるかの判断に使う） */
    const struct tp_keys_mode *mode;
    /* そのキーを押している間だけ送る（動かした量で送る分。一筆書きはキーを離して決めるので false） */
    bool while_held;
    int layer;
    uint32_t position;
    uint16_t tap_ms;
};

static struct k_spinlock lock;
static struct held_mode held[MAX_HELD_MODES];
static size_t held_len;
static uint32_t gen;
static struct slot slots[MAX_SLOTS];

K_MSGQ_DEFINE(tp_keys_actions, sizeof(struct action), ACTION_QUEUE_SIZE, 4);

/* ---- 送る（システムワークキュー） ---- */

static struct action current;
static bool current_pressed;

/* まだ押していない分を送ってよいか: そのレイヤーが有効で、while_held ならそのモードのキーを押したまま */
static bool action_still_valid(const struct action *a) {
    if (!zmk_keymap_layer_active(a->layer)) {
        return false;
    }
    if (!a->while_held) {
        return true;
    }
    bool held_now = false;
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (size_t i = 0; i < held_len; i++) {
        if (held[i].mode == a->mode && held[i].position == a->position) {
            held_now = true;
            break;
        }
    }
    k_spin_unlock(&lock, key);
    return held_now;
}

static void invoke(const struct action *a, bool pressed) {
    struct zmk_behavior_binding_event event = {
        .layer = a->layer,
        .position = a->position,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
    zmk_behavior_invoke_binding(&a->binding, event, pressed);
}

/* 1 回ごとに「押す → tap_ms → 離す → tap_ms → 次を押す」 */
static void runner_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(runner_work, runner_work_cb);

static void runner_work_cb(struct k_work *work) {
    if (current_pressed) {
        invoke(&current, false);
        current_pressed = false;
        if (k_msgq_num_used_get(&tp_keys_actions) > 0) {
            k_work_schedule(&runner_work, K_MSEC(current.tap_ms));
        }
        return;
    }
    do {
        if (k_msgq_get(&tp_keys_actions, &current, K_NO_WAIT) != 0) {
            return;
        }
    } while (!action_still_valid(&current));
    invoke(&current, true);
    current_pressed = true;
    k_work_schedule(&runner_work, K_MSEC(current.tap_ms));
}

/* 送っていなければ送り始める（システムワークキュー。送っている途中なら、その予約のまま続けて送る） */
static void kick_work_cb(struct k_work *work) {
    if (!current_pressed && !k_work_delayable_is_pending(&runner_work)) {
        k_work_schedule(&runner_work, K_NO_WAIT);
    }
}

static K_WORK_DEFINE(kick_work, kick_work_cb);

/* 集めた送るものを列に預ける（ロックの外で呼ぶ） */
static void submit(const struct action *actions, size_t n) {
    if (n == 0) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (k_msgq_put(&tp_keys_actions, &actions[i], K_NO_WAIT) != 0) {
            LOG_WRN("Trackpad keys: action queue full, dropping %d", (int)(n - i));
            break;
        }
    }
    k_work_submit(&kick_work);
}

/* ---- 以下はロックを取った状態で呼ぶ ---- */

static bool is_trans(const struct zmk_behavior_binding *b) {
    return b->behavior_dev == NULL || strcmp(b->behavior_dev, TRANS_BEHAVIOR_NAME) == 0;
}

static bool add_action(struct action *out, size_t *n, size_t max, const struct held_mode *hm,
                       const struct zmk_behavior_binding *b, bool while_held) {
    if (is_trans(b) || *n >= max) {
        return false;
    }
    out[*n] = (struct action){
        .binding = *b,
        .mode = hm->mode,
        .while_held = while_held,
        .layer = hm->layer,
        .position = hm->position,
        .tap_ms = hm->mode->tap_ms,
    };
    (*n)++;
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

static void reset_motion(struct slot *s) {
    s->cursor = (struct axis_acc){0};
    s->scroll = (struct axis_acc){0};
}

/* 描いた並びに一致するものを送り、一筆書きを終える */
static void finish_stroke(struct slot *s, struct action *out, size_t *n, size_t max) {
    const struct tp_keys_mode *mode = s->stroke.mode;
    if (mode != NULL && s->len > 0 && !s->overflow) {
        for (size_t i = 0; i < mode->num_strokes; i++) {
            if (mode->strokes[i] == s->code) {
                add_action(out, n, max, &s->stroke, &mode->stroke_bindings[i], false);
                break;
            }
        }
    }
    reset_stroke(s);
}

static const struct held_mode *active_mode(void) {
    return held_len > 0 ? &held[held_len - 1] : NULL;
}

/* ---- モードのキー（システムワークキュー） ---- */

void tp_keys_mode_pressed(const struct tp_keys_mode *mode,
                          const struct zmk_behavior_binding_event *event) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (held_len < MAX_HELD_MODES) {
        held[held_len++] = (struct held_mode){
            .mode = mode,
            .layer = event->layer,
            .position = event->position,
        };
        gen++;
    } else {
        LOG_WRN("Trackpad keys: too many mode keys held");
    }
    k_spin_unlock(&lock, key);
}

void tp_keys_mode_released(const struct tp_keys_mode *mode,
                           const struct zmk_behavior_binding_event *event) {
    struct action out[MAX_SLOTS];
    size_t n = 0;

    k_spinlock_key_t key = k_spin_lock(&lock);
    for (size_t i = held_len; i > 0; i--) {
        if (held[i - 1].mode == mode && held[i - 1].position == event->position) {
            memmove(&held[i - 1], &held[i], (held_len - i) * sizeof(held[0]));
            held_len--;
            gen++;
            break;
        }
    }
    // 指より先にキーを離したら、そこまでに描いた並びで決める
    for (size_t i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].stroke.mode == mode && slots[i].stroke.position == event->position) {
            finish_stroke(&slots[i], out, &n, ARRAY_SIZE(out));
        }
    }
    k_spin_unlock(&lock, key);

    submit(out, n);
}

/* ---- 入力処理（入力スレッド） ---- */

/* 縦か横を決めて、step ごとにその向きの binding を送る。first は bindings の上・下・左・右の先頭。
 * up_negative: 値が負のほうを「上」とする（カーソルは下が正、ホイールは上が正） */
static void convert_motion(struct axis_acc *acc, uint16_t code_x, uint16_t code, int32_t value,
                           uint16_t step, bool up_negative, const struct held_mode *hm, size_t first,
                           struct action *out, size_t *n, size_t max) {
    if (step == 0) {
        return;
    }
    const bool is_x = code == code_x;
    if (is_x) {
        acc->x += value;
    } else {
        acc->y += value;
    }

    if (acc->axis == AXIS_NONE) {
        if (MAX(abs(acc->x), abs(acc->y)) < step) {
            return;
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
    // 上・下・左・右の順。X は負が左、Y は up_negative に従う
    const size_t neg = acc->axis == AXIS_X ? 2 : (up_negative ? 0 : 1);
    const size_t pos = acc->axis == AXIS_X ? 3 : (up_negative ? 1 : 0);
    for (int i = 0; i < MAX_STEPS_PER_EVENT && *v >= step; i++) {
        *v -= step;
        add_action(out, n, max, hm, &b[pos], true);
    }
    for (int i = 0; i < MAX_STEPS_PER_EVENT && *v <= -(int32_t)step; i++) {
        *v += step;
        add_action(out, n, max, hm, &b[neg], true);
    }
    // 上限で打ち切った残りは捨てる（後からまとめて出ないように）
    if (abs(*v) >= step) {
        *v = 0;
    }
}

static void track_stroke(struct slot *s, const struct held_mode *hm, uint16_t code, int32_t value) {
    if (s->stroke.mode != hm->mode || s->stroke.position != hm->position) {
        reset_stroke(s);
        s->stroke = *hm;
    }
    if (code == INPUT_REL_X) {
        s->sx += value;
    } else {
        s->sy += value;
    }
    const uint16_t step = hm->mode->stroke_step;
    if (step == 0 || MAX(abs(s->sx), abs(s->sy)) < step) {
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

static int tp_keys_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state) {
    const uint8_t idx = state ? state->input_device_index : 0;
    if (idx >= MAX_SLOTS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct action out[MAX_STEPS_PER_EVENT];
    size_t n = 0;
    int ret = ZMK_INPUT_PROC_CONTINUE;

    k_spinlock_key_t key = k_spin_lock(&lock);
    struct slot *s = &slots[idx];

    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        s->touch_known = true;
        s->touching = event->value != 0;
        // 触れ始め・離れたときに、たまった量と縦横の決定を捨てる。離れたら一筆書きを決める
        reset_motion(s);
        if (!s->touching) {
            finish_stroke(s, out, &n, ARRAY_SIZE(out));
        }
        k_spin_unlock(&lock, key);
        submit(out, n);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->type != INPUT_EV_REL) {
        k_spin_unlock(&lock, key);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (s->gen != gen) {
        s->gen = gen;
        reset_motion(s);
    }

    const struct held_mode *hm = active_mode();
    const bool is_cursor = event->code == INPUT_REL_X || event->code == INPUT_REL_Y;
    const bool is_scroll = event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL;
    // 慣性（触れていない間の動き）か
    const bool coasting = s->touch_known && !s->touching;

    if (hm != NULL && is_cursor && hm->mode->num_strokes > 0) {
        if (!coasting) {
            track_stroke(s, hm, event->code, event->value);
        }
        event->value = 0;
        ret = ZMK_INPUT_PROC_STOP;
    } else if (hm != NULL && hm->mode->bindings != NULL &&
               ((is_cursor && hm->mode->convert_cursor) || (is_scroll && hm->mode->convert_scroll))) {
        if (!coasting) {
            if (is_cursor) {
                convert_motion(&s->cursor, INPUT_REL_X, event->code, event->value,
                               hm->mode->cursor_step, true, hm, TP_KEYS_CURSOR_FIRST, out, &n,
                               ARRAY_SIZE(out));
            } else {
                convert_motion(&s->scroll, INPUT_REL_HWHEEL, event->code, event->value,
                               hm->mode->scroll_step, false, hm, TP_KEYS_SCROLL_FIRST, out, &n,
                               ARRAY_SIZE(out));
            }
        }
        event->value = 0;
        ret = ZMK_INPUT_PROC_STOP;
    }
    k_spin_unlock(&lock, key);

    submit(out, n);
    return ret;
}

static struct zmk_input_processor_driver_api tp_keys_driver_api = {
    .handle_event = tp_keys_handle_event,
};

#define TP_KEYS_IP_INST(n)                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tp_keys_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TP_KEYS_IP_INST)
