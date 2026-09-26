/*
 * かな入力 behavior (&kana)。
 *
 * `&kana KN_KA` のように文字 ID を受け取り、対応するローマ字のキー列を
 * ZMK の behavior queue（マクロと同じ仕組み）経由で &kp として順に送る。
 * かな漢字変換はホスト側の IME（ローマ字入力モード）が行う。
 * ホストのキーボード配列は英語配列 (US) を前提にしている。
 * 記号の一部（… 『』）は Google 日本語入力の標準ローマ字テーブル（z. z[ z]）を前提にしている。
 * 親指キーのタップ（スペース／エンター）も同じキューで送り、かな列を追い越さないようにする。
 * キューが満杯で「離す」を登録できなかったときは、登録できるまで再試行してキーの押しっぱなしを防ぐ。
 */

#define DT_DRV_COMPAT zmk_behavior_kana

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>

#include <dt-bindings/kana/kana.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KANA_MAX_KEYS 4

struct kana_entry {
    uint8_t len;
    uint32_t keys[KANA_MAX_KEYS];
};

#define K1(a) {.len = 1, .keys = {a}}
#define K2(a, b) {.len = 2, .keys = {a, b}}
#define K3(a, b, c) {.len = 3, .keys = {a, b, c}}
#define K4(a, b, c, d) {.len = 4, .keys = {a, b, c, d}}

/* 文字 ID → 送るキー列（英語配列・ローマ字入力） */
static const struct kana_entry kana_table[KN_MAX_ID + 1] = {
    [KN_A] = K1(A),        [KN_I] = K1(I),        [KN_U] = K1(U),        [KN_E] = K1(E),
    [KN_O] = K1(O),        [KN_KA] = K2(K, A),    [KN_KI] = K2(K, I),    [KN_KU] = K2(K, U),
    [KN_KE] = K2(K, E),    [KN_KO] = K2(K, O),    [KN_SA] = K2(S, A),    [KN_SI] = K2(S, I),
    [KN_SU] = K2(S, U),    [KN_SE] = K2(S, E),    [KN_SO] = K2(S, O),    [KN_TA] = K2(T, A),
    [KN_TI] = K2(T, I),    [KN_TU] = K2(T, U),    [KN_TE] = K2(T, E),    [KN_TO] = K2(T, O),
    [KN_NA] = K2(N, A),    [KN_NI] = K2(N, I),    [KN_NU] = K2(N, U),    [KN_NE] = K2(N, E),
    [KN_NO] = K2(N, O),    [KN_HA] = K2(H, A),    [KN_HI] = K2(H, I),    [KN_HU] = K2(H, U),
    [KN_HE] = K2(H, E),    [KN_HO] = K2(H, O),    [KN_MA] = K2(M, A),    [KN_MI] = K2(M, I),
    [KN_MU] = K2(M, U),    [KN_ME] = K2(M, E),    [KN_MO] = K2(M, O),    [KN_YA] = K2(Y, A),
    [KN_YU] = K2(Y, U),    [KN_YO] = K2(Y, O),    [KN_RA] = K2(R, A),    [KN_RI] = K2(R, I),
    [KN_RU] = K2(R, U),    [KN_RE] = K2(R, E),    [KN_RO] = K2(R, O),    [KN_WA] = K2(W, A),
    [KN_WO] = K2(W, O),    [KN_NN] = K2(N, N),

    [KN_GA] = K2(G, A),    [KN_GI] = K2(G, I),    [KN_GU] = K2(G, U),    [KN_GE] = K2(G, E),
    [KN_GO] = K2(G, O),    [KN_ZA] = K2(Z, A),    [KN_ZI] = K2(Z, I),    [KN_ZU] = K2(Z, U),
    [KN_ZE] = K2(Z, E),    [KN_ZO] = K2(Z, O),    [KN_DA] = K2(D, A),    [KN_DI] = K2(D, I),
    [KN_DU] = K2(D, U),    [KN_DE] = K2(D, E),    [KN_DO] = K2(D, O),    [KN_BA] = K2(B, A),
    [KN_BI] = K2(B, I),    [KN_BU] = K2(B, U),    [KN_BE] = K2(B, E),    [KN_BO] = K2(B, O),

    [KN_PA] = K2(P, A),    [KN_PI] = K2(P, I),    [KN_PU] = K2(P, U),    [KN_PE] = K2(P, E),
    [KN_PO] = K2(P, O),

    [KN_XYA] = K3(X, Y, A), [KN_XYU] = K3(X, Y, U), [KN_XYO] = K3(X, Y, O), [KN_XA] = K2(X, A),
    [KN_XI] = K2(X, I),     [KN_XU] = K2(X, U),     [KN_XE] = K2(X, E),     [KN_XO] = K2(X, O),
    [KN_XTU] = K3(X, T, U), [KN_VU] = K2(V, U),

    [KN_TOUTEN] = K1(COMMA),         /* 、 */
    [KN_KUTEN] = K1(DOT),            /* 。 */
    [KN_CHOUON] = K1(MINUS),         /* ー */
    [KN_SANTEN] = K2(Z, DOT),        /* … （Google 日本語入力: z.） */
    [KN_EXCL] = K1(EXCLAMATION),     /* ！ */
    [KN_QUES] = K1(QUESTION),        /* ？ */
    [KN_KAGI] = K2(LBKT, RBKT),      /* 「」 */
    [KN_NIJUKAGI] = K4(Z, LBKT, Z, RBKT), /* 『』 （Google 日本語入力: z[ z]） */
    [KN_PAREN] = K2(LPAR, RPAR),     /* （） */

    [KN_SPACE] = K1(SPACE),
    [KN_ENTER] = K1(ENTER),
};

struct behavior_kana_config {
    uint32_t tap_ms;
    uint32_t wait_ms;
};

#define KP_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(kp))

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

#define KV(id, name)                                                                               \
    {.display_name = name, .value = id, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE}

static const struct behavior_parameter_value_metadata param_values[] = {
    KV(KN_A, "あ"),   KV(KN_I, "い"),   KV(KN_U, "う"),   KV(KN_E, "え"),   KV(KN_O, "お"),
    KV(KN_KA, "か"),  KV(KN_KI, "き"),  KV(KN_KU, "く"),  KV(KN_KE, "け"),  KV(KN_KO, "こ"),
    KV(KN_SA, "さ"),  KV(KN_SI, "し"),  KV(KN_SU, "す"),  KV(KN_SE, "せ"),  KV(KN_SO, "そ"),
    KV(KN_TA, "た"),  KV(KN_TI, "ち"),  KV(KN_TU, "つ"),  KV(KN_TE, "て"),  KV(KN_TO, "と"),
    KV(KN_NA, "な"),  KV(KN_NI, "に"),  KV(KN_NU, "ぬ"),  KV(KN_NE, "ね"),  KV(KN_NO, "の"),
    KV(KN_HA, "は"),  KV(KN_HI, "ひ"),  KV(KN_HU, "ふ"),  KV(KN_HE, "へ"),  KV(KN_HO, "ほ"),
    KV(KN_MA, "ま"),  KV(KN_MI, "み"),  KV(KN_MU, "む"),  KV(KN_ME, "め"),  KV(KN_MO, "も"),
    KV(KN_YA, "や"),  KV(KN_YU, "ゆ"),  KV(KN_YO, "よ"),  KV(KN_RA, "ら"),  KV(KN_RI, "り"),
    KV(KN_RU, "る"),  KV(KN_RE, "れ"),  KV(KN_RO, "ろ"),  KV(KN_WA, "わ"),  KV(KN_WO, "を"),
    KV(KN_NN, "ん"),  KV(KN_GA, "が"),  KV(KN_GI, "ぎ"),  KV(KN_GU, "ぐ"),  KV(KN_GE, "げ"),
    KV(KN_GO, "ご"),  KV(KN_ZA, "ざ"),  KV(KN_ZI, "じ"),  KV(KN_ZU, "ず"),  KV(KN_ZE, "ぜ"),
    KV(KN_ZO, "ぞ"),  KV(KN_DA, "だ"),  KV(KN_DI, "ぢ"),  KV(KN_DU, "づ"),  KV(KN_DE, "で"),
    KV(KN_DO, "ど"),  KV(KN_BA, "ば"),  KV(KN_BI, "び"),  KV(KN_BU, "ぶ"),  KV(KN_BE, "べ"),
    KV(KN_BO, "ぼ"),  KV(KN_PA, "ぱ"),  KV(KN_PI, "ぴ"),  KV(KN_PU, "ぷ"),  KV(KN_PE, "ぺ"),
    KV(KN_PO, "ぽ"),  KV(KN_XYA, "ゃ"), KV(KN_XYU, "ゅ"), KV(KN_XYO, "ょ"), KV(KN_XA, "ぁ"),
    KV(KN_XI, "ぃ"),  KV(KN_XU, "ぅ"),  KV(KN_XE, "ぇ"),  KV(KN_XO, "ぉ"),  KV(KN_XTU, "っ"),
    KV(KN_VU, "ゔ"),  KV(KN_TOUTEN, "、"), KV(KN_KUTEN, "。"), KV(KN_CHOUON, "ー"),
    KV(KN_SANTEN, "…"), KV(KN_EXCL, "！"), KV(KN_QUES, "？"), KV(KN_KAGI, "「」"),
    KV(KN_NIJUKAGI, "『』"), KV(KN_PAREN, "（）"),
    KV(KN_SPACE, "スペース"), KV(KN_ENTER, "エンター"),
};

BUILD_ASSERT(ARRAY_SIZE(param_values) == KN_MAX_ID, "kana metadata must cover every kana ID");

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/*
 * 「押す」を登録済みで「離す」を登録できなかったキー。
 * 登録できるまで retry_work で再試行する（キーが押されたままになるのを防ぐ）。
 * behavior のコールバックと k_work はどちらもシステムワークキューで動くので排他は不要。
 */
static bool release_pending;
static struct zmk_behavior_binding pending_release_binding;
static struct zmk_behavior_binding_event pending_release_event;
static uint32_t pending_release_wait;

static void retry_pending_release(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_work, retry_pending_release);

#define RETRY_INTERVAL_MS 10

static void retry_pending_release(struct k_work *work) {
    if (!release_pending) {
        return;
    }
    if (zmk_behavior_queue_add(&pending_release_event, pending_release_binding, false,
                               pending_release_wait) < 0) {
        k_work_schedule(&retry_work, K_MSEC(RETRY_INTERVAL_MS));
        return;
    }
    release_pending = false;
}

static int on_kana_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kana_config *cfg = dev->config;
    const uint32_t id = binding->param1;

    if (id == 0 || id > KN_MAX_ID || kana_table[id].len == 0) {
        LOG_WRN("Unknown kana id %d", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (release_pending) {
        // 前のキーの「離す」が未登録のうちは、順番が崩れないよう新しい入力を捨てる
        LOG_WRN("Behavior queue busy, dropped kana id %d", id);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const struct kana_entry *entry = &kana_table[id];
    for (int i = 0; i < entry->len; i++) {
        struct zmk_behavior_binding kp = {
            .behavior_dev = KP_BEHAVIOR_NAME,
            .param1 = entry->keys[i],
        };

        if (zmk_behavior_queue_add(&event, kp, true, cfg->tap_ms) < 0) {
            // 「押す」を登録できなかった: 何も押されないので、ここで打ち切るだけでよい
            LOG_ERR("Behavior queue full, dropped rest of kana id %d", id);
            break;
        }

        if (zmk_behavior_queue_add(&event, kp, false, cfg->wait_ms) < 0) {
            // 「押す」だけ登録された: 「離す」を登録できるまで再試行する
            LOG_ERR("Behavior queue full, retrying release for kana id %d", id);
            release_pending = true;
            pending_release_binding = kp;
            pending_release_event = event;
            pending_release_wait = cfg->wait_ms;
            k_work_schedule(&retry_work, K_MSEC(RETRY_INTERVAL_MS));
            break;
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_kana_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_kana_driver_api = {
    .binding_pressed = on_kana_binding_pressed,
    .binding_released = on_kana_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KANA_INST(n)                                                                               \
    static const struct behavior_kana_config behavior_kana_config_##n = {                         \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kana_config_##n, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kana_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KANA_INST)
