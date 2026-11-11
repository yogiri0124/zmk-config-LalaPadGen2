/*
 * レイヤーの見分けの番号（lalapad,layer-ids）と、ZMK Studio で保存した変更の付け替え。
 *
 * ZMK はレイヤーを ID（keymap に書いた順番。0 から）で扱い、ZMK Studio で保存した変更も ID で覚える
 * （keymap/l/<ID>/<位置>：キー、keymap/l_n/<ID>：レイヤーの名前、keymap/layer_order：並べ替えた順番）。
 * そのため、キーマップでレイヤーを並べ替え・足し・消しして書き込むと、保存した変更が別のレイヤーに付いてしまう。
 *
 * ここでは、keymap と同じ順番に並べた「見分けの番号」（lalapad,layer-ids の ids。レイヤーを作ったときに決め、
 * 名前を変えても並べ替えても変えない）を、設定（lalapad/layer_ids）に覚えておく。起動したとき、ZMK が設定を
 * 読み込む前（main の settings_load の前。初期化の APPLICATION の最後。ZMK Studio のスレッドなどの静的なスレッドは、
 * 初期化が終わってから動き出すので、まだ動いていない）に、覚えていた並びと今の並びを比べ、違えば（レイヤーを
 * 並べ替え・足し・消しした）、保存した変更を番号で付け替える:
 *   - キー（l/<ID>/<位置>）は、同じ番号のレイヤーの新しい ID へ。消えたレイヤーのものは捨てる。
 *     キーの引数がレイヤー ID のもの（&mo・&lt・&tog・&to・&sl など、behavior の metadata で「レイヤー ID」と
 *     書かれている引数）は、引数も新しい ID に書き直す。消えたレイヤーを指すものは捨てる（キーマップの割り当てに戻る。
 *     そのままにすると、その番号を引き継いだ別のレイヤーを指してしまうため）
 *   - レイヤーの名前（l_n/<ID>）も同じく付け替える（消えたレイヤーのものは捨てる）
 *   - 順番（layer_order）は、Studio で並べ替えていなければ（書いた順のままなら）消す（新しいキーマップの順番になる）。
 *     並べ替え・Studio で外したレイヤーがあれば、その順番を新しい ID で書き直し、足したレイヤーだけを後ろに足す
 *     （Studio で外したレイヤーは外したまま）
 * ZMK が読み込むのは付け替えた後のものなので、別のレイヤーに付いた変更を一度も使わない。
 *
 * 書き換えの途中で電源が切れたり、書けなかったりしても、別のレイヤーに付いた変更が残らない順番にする:
 *   古いものを全部消す（1 つでも消せなければ、そこでやめる。並びは覚え直さないので、次に起動したときにまた付け替える。
 *   残っているのは古い並びのものだけなので、付け替え直せる）→ 新しい並びを覚える → 新しい ID で書く
 *   （並びを覚えた後に切れれば、一部の変更がなくなるだけ）。読めなかった変更は、消して書き直さない。
 * 付け替えきれないほど変更が多いときは、Studio の「Restore Stock Settings」と同じく、保存した変更を全部消す。
 *
 * 初めて起動したとき（覚えている並びがない）は、今の並びを覚えるだけ（それまでの変更は、今の並びのものとみなす）。
 * どの起動でも、今のレイヤーの数より大きい ID の変更は消す（ZMK v0.3.0 は、範囲外の ID の名前を配列の外に書いてしまうため）。
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
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
#define MAX_LAYERS 32

BUILD_ASSERT(DT_PROP_LEN(LAYER_IDS_NODE, ids) == ZMK_KEYMAP_LAYERS_LEN,
             "lalapad,layer-ids: ids must have one number per keymap layer");
BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= MAX_LAYERS, "layer ids: up to 32 layers");

#define CHECK_ID_RANGE(node, prop, idx)                                                            \
    BUILD_ASSERT(DT_PROP_BY_IDX(node, prop, idx) >= 1 && DT_PROP_BY_IDX(node, prop, idx) <= 255,   \
                 "lalapad,layer-ids: each id must be 1-255");
DT_FOREACH_PROP_ELEM(LAYER_IDS_NODE, ids, CHECK_ID_RANGE)

static const uint8_t compiled_ids[] = DT_PROP(LAYER_IDS_NODE, ids);

/* 番号が重なっていないか（重なっていれば使わない）。初めて聞かれたときに確かめる（初期化の中が最初） */
static int ids_state; /* 0: まだ、1: 使える、-1: 使えない */

static bool check_ids(void) {
    if (ids_state == 0) {
        bool ok = true;
        for (size_t i = 0; i < ARRAY_SIZE(compiled_ids) && ok; i++) {
            for (size_t j = 0; j < i && ok; j++) {
                if (compiled_ids[j] == compiled_ids[i]) {
                    ok = false;
                }
            }
        }
        if (!ok) {
            LOG_ERR("lalapad,layer-ids: ids must be unique; layer ids are not used");
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

#define NO_LAYER 0xFF
/* 全部消すときに、読み出し直して消す回数の上限（多すぎて一度に読み出せないため） */
#define RESET_PASSES 16
/* レイヤー ID を引数に取る behavior の数の上限 */
#define LAYER_BEHAVIORS_MAX 24

/* ZMK が保存するキーの形（app/src/keymap.c の zmk_behavior_binding_setting と同じ。後ろの 0 の引数は省いて保存する） */
struct binding_setting {
    zmk_behavior_local_id_t behavior_local_id;
    uint32_t param1;
    uint32_t param2;
} __packed;

struct saved_binding {
    uint8_t layer;
    uint8_t position;
    bool ok; /* 読めた */
    struct binding_setting value;
};

/* 前回起動したときの並び（設定から読んだもの） */
static uint8_t stored_ids[MAX_LAYERS];
static size_t stored_len;
static bool stored_found;

/* 読み出した ZMK Studio の変更（初期化の中だけで触る） */
static struct saved_binding saved[CONFIG_ZMK_LAYER_IDS_MIGRATE_MAX];
static size_t saved_count;
static bool saved_overflow;
static char saved_names[MAX_LAYERS][CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN];
static uint8_t saved_name_len[MAX_LAYERS];
static uint32_t saved_name_mask; /* l_n/<ID> がある */
static uint32_t saved_name_ok;   /* そのうち読めたもの */
/* 32 以上の番号の名前（ZMK は uint8_t に切り詰めて配列の外に書く。番号で消すために覚える） */
static unsigned long big_names[8];
static size_t big_names_len;
static uint8_t saved_order[MAX_LAYERS];
static size_t saved_order_len;
static bool saved_order_found;
static bool saved_order_ok;
/* 古い ID → 新しい ID（NO_LAYER は、なくなった） */
static uint8_t id_map[MAX_LAYERS];

/* レイヤー ID を引数に取る behavior（local ID と、どの引数か） */
struct layer_behavior {
    zmk_behavior_local_id_t local_id;
    bool param1;
    bool param2;
};
static struct layer_behavior layer_behaviors[LAYER_BEHAVIORS_MAX];
static size_t layer_behaviors_len;

static int read_stored_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                          void *param) {
    const char *next;
    if (settings_name_steq(key, "layer_ids", &next) && !next) {
        // 長さ 0 は消した印。長すぎるものは壊れている（覚えていないものとして扱う）
        if (len == 0 || len > sizeof(stored_ids)) {
            stored_found = false;
            return 0;
        }
        const int n = read_cb(cb_arg, stored_ids, len);
        stored_found = n > 0;
        stored_len = n > 0 ? n : 0;
    }
    return 0;
}

static int save_ids(void) {
    const int err = settings_save_one("lalapad/layer_ids", compiled_ids, sizeof(compiled_ids));
    if (err < 0) {
        LOG_ERR("Failed to save layer ids (err %d)", err);
    }
    return err;
}

/* keymap/ の下に ZMK Studio が保存したものを読み出す（読めなかったものも、消すために名前は覚える） */
static int collect_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param) {
    const char *next;
    char *end;
    if (settings_name_steq(key, "l", &next) && next) {
        const unsigned long layer = strtoul(next, &end, 10);
        if (end == next || *end != '/') {
            return 0;
        }
        const char *pos = end + 1;
        const unsigned long position = strtoul(pos, &end, 10);
        if (end == pos || *end != '\0' || layer > UINT8_MAX || position > UINT8_MAX || len == 0) {
            return 0;
        }
        if (saved_count == ARRAY_SIZE(saved)) {
            saved_overflow = true;
            return 0;
        }
        struct saved_binding *s = &saved[saved_count++];
        memset(s, 0, sizeof(*s));
        s->layer = layer;
        s->position = position;
        s->ok = len <= sizeof(struct binding_setting) && read_cb(cb_arg, &s->value, len) > 0;
    } else if (settings_name_steq(key, "l_n", &next) && next) {
        const unsigned long layer = strtoul(next, &end, 10);
        if (end == next || *end != '\0' || len == 0) {
            return 0;
        }
        if (layer >= MAX_LAYERS) {
            // 範囲外の番号（どの起動でも消す）。覚えきれなければ、読み出しきれなかったものとして扱う
            if (big_names_len < ARRAY_SIZE(big_names)) {
                big_names[big_names_len++] = layer;
            } else {
                saved_overflow = true;
            }
            return 0;
        }
        saved_name_mask |= BIT(layer);
        if (len < CONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN) {
            const int n = read_cb(cb_arg, saved_names[layer], len);
            if (n > 0) {
                saved_name_len[layer] = n;
                saved_name_ok |= BIT(layer);
            }
        }
    } else if (settings_name_steq(key, "layer_order", &next) && !next) {
        if (len == 0) {
            return 0;
        }
        saved_order_found = true;
        const int n = read_cb(cb_arg, saved_order, MIN(len, sizeof(saved_order)));
        saved_order_ok = n > 0;
        saved_order_len = n > 0 ? n : 0;
    }
    return 0;
}

static int collect(void) {
    saved_count = 0;
    saved_overflow = false;
    saved_name_mask = 0;
    saved_name_ok = 0;
    big_names_len = 0;
    saved_order_found = false;
    saved_order_ok = false;
    return settings_load_subtree_direct("keymap", collect_cb, NULL);
}

/* 消す（消した印を書く） */
static int delete_one(const char *key) {
    const int err = settings_delete(key);
    if (err < 0) {
        LOG_ERR("Failed to delete %s (err %d)", key, err);
    }
    return err;
}

/* 読み出したもののうち、keep(古い ID) が false のものを消す（keep が NULL なら順番も含めて全部）。
 * 1 つでも消せなければ負の値 */
static int delete_saved(bool (*keep)(uint32_t layer)) {
    char key[32];
    int ret = 0;
    for (size_t i = 0; i < saved_count; i++) {
        if (keep != NULL && keep(saved[i].layer)) {
            continue;
        }
        snprintf(key, sizeof(key), "keymap/l/%u/%u", saved[i].layer, saved[i].position);
        ret = MIN(ret, delete_one(key));
    }
    for (int l = 0; l < MAX_LAYERS; l++) {
        if ((saved_name_mask & BIT(l)) && (keep == NULL || !keep(l))) {
            snprintf(key, sizeof(key), "keymap/l_n/%d", l);
            ret = MIN(ret, delete_one(key));
        }
    }
    for (size_t i = 0; i < big_names_len; i++) {
        snprintf(key, sizeof(key), "keymap/l_n/%lu", big_names[i]);
        ret = MIN(ret, delete_one(key));
    }
    if (keep == NULL && saved_order_found) {
        ret = MIN(ret, delete_one("keymap/layer_order"));
    }
    return ret;
}

static bool in_range(uint32_t layer) { return layer < ZMK_KEYMAP_LAYERS_LEN; }

/* ZMK Studio の変更を全部消す（多すぎて付け替えきれないとき。読み出せた分ずつ、なくなるまで消し直す） */
static int delete_all(void) {
    for (int pass = 0; pass < RESET_PASSES; pass++) {
        const int err = collect();
        if (err < 0) {
            return err;
        }
        if (saved_count == 0 && saved_name_mask == 0 && big_names_len == 0 && !saved_order_found &&
            !saved_overflow) {
            return 0;
        }
        const int del = delete_saved(NULL);
        if (del < 0) {
            return del;
        }
    }
    return -EAGAIN;
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

/* behavior の名前から、レイヤー ID を引数に取るなら覚える */
static void note_behavior(zmk_behavior_local_id_t local_id, const char *name) {
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
    if (!p1 && !p2) {
        return;
    }
    if (layer_behaviors_len < ARRAY_SIZE(layer_behaviors)) {
        layer_behaviors[layer_behaviors_len++] = (struct layer_behavior){local_id, p1, p2};
    } else {
        LOG_WRN("Too many behaviors with layer parameters; keys of %s are dropped", name);
    }
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE)
/* behavior の local ID は、まだ ZMK が設定から読んでいないので、こちらで読む（behavior/local_id/<ID> = 名前） */
static int behavior_ids_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                           void *param) {
    const char *next;
    if (settings_name_steq(key, "local_id", &next) && next) {
        char *end;
        const unsigned long id = strtoul(next, &end, 10);
        char name[64];
        if (end == next || *end != '\0' || len == 0 || len >= sizeof(name)) {
            return 0;
        }
        const int n = read_cb(cb_arg, name, len);
        if (n > 0) {
            name[n] = '\0';
            note_behavior(id, name);
        }
    }
    return 0;
}
#endif

static void find_layer_behaviors(void) {
    layer_behaviors_len = 0;
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE)
    settings_load_subtree_direct("behavior", behavior_ids_cb, NULL);
#else
    // CRC16 の local ID は、ここより前の初期化（APPLICATION の既定の優先度）で決まっている
    for (size_t i = 0; i < saved_count; i++) {
        const zmk_behavior_local_id_t id = saved[i].value.behavior_local_id;
        bool seen = false;
        for (size_t k = 0; k < layer_behaviors_len; k++) {
            seen = seen || layer_behaviors[k].local_id == id;
        }
        const char *name = zmk_behavior_find_behavior_name_from_local_id(id);
        if (!seen && saved[i].ok && name != NULL) {
            note_behavior(id, name);
        }
    }
#endif
}
#else
static void find_layer_behaviors(void) { layer_behaviors_len = 0; }
#endif

/* キーの引数がレイヤー ID なら、新しい ID に書き直す。指すレイヤーが消えていれば false（キーは捨てる） */
static bool remap_params(struct binding_setting *value) {
    for (size_t k = 0; k < layer_behaviors_len; k++) {
        const struct layer_behavior *b = &layer_behaviors[k];
        if (b->local_id != value->behavior_local_id) {
            continue;
        }
        if (b->param1) {
            const uint8_t l = map_layer(value->param1);
            if (l == NO_LAYER) {
                return false;
            }
            value->param1 = l;
        }
        if (b->param2) {
            const uint8_t l = map_layer(value->param2);
            if (l == NO_LAYER) {
                return false;
            }
            value->param2 = l;
        }
        return true;
    }
    return true;
}

/* 新しい ID で書く（古いものを全部消し、新しい並びを覚えた後） */
static void write_new(void) {
    char key[32];
    for (size_t i = 0; i < saved_count; i++) {
        const uint8_t layer = map_layer(saved[i].layer);
        struct binding_setting value = saved[i].value;
        if (!saved[i].ok || layer == NO_LAYER || !remap_params(&value)) {
            continue;
        }
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
    for (int l = 0; l < MAX_LAYERS; l++) {
        const uint8_t layer = map_layer(l);
        if (!(saved_name_ok & BIT(l)) || layer == NO_LAYER) {
            continue;
        }
        snprintf(key, sizeof(key), "keymap/l_n/%u", layer);
        settings_save_one(key, saved_names[l], saved_name_len[l]);
    }
    if (!saved_order_ok) {
        return;
    }
    // 書いた順のまま（0, 1, 2, … と前回のレイヤーが全部並び、後ろは空き）なら、Studio で並べ替えていない。
    // 消したままにする（新しいキーマップの順番になる）
    bool stock = saved_order_len >= stored_len;
    for (size_t i = 0; i < saved_order_len && stock; i++) {
        stock = saved_order[i] == (i < stored_len ? i : ZMK_KEYMAP_LAYER_ID_INVAL);
    }
    if (stock) {
        return;
    }
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
    // 足したレイヤー（前の並びになかった番号）だけを後ろに足す（Studio で外したレイヤーは外したまま）
    for (size_t j = 0; j < sizeof(compiled_ids) && n < ARRAY_SIZE(order); j++) {
        bool existed = false;
        for (size_t i = 0; i < stored_len; i++) {
            existed = existed || stored_ids[i] == compiled_ids[j];
        }
        if (!existed && !(used & BIT(j))) {
            order[n++] = j;
            used |= BIT(j);
        }
    }
    while (n < ARRAY_SIZE(order)) {
        order[n++] = ZMK_KEYMAP_LAYER_ID_INVAL;
    }
    settings_save_one("keymap/layer_order", order, sizeof(order));
}

static int layer_ids_init(void) {
    const bool valid = check_ids();
    if (settings_subsys_init() < 0) {
        LOG_ERR("Layer ids: settings are not available");
        return 0;
    }
    stored_found = false;
    settings_load_subtree_direct("lalapad", read_stored_cb, NULL);

    if (collect() < 0) {
        LOG_ERR("Layer ids: cannot read ZMK Studio changes");
        return 0;
    }

    const bool changed = valid && stored_found &&
                         (stored_len != sizeof(compiled_ids) ||
                          memcmp(stored_ids, compiled_ids, stored_len) != 0);
    if (!changed) {
        // 並びは同じ（か、初めて・番号が使えない）。今のレイヤーの数以上の ID の変更だけを消す
        // （名前は、ZMK が配列の外に書いてしまうため。名前は読み出しきれなくても番号は全部わかる。キーは ZMK が範囲外として
        //  読まないので、読み出しきれなかった分は残っても害はない）
        delete_saved(in_range);
        if (valid && !stored_found) {
            save_ids();
        }
        return 0;
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

    if (saved_overflow) {
        // 付け替えきれない: 保存した変更を全部消す（別のレイヤーに付いたまま残さない）。消しきれなければ、並びは覚え直さない
        LOG_WRN("Too many ZMK Studio changes to move; resetting them");
        if (delete_all() == 0) {
            save_ids();
        }
        return 0;
    }

    find_layer_behaviors();
    // 古いものを全部消す。1 つでも消せなければやめる（並びを覚え直さないので、次に起動したときに付け替え直す）
    if (delete_saved(NULL) < 0) {
        LOG_ERR("Layer ids: cannot delete old ZMK Studio changes; will retry on next boot");
        return 0;
    }
    if (save_ids() < 0) {
        return 0;
    }
    write_new();
    return 0;
}

/* ZMK の設定の読み込み（main の settings_load）より前。APPLICATION の初期化の最後に動かす（behavior の local ID の
 * 初期化（CRC16 のとき）より後） */
SYS_INIT(layer_ids_init, APPLICATION, 99);

#endif // IS_ENABLED(CONFIG_ZMK_LAYER_IDS_MIGRATE)
