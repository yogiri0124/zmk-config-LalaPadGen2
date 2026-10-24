/*
 * Windows の Alt コードで文字を送る behavior（zmk,behavior-alt-code、引数なし）。
 * keymap で codes = <33116> などを付けたインスタンスを作って使う。
 *
 * codes の各値を「Alt を押したまま、テンキーでその10進数を打つ」操作として順に送り、
 * 最後に ← を cursor-left 回送る（カッコの間にカーソルを置く用。不要なら 0）。
 * IME の状態やローマ字テーブルに関係なく、確定済みの文字として入る（Windows の機能）。
 *
 * commit-ime を付けたインスタンスは、Alt コードの前に IME オフ (LANGUAGE_2)、後に IME オン (LANGUAGE_1) を送る。
 * Alt コードの文字は IME を通らずアプリに直接入るので、確定前の文字（下線付き）があるときに送ると、
 * IME のカーソルが確定前の文字の末尾に残り、続きがその文字の前に入ってしまう。IME オフで確定前の文字を
 * 確定させてから送ることで、これを防ぐ（Google 日本語入力では IME オフで確定される）。
 * IME がオフのときに押した場合も、送り終えた後は IME オンになる。
 *
 * テンキーの数字は Num Lock がオフだと矢印キーなどとして扱われるため、
 * PC から届いている Num Lock の点灯状態（HID インジケーター）を見て、オフのときだけ
 * 前後で Num Lock を押して一時的にオンにし、送り終わったら元に戻す。
 *
 * - Alt（左右どちらか）がすでに押されている間（Alt キーを押したまま、mod_lock_alt で押しっぱなし など）は
 *   送らずに捨てる。ZMK は押した数で修飾キーを数えるので、ここで Alt を離しても PC 側の Alt は離れず、
 *   Alt コードの区切りがつかない（2つ目以降の値が1つ目に続いて打たれ、違う文字になる）ため。
 * - 送る操作は開始時に一覧（steps）にまとめ、k_work で1つずつ順に送る。behavior queue を
 *   使わないので、キューが満杯で「離す」や Num Lock の復元が抜けることはない。
 * - 押したら、まず behavior queue の末尾に目印を並べ（kana_output_add_marker）、それより前に並んでいた
 *   &kana_key・記号・マクロの送信がすべて終わってから送り始める（先に打ったかなを追い越さない）。
 *   目印を並べられないとき（キューが満杯、または「離す」の再試行中）は、その押下は送らない
 *   （送り始める前なので後始末は要らない。かな・記号も、並べられなければ送らない）。
 *   かな入力のモジュールがないビルドでは、すぐに送り始める。
 * - 送信中に押された分は待ち行列に入れ、前の送信が終わってから順に送る。
 * - 送信中にほかから届いたキー入力（通常のキー、&kana_key やマクロの送信など）は、いったん預かり
 *   （ZMK の zmk_keycode_state_changed を capture する）、送り終えてから届いた順のまま通す。
 *   Alt を押したままの間に通常のキーが PC に届くと Alt+キー（メニュー操作など）になってしまうため。
 *   - 「押す」も「離す」も、届いた順のまま預かって、そのまま通す（押した数を数える修飾キーも崩れない）。
 *   - 次の送信が待っているときは、その送信の目印より前に届いた分だけを先に通してから、次を送り始める
 *     （預かった入力と、続けて押した Alt コードのキーの順番を守る）。
 *   - 例外として、PC 側ですでに押されているキーの「離す」で、同じキーのイベントが待ち行列に
 *     （通している最中のものも含めて）ないものは、預からずにそのまま通す（送信を始める前から押して
 *     いたキーが、押しっぱなしのまま Alt と重ならないように）。PC 側で押されているキーは、この
 *     listener を通った入力と、預かって通し終えた入力から数えておく（held）。
 *   - 待ち行列（MAX_CAPTURED 件）があふれたら、その分は預からずにそのまま通す（1回の送信の間に
 *     これほどの入力は届かないので、あふれたときの順番の崩れには備えない）。
 *   - 制約: 送信中（数十〜数百 ms）に押したキーの behavior は、押した時点の PC 側の状態（押されている
 *     修飾キーなど）を見る。預かった入力はまだ反映されていないので、mod-morph や &kt のように押した
 *     時点の修飾キーの状態で動きを変える behavior は、送信中に押すと違う動きになることがある
 *     （このキーマップでは使っていない。自作の key-lock は自分で状態を持つので影響を受けない）。
 * - 送信の途中で接続先（USB・BLE プロファイル）が変わったら、残りは送らずに中止する
 *   （別の PC に数字や Num Lock の復元を送らないため。操作を1つ送るたびに、始めたときの接続先と比べる）。
 *   送信中に押されて待っていた分も、押したときと接続先が違えば送らない。
 *   制約: ZMK v0.3.0 の BLE 送信（app/src/hog.c）はレポートをキューに入れ、取り出す時点の
 *   アクティブなプロファイルへ送るので、切り替えの直前に送った数操作は切り替え先に届くことがある
 *   （通常のキーやマクロと同じ ZMK 本体の動作）。そのため中止したときは、下の Num Lock の記録を消して
 *   PC から届く点灯状態に従う。
 * - Num Lock を切り替えた直後は、PC からの点灯状態の報告が遅れて古い状態が残っていることがある
 *   （続けて押したときに、オンにした報告が残っていると、オフなのに切り替えずに送ってしまう）。
 *   そのため、Num Lock をオンにして最後まで送り、オフに戻したときは「この接続先は ASSUME_MS の間
 *   Num Lock オフ」と記録し、その間は PC の報告より記録を使う
 *   （この間に別のキーボードなどで Num Lock を切り替えた場合は反映されない）。
 *
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので、送信の状態の排他は不要。
 */

#define DT_DRV_COMPAT zmk_behavior_alt_code

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include <dt-bindings/zmk/keys.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_KANA)
#include "kana_output.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* HID の LED レポートのビット0 が Num Lock（HID Usage Tables: LED Page 0x01） */
#define HID_INDICATOR_NUM_LOCK BIT(0)

/* 1つの値の最大桁数（uint32_t の10進数） */
#define MAX_DIGITS 10
/* 1回の送信で送る操作（押す／離す）の上限。設定がこれに収まるかはビルド時に確かめる */
#define MAX_STEPS 64
/* 1つの値で送る操作の最大数（Alt の押す・離すと、最大 MAX_DIGITS 桁のタップ） */
#define MAX_STEPS_PER_CODE (2 + 2 * MAX_DIGITS)
/* IME オフ・オンと Num Lock の前後のタップで送る操作の最大数 */
#define MAX_STEPS_EXTRA 8
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
    bool commit_ime;
    uint32_t ime_wait_ms;
};

struct step {
    uint32_t keycode;
    bool press;
    /* この操作の後に、通常の待ち時間に加えて待つ時間（IME の切り替えが済むのを待つ用） */
    uint32_t extra_wait_ms;
};

static const uint32_t keypad_digits[10] = {
    KP_N0, KP_N1, KP_N2, KP_N3, KP_N4, KP_N5, KP_N6, KP_N7, KP_N8, KP_N9,
};

/* ---- 送信中に預かるキー入力 ---- */

/* 預かれるイベントの数（送信中に、&kana_key の送信待ちがまとめて流れてくることもあるので多めに持つ） */
#define MAX_CAPTURED 128
/* PC 側で押されているキーとして数えておける種類の数 */
#define MAX_HELD 32

/* キー（usage page とキーコード）ごとの数 */
struct key_count {
    uint16_t usage_page;
    uint32_t keycode;
    uint8_t count;
};

/* 以下はすべて capture_lock で守る（キー入力はほかのスレッドから届くこともあるため） */
static struct k_spinlock capture_lock;
/* 預かる状態か（送信中と、預かったものを通し終えるまで） */
static bool capturing;
/* 預かったイベント（ZMK のイベントは送り手のスタックにあるので、中身を写して持つ）。
 * 先頭 captured_head から captured_count 個の輪状の待ち行列。先頭は、通し終えるまで取り除かない */
static struct zmk_keycode_state_changed_event captured[MAX_CAPTURED];
static size_t captured_head;
static size_t captured_count;
/* これまでに預かった入力の通し番号（次に預かる入力の番号）。先頭の番号は captured_pushed - captured_count */
static uint32_t captured_pushed;
/* PC 側で押されているキーの数（この listener を通した入力と、預かって通し終えた入力から数える） */
static struct key_count held[MAX_HELD];

/* 自分のキー入力を送っているスレッド（送っている間だけ。それ以外は NULL） */
static k_tid_t raising_thread;

/* ---- 送信の状態 ---- */

static bool busy;
static const struct behavior_alt_code_config *active_cfg;
static struct step steps[MAX_STEPS];
static size_t step_count;
static size_t step_index;
/* この送信で、前後に Num Lock を押して一時的にオンにする */
static bool num_lock_toggled;
static struct zmk_endpoint_instance active_endpoint;
/* この送信でいま押したままにしているキー（中止するときに離す） */
static bool alt_down;
static uint32_t key_down;

/* Num Lock をオフに戻した記録: assumed_endpoint は assume_until まで Num Lock オフとみなす */
static bool assumed_off;
static struct zmk_endpoint_instance assumed_endpoint;
static int64_t assume_until;

struct pending_press {
    const struct behavior_alt_code_config *cfg;
    struct zmk_endpoint_instance endpoint; /* 押したときの接続先 */
    uint32_t id;             /* 目印の番号 */
    bool ready;              /* 目印が処理された（それより前の送信が終わった） */
    uint32_t mark;           /* 目印が処理されたときまでに預かった入力の数（これより前の分を先に通す） */
};

static struct pending_press pending[MAX_PENDING];
static size_t pending_count;
static uint32_t next_pending_id;

static void alt_code_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(alt_code_work, alt_code_work_handler);

/* key_count の表で、そのキーの欄を探す。なければ NULL */
static struct key_count *find_key(struct key_count *table, size_t len,
                                  const struct zmk_keycode_state_changed *ev) {
    for (size_t i = 0; i < len; i++) {
        if (table[i].count > 0 && table[i].usage_page == ev->usage_page &&
            table[i].keycode == ev->keycode) {
            return &table[i];
        }
    }
    return NULL;
}

/* key_count の表で、そのキーの数を1増やす。表が満杯なら false */
static bool add_key(struct key_count *table, size_t len, const struct zmk_keycode_state_changed *ev) {
    struct key_count *k = find_key(table, len, ev);
    if (k == NULL) {
        for (size_t i = 0; i < len; i++) {
            if (table[i].count == 0) {
                k = &table[i];
                k->usage_page = ev->usage_page;
                k->keycode = ev->keycode;
                break;
            }
        }
    }
    if (k == NULL || k->count == UINT8_MAX) {
        return false;
    }
    k->count++;
    return true;
}

/* key_count の表で、そのキーの数を1減らす（そのキーがなければ何もしない） */
static void remove_key(struct key_count *table, size_t len,
                       const struct zmk_keycode_state_changed *ev) {
    struct key_count *k = find_key(table, len, ev);
    if (k != NULL) {
        k->count--;
    }
}

/* 待ち行列に（通している最中の先頭も含めて）同じキーのイベントがあるか。ロックを取った状態で呼ぶ */
static bool same_key_queued(const struct zmk_keycode_state_changed *ev) {
    for (size_t i = 0; i < captured_count; i++) {
        const struct zmk_keycode_state_changed *c =
            &captured[(captured_head + i) % MAX_CAPTURED].data;
        if (c->usage_page == ev->usage_page && c->keycode == ev->keycode) {
            return true;
        }
    }
    return false;
}

/* PC 側へ通した入力で、押されているキーの数を合わせる。ロックを取った状態で呼ぶ */
static void track_held(const struct zmk_keycode_state_changed *ev) {
    if (ev->state) {
        if (!add_key(held, MAX_HELD, ev)) {
            LOG_WRN("Too many held keys to track (0x%02X)", ev->keycode);
        }
    } else {
        remove_key(held, MAX_HELD, ev);
    }
}

/* 自分の送るキー入力。預からずに PC へ通すよう印を付けて送る（押して離すので held には数えない） */
static void raise_own(uint32_t keycode, bool pressed, int64_t timestamp) {
    raising_thread = k_current_get();
    raise_zmk_keycode_state_changed_from_encoded(keycode, pressed, timestamp);
    raising_thread = NULL;
}

/* 送信を始める: ここからのほかのキー入力を預かる */
static void start_capturing(void) {
    k_spinlock_key_t key = k_spin_lock(&capture_lock);
    capturing = true;
    k_spin_unlock(&capture_lock, key);
}

/* 預かったキー入力を、届いた順のまま PC へ通す。
 * all = true: 全部通して、預かるのをやめる（送信がすべて終わったとき）。
 * all = false: 通し番号が mark より前の分だけ通す（次の送信の目印より前に届いた分）。預かるのは続ける。
 * 先頭を写してロックの外で通し、通し終えてから待ち行列から取り除いて held に数える。
 * その間に届いた入力は後ろに並ぶ */
static void release_captured(bool all, uint32_t mark) {
    while (true) {
        struct zmk_keycode_state_changed_event ev;
        k_spinlock_key_t key = k_spin_lock(&capture_lock);
        const uint32_t head_seq = captured_pushed - captured_count;
        if (captured_count == 0 || (!all && (int32_t)(head_seq - mark) >= 0)) {
            if (all) {
                capturing = false;
                captured_head = 0;
            }
            k_spin_unlock(&capture_lock, key);
            return;
        }
        ev = captured[captured_head];
        k_spin_unlock(&capture_lock, key);

        // 預かったところ（このモジュールの listener）の次から、残りの listener に通す
        ZMK_EVENT_RELEASE(ev);

        key = k_spin_lock(&capture_lock);
        captured_head = (captured_head + 1) % MAX_CAPTURED;
        captured_count--;
        track_held(&ev.data);
        k_spin_unlock(&capture_lock, key);
    }
}

static bool reported_num_lock(struct zmk_endpoint_instance endpoint) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    return (zmk_hid_indicators_get_profile(endpoint) & HID_INDICATOR_NUM_LOCK) != 0;
#else
    // 点灯状態を受け取れない設定ではオンとみなし、そのまま送る
    ARG_UNUSED(endpoint);
    return true;
#endif
}

/* 接続先 endpoint の今の Num Lock の状態。オフに戻してから ASSUME_MS の間は記録を使う
 * （一時的にオンにした報告が、元に戻した報告より後から届くこともあるため、途中で報告に切り替えない） */
static bool current_num_lock(struct zmk_endpoint_instance endpoint) {
    if (assumed_off && zmk_endpoint_instance_eq(endpoint, assumed_endpoint) &&
        k_uptime_get() < assume_until) {
        return false;
    }
    return reported_num_lock(endpoint);
}

static void add_step(uint32_t keycode, bool press) {
    steps[step_count++] = (struct step){.keycode = keycode, .press = press};
}

/* タップして、離した後に extra_wait_ms だけ余分に待つ */
static void add_tap_then_wait(uint32_t keycode, uint32_t extra_wait_ms) {
    add_step(keycode, true);
    steps[step_count++] =
        (struct step){.keycode = keycode, .press = false, .extra_wait_ms = extra_wait_ms};
}

static void add_tap(uint32_t keycode) {
    add_step(keycode, true);
    add_step(keycode, false);
}

/* 送る操作の一覧を作る（MAX_STEPS に収まることはビルド時に確かめてある） */
static void build_steps(const struct behavior_alt_code_config *cfg) {
    step_count = 0;
    if (cfg->commit_ime) {
        // 確定前の文字を確定させる（IME オフ）。IME の処理が済むのを少し待つ
        add_tap_then_wait(LANGUAGE_2, cfg->ime_wait_ms);
    }
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
    if (cfg->commit_ime) {
        // 続きを日本語で打てるように IME オンに戻す
        add_tap(LANGUAGE_1);
    }
}

/* 待ち行列の先頭から次の送信を始める。先頭の目印がまだ処理されていなければ、処理されるまで待つ。
 * 待ち行列が空なら、預かった入力をすべて通して待機状態に戻る */
static void start_next(void) {
    while (pending_count > 0) {
        if (!pending[0].ready) {
            // 先頭の目印がまだ（それより前に並んだかななどの送信が終わっていない）: 目印の処理で呼び直される
            busy = false;
            active_cfg = NULL;
            return;
        }
        // この送信の目印より前に届いて預かっていた入力を、先に通す
        release_captured(false, pending[0].mark);

        const struct behavior_alt_code_config *cfg = pending[0].cfg;
        const struct zmk_endpoint_instance pressed_endpoint = pending[0].endpoint;
        pending_count--;
        memmove(&pending[0], &pending[1], pending_count * sizeof(pending[0]));

        active_endpoint = zmk_endpoints_selected();
        if (!zmk_endpoint_instance_eq(pressed_endpoint, active_endpoint)) {
            // 押した後に接続先が切り替わった: 押し直していない入力は送らない
            LOG_WRN("Endpoint changed since alt code key press, dropping");
            continue;
        }
        if (zmk_hid_get_explicit_mods() & (MOD_LALT | MOD_RALT)) {
            // Alt がすでに押されている: 自分の Alt を離しても区切れないので送らない
            LOG_WRN("Alt is already held, dropping alt code");
            continue;
        }
        num_lock_toggled = !current_num_lock(active_endpoint);
        build_steps(cfg);
        start_capturing();
        busy = true;
        active_cfg = cfg;
        step_index = 0;
        alt_down = false;
        key_down = 0;
        k_work_schedule(&alt_code_work, K_NO_WAIT);
        return;
    }
    busy = false;
    active_cfg = NULL;
    release_captured(true, 0);
}

/* 目印が処理された（それより前に並んだ送信がすべて終わった）: その分の送信を始められる */
static void alt_code_on_marker(uint32_t id) {
    for (size_t i = 0; i < pending_count; i++) {
        if (pending[i].id == id && !pending[i].ready) {
            k_spinlock_key_t key = k_spin_lock(&capture_lock);
            pending[i].mark = captured_pushed;
            k_spin_unlock(&capture_lock, key);
            pending[i].ready = true;
            break;
        }
    }
    if (!busy) {
        start_next();
    }
}

static void alt_code_work_handler(struct k_work *work) {
    if (!zmk_endpoint_instance_eq(zmk_endpoints_selected(), active_endpoint)) {
        // 途中（最後の操作の後の待ち時間も含む）で接続先が変わった: 残りは送らず、記録も消す。
        // 押したままのキーは離しておく（ZMK は切り替えで PC へ送る状態を消すが、修飾キーの押した数は
        // 残るので、離して数を戻す）
        LOG_WRN("Endpoint changed during alt code sequence, aborting");
        const int64_t now = k_uptime_get();
        if (key_down != 0) {
            raise_own(key_down, false, now);
        }
        if (alt_down) {
            raise_own(LALT, false, now);
        }
        key_down = 0;
        alt_down = false;
        assumed_off = false;
        start_next();
        return;
    }

    if (step_index < step_count) {
        const struct step *s = &steps[step_index++];
        raise_own(s->keycode, s->press, k_uptime_get());
        if (s->keycode == LALT) {
            alt_down = s->press;
        } else {
            key_down = s->press ? s->keycode : 0;
        }
        const uint32_t delay =
            (s->press ? active_cfg->tap_ms : active_cfg->wait_ms) + s->extra_wait_ms;
        k_work_schedule(&alt_code_work, K_MSEC(delay));
        return;
    }

    if (num_lock_toggled) {
        // 最後まで送って Num Lock をオフに戻した: PC の報告が追いつくまで、オフとみなす
        assumed_off = true;
        assumed_endpoint = active_endpoint;
        assume_until = k_uptime_get() + ASSUME_MS;
    }
    start_next();
}

/* 送信中に届いたほかのキー入力を預かる。送信中でないときは、PC 側で押されているキーを数えるだけ。
 * この listener は ZMK 本体の HID への反映（hid_listener）より前に呼ばれる（このモジュールの
 * listener は ZMK 本体より先にリンクされるため）。 */
static int alt_code_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (raising_thread == k_current_get()) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    int ret = ZMK_EV_EVENT_BUBBLE;
    k_spinlock_key_t key = k_spin_lock(&capture_lock);
    if (!capturing) {
        track_held(ev);
    } else if (!ev->state && find_key(held, MAX_HELD, ev) != NULL && !same_key_queued(ev)) {
        // PC 側で押されているキーの「離す」で、同じキーのイベントが待ち行列にないもの: そのまま通す
        // （送信を始める前から押していたキーが、Alt と重なって押しっぱなしにならないように）。
        // 同じキーのイベントが待っていれば、それを追い越さないよう後ろに並べる（下）
        track_held(ev);
    } else if (captured_count < MAX_CAPTURED) {
        // 「押す」と、それ以外の「離す」: 届いた順に後ろに並べる
        const size_t tail = (captured_head + captured_count) % MAX_CAPTURED;
        captured[tail] = copy_raised_zmk_keycode_state_changed(ev);
        captured_count++;
        captured_pushed++;
        ret = ZMK_EV_EVENT_CAPTURED;
    } else {
        // 待ち行列があふれた（1回の送信の間には現実には起きない）: 預からずに通す
        LOG_WRN("Alt code capture queue full, passing 0x%02X", ev->keycode);
        track_held(ev);
    }
    k_spin_unlock(&capture_lock, key);
    return ret;
}

ZMK_LISTENER(behavior_alt_code_keys, alt_code_keycode_listener);
ZMK_SUBSCRIPTION(behavior_alt_code_keys, zmk_keycode_state_changed);

static int on_alt_code_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_alt_code_config *cfg = dev->config;

    if (pending_count >= MAX_PENDING) {
        LOG_WRN("Too many pending alt code presses, ignoring");
        return ZMK_BEHAVIOR_OPAQUE;
    }
    const uint32_t id = next_pending_id++ & 0x7FFFFFFF;
    pending[pending_count++] = (struct pending_press){
        .cfg = cfg,
        .endpoint = zmk_endpoints_selected(),
        .id = id,
    };
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_KANA)
    // behavior queue の末尾に目印を並べる。それより前の送信（かななど）が終わったら alt_code_on_marker が
    // 呼ばれる（キューが空なら、この中ですぐ呼ばれる）。並べられなければ、この押下は取り消す
    // （並べられないのは満杯か「離す」の再試行中で、すぐには処理されないので、この分は待ち行列の末尾に残っている）
    kana_output_set_marker_handler(alt_code_on_marker);
    if (kana_output_add_marker(&event, id) < 0) {
        pending_count--;
        LOG_WRN("Behavior queue busy/full, dropped alt code press");
    }
#else
    // かな入力のモジュールがない: 待つ送信もないので、すぐに送り始めてよい
    alt_code_on_marker(id);
#endif
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

/* 時間の設定は 0〜10 秒（負の値や大きすぎる値で、待ち時間の計算がおかしくならないように） */
#define ALT_CODE_CHECK_MS(n, prop)                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, prop) >= 0 && DT_INST_PROP(n, prop) <= 10000,                     \
                 "alt code: " #prop " must be 0-10000 ms");

#define ALT_CODE_INST(n)                                                                           \
    ALT_CODE_CHECK_MS(n, tap_ms)                                                                   \
    ALT_CODE_CHECK_MS(n, wait_ms)                                                                  \
    ALT_CODE_CHECK_MS(n, ime_wait_ms)                                                              \
    BUILD_ASSERT(DT_INST_PROP(n, cursor_left) >= 0 &&                                              \
                     DT_INST_PROP_LEN(n, codes) * MAX_STEPS_PER_CODE +                             \
                             2 * DT_INST_PROP(n, cursor_left) + MAX_STEPS_EXTRA <=                 \
                         MAX_STEPS,                                                                \
                 "alt code: too many codes or cursor-left");                                       \
    static const uint32_t behavior_alt_code_codes_##n[] = DT_INST_PROP(n, codes);                  \
    static const struct behavior_alt_code_config behavior_alt_code_config_##n = {                 \
        .codes = behavior_alt_code_codes_##n,                                                      \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .cursor_left = DT_INST_PROP(n, cursor_left),                                               \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .commit_ime = DT_INST_PROP(n, commit_ime),                                                 \
        .ime_wait_ms = DT_INST_PROP(n, ime_wait_ms),                                               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_alt_code_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_alt_code_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ALT_CODE_INST)
