/*
 * レイヤーの見分けの番号（lalapad,layer-ids）と、ZMK Studio で保存した変更の付け替え。
 *
 * ZMK はレイヤーを ID（keymap に書いた順番。0 から）で扱い、ZMK Studio で保存した変更も ID で覚える
 * （keymap/l/<ID>/<位置>：キー、keymap/l_n/<ID>：レイヤーの名前、keymap/layer_order：並べ替えた順番）。
 * そのため、キーマップでレイヤーを並べ替え・足し・消しして書き込むと、保存した変更が別のレイヤーに付いてしまう。
 *
 * ここでは、keymap と同じ順番に並べた「見分けの番号」（lalapad,layer-ids の ids。レイヤーを作ったときに決め、
 * 名前を変えても並べ替えても変えない）を、前回起動したときのものと一緒に設定（lalapad/layer_ids）に覚えておく。
 * 起動して設定を読み込んだ後、覚えていた並びと今の並びが違えば（レイヤーを並べ替え・足し・消しした）、
 * 保存した変更を番号で付け替える:
 *   - キー（l/<ID>/<位置>）は、同じ番号のレイヤーの新しい ID へ。消えたレイヤーのものは捨てる。
 *     キーの引数がレイヤー ID のもの（&mo・&lt・&tog・&to・&sl など、behavior の metadata で「レイヤー ID」と
 *     書かれている引数）は、引数も新しい ID に書き直す（消えたレイヤーを指すものはそのまま）
 *   - レイヤーの名前（l_n/<ID>）も同じく付け替える
 *   - 順番（layer_order）は、Studio で並べ替えていなければ（書いた順のままなら）消す（新しいキーマップの順番になる）。
 *     並べ替えていれば、その順番を新しい ID で書き直し、足したレイヤーは後ろに足す
 * 付け替えたら、ZMK に設定を読み直させる（zmk_keymap_discard_changes）。
 *
 * 書き換えの途中で電源が切れても、別のレイヤーに付いた変更が残らない順番にする:
 *   古いものを全部消す → 新しい並びを覚える → 新しい ID で書く
 * （消した後に切れれば、変更がなくなるだけ。並びを覚えた後に切れれば、一部の変更がなくなるだけ）。
 * 付け替えきれないほど変更が多いときは、Studio の「Restore Stock Settings」と同じく、保存した変更を消す。
 *
 * 初めて起動したとき（覚えている並びがない）は、今の並びを覚えるだけ（それまでの変更は、今の並びのものとみなす）。
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include "layer_ids.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LAYER_IDS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(lalapad_layer_ids)

BUILD_ASSERT(DT_PROP_LEN(LAYER_IDS_NODE, ids) == ZMK_KEYMAP_LAYERS_LEN,
             "lalapad,layer-ids: ids must have one number per keymap layer");
BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= 32, "layer ids: up to 32 layers");

static const uint8_t compiled_ids[] = DT_PROP(LAYER_IDS_NODE, ids);

/* 番号が使える（1〜255 で、重なっていない）か。初めて聞かれたときに確かめる */
static int ids_state; /* 0: まだ、1: 使える、-1: 使えない */

static bool check_ids(void) {
    if (ids_state == 0) {
        bool ok = true;
        for (size_t i = 0; i < ARRAY_SIZE(compiled_ids) && ok; i++) {
            if (compiled_ids[i] == 0) {
                ok = false;
            }
            for (size_t j = 0; j < i && ok; j++) {
                if (compiled_ids[j] == compiled_ids[i]) {
                    ok = false;
                }
            }
        }
        if (!ok) {
            LOG_ERR("lalapad,layer-ids: ids must be 1-255 and unique; layer ids are not used");
        }
        ids_state = ok ? 1 : -1;
    }
    return ids_state > 0;
}

size_t lalapad_layer_ids(const uint8_t **ids) {
    if (!check_ids()) {
        *ids = NULL;
        return 0;
    }
    *ids = compiled_ids;
    return ARRAY_SIZE(compiled_ids);
}

#if IS_ENABLED(CONFIG_ZMK_LAYER_IDS_MIGRATE)

#define SETTINGS_ROOT "lalapad"
#define SETTINGS_KEY "layer_ids"
#define SETTINGS_FULL_KEY SETTINGS_ROOT "/" SETTINGS_KEY
#define NO_LAYER 0xFF

/* ZMK が保存するキーの形（app/src/keymap.c の zmk_behavior_binding_setting と同じ。後ろの 0 の引数は省いて保存する） */
struct binding_setting {
    zmk_behavior_local_id_t behavior_local_id;
    uint32_t param1;
    uint32_t param2;
} __packed;

struct saved_binding {
    uint8_t layer;
    uint8_t position;
    struct binding_setting value;
};

/* 前回起動したときの並び（設定から読んだもの） */
static uint8_t stored_ids[32];
static size_t stored_len;
static bool stored_found;
static bool checked;

/* 付け替えるために読み出したもの（専用のワークでだけ触る） */
static struct saved_binding saved[CONFIG_ZMK_LAYER_IDS_MIGRATE_MAX];
static size_t saved_count;
static bool saved_overflow;
static char saved_names[32][CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN];
static uint8_t saved_name_len[32];
static uint32_t saved_name_mask;
static uint8_t saved_order[32];
static size_t saved_order_len;
static bool saved_order_found;
/* 古い ID → 新しい ID（NO_LAYER は、なくなった） */
static uint8_t id_map[32];

static int ids_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, SETTINGS_KEY, &next) && !next) {
        if (len == 0 || len > sizeof(stored_ids)) {
            // 消した印（長さ 0）か、壊れている。覚えていないものとして扱う
            stored_found = false;
            return 0;
        }
        const int err = read_cb(cb_arg, stored_ids, len);
        if (err <= 0) {
            LOG_ERR("Failed to read layer ids from settings (err %d)", err);
            stored_found = false;
            return 0;
        }
        stored_len = err;
        stored_found = true;
    }
    return 0;
}

static void migrate_work_handler(struct k_work *work);
static K_WORK_DEFINE(migrate_work, migrate_work_handler);

static int ids_handle_commit(void) {
    // 設定を読み込み終えた（ZMK の keymap の設定も読み込み済み）。付け替えは、読み込みの外（システムワークキュー）で行う
    // （ここから設定を書き換えたり、keymap に読み直させたりしない）。起動して最初の 1 回だけ
    if (!checked) {
        checked = true;
        k_work_submit(&migrate_work);
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(lalapad_layer_ids, SETTINGS_ROOT, NULL, ids_handle_set, ids_handle_commit,
                               NULL);

static int save_ids(void) {
    const int err = settings_save_one(SETTINGS_FULL_KEY, compiled_ids, sizeof(compiled_ids));
    if (err < 0) {
        LOG_ERR("Failed to save layer ids (err %d)", err);
    }
    return err;
}

/* keymap/ の下に ZMK Studio が保存したものを読み出す */
static int collect_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param) {
    const char *next;
    if (settings_name_steq(key, "l", &next) && next) {
        char *end;
        const unsigned long layer = strtoul(next, &end, 10);
        if (*end != '/') {
            return 0;
        }
        const unsigned long position = strtoul(end + 1, &end, 10);
        if (*end != '\0' || layer >= 32 || position > UINT8_MAX || len == 0 ||
            len > sizeof(struct binding_setting)) {
            return 0;
        }
        if (saved_count == ARRAY_SIZE(saved)) {
            saved_overflow = true;
            return 0;
        }
        struct saved_binding *s = &saved[saved_count];
        memset(s, 0, sizeof(*s));
        if (read_cb(cb_arg, &s->value, len) <= 0) {
            return 0;
        }
        s->layer = layer;
        s->position = position;
        saved_count++;
    } else if (settings_name_steq(key, "l_n", &next) && next) {
        char *end;
        const unsigned long layer = strtoul(next, &end, 10);
        if (*end != '\0' || layer >= 32 || len == 0 || len >= CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN) {
            return 0;
        }
        const int n = read_cb(cb_arg, saved_names[layer], len);
        if (n > 0) {
            saved_name_len[layer] = n;
            saved_name_mask |= BIT(layer);
        }
    } else if (settings_name_steq(key, "layer_order", &next) && !next) {
        if (len == 0) {
            return 0;
        }
        const int n = read_cb(cb_arg, saved_order, MIN(len, sizeof(saved_order)));
        if (n > 0) {
            saved_order_len = n;
            saved_order_found = true;
        }
    }
    return 0;
}

static uint8_t map_layer(uint32_t old_id) { return old_id < stored_len ? id_map[old_id] : NO_LAYER; }

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
/* その引数の取りうる値に「レイヤー ID」があるか */
static bool values_have_layer(const struct behavior_parameter_value_metadata *values, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (values[i].type == BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID) {
            return true;
        }
    }
    return false;
}
#endif

/* キーの引数がレイヤー ID なら、新しい ID に書き直す */
static void remap_params(struct binding_setting *value) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    const char *name = zmk_behavior_find_behavior_name_from_local_id(value->behavior_local_id);
    if (name == NULL) {
        return;
    }
    const struct device *dev = zmk_behavior_get_binding(name);
    struct behavior_parameter_metadata meta = {0};
    if (dev == NULL || behavior_get_parameter_metadata(dev, &meta) < 0) {
        return;
    }
    bool p1 = false;
    bool p2 = false;
    for (size_t s = 0; s < meta.sets_len; s++) {
        p1 = p1 || values_have_layer(meta.sets[s].param1_values, meta.sets[s].param1_values_len);
        p2 = p2 || values_have_layer(meta.sets[s].param2_values, meta.sets[s].param2_values_len);
    }
    if (p1 && map_layer(value->param1) != NO_LAYER) {
        value->param1 = map_layer(value->param1);
    }
    if (p2 && map_layer(value->param2) != NO_LAYER) {
        value->param2 = map_layer(value->param2);
    }
#else
    ARG_UNUSED(value);
#endif
}

static void delete_old(void) {
    char key[32];
    for (size_t i = 0; i < saved_count; i++) {
        snprintf(key, sizeof(key), "keymap/l/%u/%u", saved[i].layer, saved[i].position);
        settings_delete(key);
    }
    for (int l = 0; l < 32; l++) {
        if (saved_name_mask & BIT(l)) {
            snprintf(key, sizeof(key), "keymap/l_n/%d", l);
            settings_delete(key);
        }
    }
    if (saved_order_found) {
        settings_delete("keymap/layer_order");
    }
}

static void write_new(void) {
    char key[32];
    for (size_t i = 0; i < saved_count; i++) {
        const uint8_t layer = map_layer(saved[i].layer);
        if (layer == NO_LAYER) {
            continue;
        }
        struct binding_setting value = saved[i].value;
        remap_params(&value);
        // ZMK と同じく、後ろの 0 の引数は省いて保存する
        size_t len = sizeof(value);
        if (value.param2 == 0) {
            len -= 4;
            if (value.param1 == 0) {
                len -= 4;
            }
        }
        snprintf(key, sizeof(key), "keymap/l/%u/%u", layer, saved[i].position);
        const int err = settings_save_one(key, &value, len);
        if (err < 0) {
            LOG_ERR("Failed to move a saved key to layer %u (err %d)", layer, err);
        }
    }
    for (int l = 0; l < 32; l++) {
        const uint8_t layer = map_layer(l);
        if (!(saved_name_mask & BIT(l)) || layer == NO_LAYER) {
            continue;
        }
        snprintf(key, sizeof(key), "keymap/l_n/%u", layer);
        settings_save_one(key, saved_names[l], saved_name_len[l]);
    }
    if (saved_order_found) {
        // 書いた順のまま（0, 1, 2, …。後ろは空き）なら、並べ替えていない。消したままにする（新しい順番になる）
        bool stock = true;
        for (size_t i = 0; i < saved_order_len; i++) {
            if (saved_order[i] != (i < stored_len ? i : ZMK_KEYMAP_LAYER_ID_INVAL)) {
                stock = false;
            }
        }
        if (!stock) {
            uint8_t order[ZMK_KEYMAP_LAYERS_LEN];
            uint32_t used = 0;
            size_t n = 0;
            for (size_t i = 0; i < saved_order_len && n < ARRAY_SIZE(order); i++) {
                const uint8_t layer = map_layer(saved_order[i]);
                if (layer != NO_LAYER && !(used & BIT(layer))) {
                    order[n++] = layer;
                    used |= BIT(layer);
                }
            }
            // 足したレイヤーは後ろに足す
            for (uint8_t layer = 0; layer < ZMK_KEYMAP_LAYERS_LEN && n < ARRAY_SIZE(order); layer++) {
                if (!(used & BIT(layer))) {
                    order[n++] = layer;
                }
            }
            settings_save_one("keymap/layer_order", order, n);
        }
    }
}

static void migrate_work_handler(struct k_work *work) {
    if (!check_ids()) {
        return;
    }
    if (!stored_found) {
        save_ids();
        return;
    }
    if (stored_len == sizeof(compiled_ids) && memcmp(stored_ids, compiled_ids, stored_len) == 0) {
        return;
    }
    LOG_INF("Keymap layers changed; moving ZMK Studio changes to follow the layers");
    for (size_t i = 0; i < stored_len; i++) {
        id_map[i] = NO_LAYER;
        for (size_t j = 0; j < sizeof(compiled_ids); j++) {
            if (stored_ids[i] == compiled_ids[j]) {
                id_map[i] = j;
            }
        }
    }

    saved_count = 0;
    saved_overflow = false;
    saved_name_mask = 0;
    saved_order_found = false;
    const int err = settings_load_subtree_direct("keymap", collect_cb, NULL);
    if (err < 0 || saved_overflow) {
        // 付け替えきれない: 保存した変更を消す（別のレイヤーに付いたまま残さない）
        LOG_WRN("Cannot move ZMK Studio changes (err %d, overflow %d); resetting them", err,
                saved_overflow);
        zmk_keymap_reset_settings();
        delete_old();
        save_ids();
        zmk_keymap_discard_changes();
        return;
    }

    delete_old();
    if (save_ids() < 0) {
        // 並びを覚えられなかった: 新しい ID で書くと、次に起動したときにまた付け替えてしまうので、書かない
        zmk_keymap_discard_changes();
        return;
    }
    write_new();
    zmk_keymap_discard_changes();
}

#endif // IS_ENABLED(CONFIG_ZMK_LAYER_IDS_MIGRATE)
