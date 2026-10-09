/*
 * Windows の Alt コードで文字を送る behavior（zmk,behavior-alt-code、引数なし）。
 * keymap で codes = <33116> などを付けたインスタンスを作って使う。
 *
 * codes の各値を「Alt を押したまま、テンキーでその10進数を打つ」操作として順に送り、
 * 最後に ← を cursor-left 回送る（カッコの間にカーソルを置く用。不要なら 0）。
 * IME の状態やローマ字テーブルに関係なく、確定済みの文字として入る（Windows の機能）。
 *
 * テンキーの数字は Num Lock がオフだと矢印キーなどとして扱われるため、
 * PC から届いている Num Lock の点灯状態（HID インジケーター）を見て、オフのときだけ
 * 前後で Num Lock を押して一時的にオンにし、送り終わったら元に戻す。
 *
 * - 送る操作は開始時に一覧（steps）にまとめ、k_work で1つずつ順に送る。behavior queue を
 *   使わないので、キューが満杯で「離す」や Num Lock の復元が抜けることはない
 *   （そのかわり、キューに残っている &kana_key やマクロの送信とは順序を保証しない）。
 * - 送信中に押された分は待ち行列に入れ、前の送信が終わってから順に送る。
 * - 送信の途中で接続先（USB・BLE プロファイル）が変わったら、残りは送らずに中止する
 *   （別の PC に数字や Num Lock の復元を送らないため）。送信中に押されて待っていた分も、
 *   押したときと接続先が違えば送らない。
 *   制約: ZMK v0.3.0 の BLE 送信（app/src/hog.c）はレポートをキューに入れ、取り出す時点の
 *   アクティブなプロファイルへ送る。そのため、ここで接続先を確かめてからキューに入った直近の
 *   数操作は、送信前にプロファイルが切り替わると切り替え先に届くことがある（通常のキーやマクロと
 *   同じ ZMK 本体の動作で、behavior からは送信先を固定できない。防ぐには hog.c のキューに
 *   送信先を持たせる ZMK 本体の変更が必要）。このため中止したときは、どちらの PC の Num Lock が
 *   どうなったか確定できないものとして自分の記録は作らず（あれば消し）、PC から届く点灯状態に従う。
 *   接続先が切り替わったとき（zmk_endpoint_changed）も、同じ理由で記録を使わない。記録には
 *   送信を始めたときの世代番号を付け、使うときに今の世代番号と同じものだけを有効とする
 *   （切り替え前の送信の結果を、切り替え後に引き継がない）。
 *   切り替えは世代番号（endpoint_generation）で数え、A → B → A のように元の接続先へ戻った場合も
 *   「切り替わった」と判定する（切り替え時に ZMK が HID の状態を消し、Alt コードが途切れるため）。
 * - Num Lock を切り替えた直後は、PC からの点灯状態の報告が遅れて古い状態が残っていることがある。
 *   そのため切り替えてから ASSUME_MS の間は、PC の報告ではなく「実際に送った Num Lock の回数から
 *   わかる状態」（元に戻した／中止でオンのまま）という自分の記録を使う。記録は接続先ごとに持つ。
 *   （この間に別のキーボードなどで Num Lock を切り替えた場合は反映されない）
 *
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので、送信の状態の排他は不要。
 * 接続先の切り替えの通知（zmk_endpoint_changed）は別のスレッドから届くことがあるので、
 * そこでは世代番号（atomic）を進めるだけにする。記録は世代番号つきで持ち、使うときに照合する。
 */

#define DT_DRV_COMPAT zmk_behavior_alt_code

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/keys.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* HID の LED レポートのビット0 が Num Lock（HID Usage Tables: LED Page 0x01） */
#define HID_INDICATOR_NUM_LOCK BIT(0)

/* 1つの値の最大桁数（uint32_t の10進数） */
#define MAX_DIGITS 10
/* 1回の送信で送る操作（押す／離す）の上限 */
#define MAX_STEPS 128
/* 送信中に押された分を待たせておける数 */
#define MAX_PENDING 4
/* Num Lock を切り替えてから、PC の報告より自分の記録を優先する時間 */
#define ASSUME_MS 3000

struct behavior_alt_code_config {
    const uint32_t *codes;
    size_t codes_len;
    uint32_t cursor_left;
    uint32_t tap_ms;
    uint32_t wait_ms;
};

struct step {
    uint32_t keycode;
    bool press;
};

static const uint32_t keypad_digits[10] = {
    KP_N0, KP_N1, KP_N2, KP_N3, KP_N4, KP_N5, KP_N6, KP_N7, KP_N8, KP_N9,
};

/* ---- 送信の状態 ---- */

static bool busy;
static const struct behavior_alt_code_config *active_cfg;
static struct step steps[MAX_STEPS];
static size_t step_count;
static size_t step_index;
static bool num_lock_toggled;
static bool num_lock_original;
static struct zmk_endpoint_instance active_endpoint;
/* 接続先が切り替わるたびに増える番号と、この送信を始めたときの番号 */
static atomic_t endpoint_generation;
static atomic_val_t active_generation;
/* この送信で実際に送った Num Lock の「押す」の回数（0: 未送信、1: オンにした、2: 元に戻した） */
static uint8_t num_lock_presses_sent;
/* この送信でいま押したままにしているキー（中止するときに離す） */
static bool alt_down;
static uint32_t key_down;

/* Num Lock を切り替えた後、ASSUME_MS の間だけ使う記録（接続先ごと） */
static bool assumed_valid[ZMK_ENDPOINT_COUNT];
static bool assumed_num_lock[ZMK_ENDPOINT_COUNT];
static int64_t assume_until[ZMK_ENDPOINT_COUNT];
/* 記録のもとになった送信を始めたときの世代番号。今の世代番号と違う記録は使わない */
static atomic_val_t assumed_generation[ZMK_ENDPOINT_COUNT];

struct pending_press {
    const struct behavior_alt_code_config *cfg;
    atomic_val_t generation; /* 押したときの接続先の世代番号 */
};

static struct pending_press pending[MAX_PENDING];
static size_t pending_count;

static void alt_code_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(alt_code_work, alt_code_work_handler);

static bool reported_num_lock(struct zmk_endpoint_instance endpoint) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    return (zmk_hid_indicators_get_profile(endpoint) & HID_INDICATOR_NUM_LOCK) != 0;
#else
    // 点灯状態を受け取れない設定ではオンとみなし、そのまま送る
    ARG_UNUSED(endpoint);
    return true;
#endif
}

/* 接続先 endpoint の今の Num Lock の状態。その接続先で切り替えてから ASSUME_MS の間は自分の記録を使う
 * （一時的にオンにした報告が、元に戻した報告より後から届くこともあるため、途中で報告に切り替えない） */
static bool current_num_lock(struct zmk_endpoint_instance endpoint) {
    const int idx = zmk_endpoint_instance_to_index(endpoint);
    if (idx >= 0 && idx < ZMK_ENDPOINT_COUNT && assumed_valid[idx]) {
        // 接続先が切り替わる前の送信の記録は使わない（切り替えの前後に送った操作が
        // どちらの PC に届いたか確定できないため。PC から届く点灯状態に従う）
        if (assumed_generation[idx] == atomic_get(&endpoint_generation) &&
            k_uptime_get() < assume_until[idx]) {
            return assumed_num_lock[idx];
        }
        assumed_valid[idx] = false;
    }
    return reported_num_lock(endpoint);
}

/* 最後まで送り終えたときに、Num Lock を元に戻したことを接続先の記録に残す */
static void record_num_lock_restored(void) {
    if (num_lock_presses_sent != 2) {
        return;
    }
    const int idx = zmk_endpoint_instance_to_index(active_endpoint);
    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
        return;
    }
    assumed_num_lock[idx] = num_lock_original;
    assume_until[idx] = k_uptime_get() + ASSUME_MS;
    // この送信を始めたときの世代番号を付ける（この後に接続先が切り替わっていれば、使うときに無効になる）
    assumed_generation[idx] = active_generation;
    assumed_valid[idx] = true;
}

/* 中止したとき: どの操作がどの PC に届いたか確定できないので、記録は作らず、あれば消す */
static void forget_num_lock_state(struct zmk_endpoint_instance endpoint) {
    const int idx = zmk_endpoint_instance_to_index(endpoint);
    if (idx >= 0 && idx < ZMK_ENDPOINT_COUNT) {
        assumed_valid[idx] = false;
    }
}

static void add_step(uint32_t keycode, bool press) {
    steps[step_count++] = (struct step){.keycode = keycode, .press = press};
}

static void add_tap(uint32_t keycode) {
    add_step(keycode, true);
    add_step(keycode, false);
}

static size_t count_digits(uint32_t code) {
    size_t count = 1;
    while (code >= 10) {
        code /= 10;
        count++;
    }
    return count;
}

/* 送る操作の一覧を作る。上限を超える設定なら何も作らずに false（途中まで送ることはしない） */
static bool build_steps(const struct behavior_alt_code_config *cfg) {
    // 途中で上限を超えたらその時点で打ち切る（大きな設定値でも計算があふれないように）
    size_t needed = num_lock_toggled ? 4 : 0;
    bool too_long = cfg->cursor_left > MAX_STEPS / 2;
    if (!too_long) {
        needed += 2 * (size_t)cfg->cursor_left;
    }
    for (size_t i = 0; i < cfg->codes_len && !too_long; i++) {
        needed += 2 + 2 * count_digits(cfg->codes[i]); // 1つあたり最大 22 なので、あふれない
        too_long = needed > MAX_STEPS;
    }
    if (too_long || needed > MAX_STEPS) {
        LOG_ERR("Alt code sequence too long (max %d steps)", MAX_STEPS);
        return false;
    }

    step_count = 0;
    if (num_lock_toggled) {
        add_tap(KP_NUM);
    }
    for (size_t i = 0; i < cfg->codes_len; i++) {
        uint8_t digits[MAX_DIGITS];
        size_t count = 0;
        uint32_t code = cfg->codes[i];
        do {
            digits[count++] = code % 10;
            code /= 10;
        } while (code > 0 && count < MAX_DIGITS);

        add_step(LALT, true);
        // 上の桁から順に打つ
        while (count > 0) {
            add_tap(keypad_digits[digits[--count]]);
        }
        add_step(LALT, false);
    }
    for (uint32_t i = 0; i < cfg->cursor_left; i++) {
        add_tap(LEFT);
    }
    if (num_lock_toggled) {
        add_tap(KP_NUM);
    }
    return true;
}

/* 待ち行列の先頭から次の送信を始める。なければ待機状態に戻る */
static void start_next(void) {
    while (pending_count > 0) {
        const struct behavior_alt_code_config *cfg = pending[0].cfg;
        const atomic_val_t pressed_generation = pending[0].generation;
        pending_count--;
        memmove(&pending[0], &pending[1], pending_count * sizeof(pending[0]));

        if (pressed_generation != atomic_get(&endpoint_generation)) {
            // 押した後に接続先が切り替わった: 押し直していない入力は送らない
            LOG_WRN("Endpoint changed since alt code key press, dropping");
            continue;
        }
        active_endpoint = zmk_endpoints_selected();
        active_generation = pressed_generation;
        num_lock_original = current_num_lock(active_endpoint);
        num_lock_toggled = !num_lock_original;
        if (!build_steps(cfg)) {
            continue;
        }
        busy = true;
        active_cfg = cfg;
        step_index = 0;
        num_lock_presses_sent = 0;
        alt_down = false;
        key_down = 0;
        k_work_schedule(&alt_code_work, K_NO_WAIT);
        return;
    }
    busy = false;
    active_cfg = NULL;
}

/* この送信を始めてから接続先が切り替わったか（元の接続先に戻った場合も含む） */
static bool endpoint_changed_since_start(void) {
    return active_generation != atomic_get(&endpoint_generation) ||
           !zmk_endpoint_instance_eq(zmk_endpoints_selected(), active_endpoint);
}

static void alt_code_work_handler(struct k_work *work) {
    if (endpoint_changed_since_start()) {
        // 途中（最後の操作の後の待ち時間も含む）で接続先が変わった: 残りは送らず、記録も作らない。
        // 押したままのキーは離しておく（接続先の切り替えで ZMK が HID の状態を消しているが、念のため）
        LOG_WRN("Endpoint changed during alt code sequence, aborting");
        const int64_t now = k_uptime_get();
        if (key_down != 0) {
            raise_zmk_keycode_state_changed_from_encoded(key_down, false, now);
        }
        if (alt_down) {
            raise_zmk_keycode_state_changed_from_encoded(LALT, false, now);
        }
        key_down = 0;
        alt_down = false;
        forget_num_lock_state(active_endpoint);
        forget_num_lock_state(zmk_endpoints_selected());
        start_next();
        return;
    }

    if (step_index < step_count) {
        const struct step *s = &steps[step_index++];
        raise_zmk_keycode_state_changed_from_encoded(s->keycode, s->press, k_uptime_get());
        if (s->keycode == LALT) {
            alt_down = s->press;
        } else {
            key_down = s->press ? s->keycode : 0;
        }
        if (s->keycode == KP_NUM && s->press) {
            num_lock_presses_sent++;
        }
        const uint32_t delay = s->press ? active_cfg->tap_ms : active_cfg->wait_ms;
        k_work_schedule(&alt_code_work, K_MSEC(delay));
        return;
    }

    record_num_lock_restored();
    start_next();
}

/* 接続先が切り替わったら世代番号を進める（別のスレッドから呼ばれることがあるので、ここでは数えるだけ） */
static int alt_code_endpoint_listener(const zmk_event_t *eh) {
    atomic_inc(&endpoint_generation);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_alt_code, alt_code_endpoint_listener);
ZMK_SUBSCRIPTION(behavior_alt_code, zmk_endpoint_changed);

static int on_alt_code_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_alt_code_config *cfg = dev->config;

    if (pending_count >= MAX_PENDING) {
        LOG_WRN("Too many pending alt code presses, ignoring");
        return ZMK_BEHAVIOR_OPAQUE;
    }
    pending[pending_count++] = (struct pending_press){
        .cfg = cfg,
        .generation = atomic_get(&endpoint_generation),
    };

    if (!busy) {
        start_next();
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_alt_code_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_alt_code_driver_api = {
    .binding_pressed = on_alt_code_binding_pressed,
    .binding_released = on_alt_code_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // 引数なしの behavior として ZMK Studio から割り当てられるようにする
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define ALT_CODE_INST(n)                                                                           \
    static const uint32_t behavior_alt_code_codes_##n[] = DT_INST_PROP(n, codes);                  \
    static const struct behavior_alt_code_config behavior_alt_code_config_##n = {                 \
        .codes = behavior_alt_code_codes_##n,                                                      \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .cursor_left = DT_INST_PROP(n, cursor_left),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_alt_code_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_alt_code_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ALT_CODE_INST)
