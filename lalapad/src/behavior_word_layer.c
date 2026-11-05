/*
 * 打っている間だけ続くレイヤー（zmk,behavior-word-layer、引数 1 つ: レイヤー番号。Num Word に使う）。
 *
 * 押すとレイヤーを有効にし、continue-list のキー（と ignore-* で指定した種類のキー）を押している間は続ける。
 * それ以外のキー（キーコード）の「押す」が届いたら、レイヤーを解除する。有効な間にもう一度押すと解除する。
 * urob/zmk-auto-layer（MIT。ZMK Contributors）の auto-layer と同じ動きに、次を足したもの:
 * - ほかの操作（&mo のキーを離した、&to など）でそのレイヤーが解除されたら、こちらも終わる
 *   （終わったことを知らないまま、次に押したときに「解除」として扱わないように）。
 * - exit-layers のレイヤーが有効になったら終える（Num Word の間に記号レイヤーを押したとき、数字レイヤーと
 *   記号レイヤーが同時に有効になって、conditional layer でシステムレイヤー（bootloader・BT_CLR など）に
 *   ならないように。33 の長押しのように両方を押していると自覚できる場合と違い、Num Word は見えないため）。
 * - exit-layers のレイヤーがすでに有効なときは始めない（記号レイヤーを押したまま 33 をタップしたときに、
 *   システムレイヤーにならないように）。
 * - momentary を付けたインスタンスは、押している間だけレイヤーを有効にする（&mo と同じ。hold-tap の長押しに使う）。
 *   同じレイヤーの Num Word が有効なときに押したら、Num Word を終えずにレイヤーを引き継ぐ（Num Word の間に
 *   33 を押し続けたら、ほかのキーや記号レイヤーを押しても数字レイヤーのまま。33 と 38 でシステムレイヤーになる）。
 * - Alt コードのキーが自分で送るキー（IME オフ／オン・Alt・テンキーの数字など）では終えない（送っている間に
 *   始めた Num Word が、送信の最後の IME オンで終わらないように。Alt コードのキーを押したときは、
 *   word_layer_finish_all で終える）。
 * - ほかのモジュールから終えられる（word_layer_finish_all。Alt コードのキーを押したときなど。Alt コードは
 *   Alt とテンキーの数字を送るので、数字・修飾キーで続く設定だと、送っても終わらないため）。
 *
 * 状態はシステムワークキューで触る（behavior のコールバックと listener）。
 */

#define DT_DRV_COMPAT zmk_behavior_word_layer

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keys.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/modifiers.h>

#include "alt_code.h"
#include "word_layer.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* 同時に有効にしておけるレイヤーの数 */
#define MAX_ACTIVE 4

struct word_layer_key {
    zmk_mod_flags_t modifiers;
    uint16_t page;
    uint16_t id;
};

struct behavior_word_layer_config {
    const struct word_layer_key *continue_keys;
    size_t num_continue_keys;
    bool ignore_alphas;
    bool ignore_numbers;
    bool ignore_modifiers;
    bool momentary;
    const zmk_keymap_layer_id_t *exit_layers;
    size_t num_exit_layers;
};

struct active_word_layer {
    bool active;
    zmk_keymap_layer_id_t layer;
    const struct behavior_word_layer_config *config;
};

static struct active_word_layer actives[MAX_ACTIVE];

static struct active_word_layer *find_active(zmk_keymap_layer_id_t layer) {
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (actives[i].active && actives[i].layer == layer) {
            return &actives[i];
        }
    }
    return NULL;
}

/* 終える（印を先に外す: 解除の出来事を受けた listener が、もう一度終えようとしないように） */
static void finish(struct active_word_layer *w) {
    w->active = false;
    zmk_keymap_layer_deactivate(w->layer);
}

void word_layer_finish_all(void) {
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (actives[i].active) {
            finish(&actives[i]);
        }
    }
}

static bool exit_layer_active(const struct behavior_word_layer_config *cfg) {
    for (size_t i = 0; i < cfg->num_exit_layers; i++) {
        if (zmk_keymap_layer_active(cfg->exit_layers[i])) {
            return true;
        }
    }
    return false;
}

static int on_word_layer_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_word_layer_config *cfg = dev->config;
    const zmk_keymap_layer_id_t layer = binding->param1;

    struct active_word_layer *w = find_active(layer);
    if (cfg->momentary) {
        // 押している間だけ（&mo と同じ）。Num Word が有効なら、終えずに引き継ぐ（レイヤーは有効のまま）
        if (w != NULL) {
            w->active = false;
        }
        zmk_keymap_layer_activate(layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    if (w != NULL) {
        finish(w);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    if (exit_layer_active(cfg)) {
        LOG_DBG("Word layer: exit layer active, not starting layer %d", layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!actives[i].active) {
            actives[i] = (struct active_word_layer){
                .active = true,
                .layer = layer,
                .config = dev->config,
            };
            zmk_keymap_layer_activate(layer);
            return ZMK_BEHAVIOR_OPAQUE;
        }
    }
    LOG_ERR("Word layer: too many active layers");
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_word_layer_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_word_layer_config *cfg = dev->config;
    if (cfg->momentary) {
        zmk_keymap_layer_deactivate(binding->param1);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_word_layer_driver_api = {
    .binding_pressed = on_word_layer_binding_pressed,
    .binding_released = on_word_layer_binding_released,
};

static bool is_alpha(uint16_t page, uint32_t id) {
    return page == HID_USAGE_KEY && id >= HID_USAGE_KEY_KEYBOARD_A && id <= HID_USAGE_KEY_KEYBOARD_Z;
}

static bool is_number(uint16_t page, uint32_t id) {
    if (page != HID_USAGE_KEY) {
        return false;
    }
    return (id >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
            id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ||
           (id >= HID_USAGE_KEY_KEYPAD_1_AND_END && id <= HID_USAGE_KEY_KEYPAD_0_AND_INSERT) ||
           id == HID_USAGE_KEY_KEYPAD_00 || id == HID_USAGE_KEY_KEYPAD_000;
}

static bool should_continue(const struct behavior_word_layer_config *cfg,
                            const struct zmk_keycode_state_changed *ev) {
    if (cfg->ignore_alphas && is_alpha(ev->usage_page, ev->keycode)) {
        return true;
    }
    if (cfg->ignore_numbers && is_number(ev->usage_page, ev->keycode)) {
        return true;
    }
    if (cfg->ignore_modifiers && is_mod(ev->usage_page, ev->keycode)) {
        return true;
    }
    const zmk_mod_flags_t mods = ev->implicit_modifiers | zmk_hid_get_explicit_mods();
    for (size_t i = 0; i < cfg->num_continue_keys; i++) {
        const struct word_layer_key *k = &cfg->continue_keys[i];
        if (k->page == ev->usage_page && k->id == ev->keycode &&
            (k->modifiers & mods) == k->modifiers) {
            return true;
        }
    }
    return false;
}

static int word_layer_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        if (!kc->state || alt_code_is_own_event(kc)) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        for (int i = 0; i < MAX_ACTIVE; i++) {
            if (actives[i].active && !should_continue(actives[i].config, kc)) {
                finish(&actives[i]);
            }
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_layer_state_changed *ly = as_zmk_layer_state_changed(eh);
    if (ly == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (!ly->state) {
        // ほかの操作でレイヤーが解除された: こちらも終える
        struct active_word_layer *w = find_active(ly->layer);
        if (w != NULL && !zmk_keymap_layer_active(ly->layer)) {
            w->active = false;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    // exit-layers のレイヤーが有効になった: 終える（有効になった時点のその出来事の中で解除するので、
    // 後に続く conditional layer の listener は、両方が有効な状態を見ない）
    if (!zmk_keymap_layer_active(ly->layer)) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!actives[i].active) {
            continue;
        }
        const struct behavior_word_layer_config *cfg = actives[i].config;
        for (size_t j = 0; j < cfg->num_exit_layers; j++) {
            if (cfg->exit_layers[j] == ly->layer) {
                finish(&actives[i]);
                break;
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_word_layer, word_layer_listener);
ZMK_SUBSCRIPTION(behavior_word_layer, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(behavior_word_layer, zmk_layer_state_changed);

#define WORD_LAYER_KEY(i, n)                                                                       \
    {                                                                                              \
        .modifiers = SELECT_MODS(DT_INST_PROP_BY_IDX(n, continue_list, i)),                        \
        .page = ZMK_HID_USAGE_PAGE(DT_INST_PROP_BY_IDX(n, continue_list, i)),                      \
        .id = ZMK_HID_USAGE_ID(DT_INST_PROP_BY_IDX(n, continue_list, i)),                          \
    }

#define WORD_LAYER_EXIT_OK(i, n)                                                                   \
    &&(DT_INST_PROP_BY_IDX(n, exit_layers, i) >= 0 &&                                              \
       DT_INST_PROP_BY_IDX(n, exit_layers, i) < ZMK_KEYMAP_LAYERS_LEN)

#define WORD_LAYER_INST(n)                                                                         \
    BUILD_ASSERT(1 LISTIFY(DT_INST_PROP_LEN_OR(n, exit_layers, 0), WORD_LAYER_EXIT_OK, (), n),     \
                 "word-layer: exit-layers must be layer numbers in the keymap");                   \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, continue_list),                                           \
                (static const struct word_layer_key word_layer_keys_##n[] = {LISTIFY(              \
                     DT_INST_PROP_LEN(n, continue_list), WORD_LAYER_KEY, (, ), n)};),              \
                ())                                                                                \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, exit_layers),                                             \
                (static const zmk_keymap_layer_id_t word_layer_exit_##n[] =                        \
                     DT_INST_PROP(n, exit_layers);),                                               \
                ())                                                                                \
    static const struct behavior_word_layer_config behavior_word_layer_config_##n = {              \
        .continue_keys = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, continue_list),                      \
                                     (word_layer_keys_##n), (NULL)),                               \
        .num_continue_keys = DT_INST_PROP_LEN_OR(n, continue_list, 0),                             \
        .ignore_alphas = DT_INST_PROP(n, ignore_alphas),                                           \
        .ignore_numbers = DT_INST_PROP(n, ignore_numbers),                                         \
        .ignore_modifiers = DT_INST_PROP(n, ignore_modifiers),                                     \
        .momentary = DT_INST_PROP(n, momentary),                                                   \
        .exit_layers = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, exit_layers), (word_layer_exit_##n),   \
                                   (NULL)),                                                        \
        .num_exit_layers = DT_INST_PROP_LEN_OR(n, exit_layers, 0),                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_word_layer_config_##n, POST_KERNEL,     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_word_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(WORD_LAYER_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
