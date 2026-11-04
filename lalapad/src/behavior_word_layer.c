/*
 * 打っている間だけ続くレイヤー（zmk,behavior-word-layer、引数 1 つ: レイヤー番号。Num Word に使う）。
 *
 * 押すとレイヤーを有効にし、continue-list のキー（と ignore-* で指定した種類のキー）を押している間は続ける。
 * それ以外のキー（キーコード）の「押す」が届いたら、レイヤーを解除する。有効な間にもう一度押すと解除する。
 * urob/zmk-auto-layer（MIT。ZMK Contributors）の auto-layer と同じ動きに、次を足したもの:
 * - ほかの操作（&mo のキーを離した、&to など）でそのレイヤーが解除されたら、こちらも終わる
 *   （終わったことを知らないまま、次に押したときに「解除」として扱わないように）。
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

static int on_word_layer_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const zmk_keymap_layer_id_t layer = binding->param1;

    struct active_word_layer *w = find_active(layer);
    if (w != NULL) {
        finish(w);
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
        if (!kc->state) {
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
    if (ly != NULL && !ly->state) {
        // ほかの操作でレイヤーが解除された: こちらも終える
        struct active_word_layer *w = find_active(ly->layer);
        if (w != NULL && !zmk_keymap_layer_active(ly->layer)) {
            w->active = false;
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

#define WORD_LAYER_INST(n)                                                                         \
    static const struct word_layer_key word_layer_keys_##n[] = {                                  \
        LISTIFY(DT_INST_PROP_LEN(n, continue_list), WORD_LAYER_KEY, (, ), n)};                     \
    static const struct behavior_word_layer_config behavior_word_layer_config_##n = {              \
        .continue_keys = word_layer_keys_##n,                                                      \
        .num_continue_keys = ARRAY_SIZE(word_layer_keys_##n),                                      \
        .ignore_alphas = DT_INST_PROP(n, ignore_alphas),                                           \
        .ignore_numbers = DT_INST_PROP(n, ignore_numbers),                                         \
        .ignore_modifiers = DT_INST_PROP(n, ignore_modifiers),                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_word_layer_config_##n, POST_KERNEL,     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_word_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(WORD_LAYER_INST)

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
