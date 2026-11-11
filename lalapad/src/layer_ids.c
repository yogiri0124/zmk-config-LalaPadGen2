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
 *   引数がレイヤー ID かどうかを確かめられなかったキー（behavior が見つからない・metadata がない）も、書き直さない。
 * 付け替えきれないほど変更が多いときは、Studio の「Restore Stock Settings」と同じく、保存した変更を全部消す。
 *
 * 付け替えられなかった起動（設定が使えない・消せなかった・覚えていた並びが読めない・新しい並びを覚えられなかった）では、
 * その起動の間、ZMK に Studio の変更（キー・名前・順番）を読ませず（キーマップの割り当てのまま動く）、ZMK Studio を
 * ロックする（今の ID で保存されると、次の起動で古い並びのものとして付け替えてしまうため。Studio のキーマップの操作は
 * すべてロックの対象。CONFIG_ZMK_STUDIO_LOCKING=n でもロックは効き、キーマップに &studio_unlock がなければ、
 * 次の起動まで解けない）。ZMK の keymap の設定の受け口（"keymap"）より
 * 細かい受け口（"keymap/l"・"keymap/l_n"・"keymap/layer_order"）をここで持ち、通してよいものだけを ZMK の受け口に
 * 渡す（Zephyr の設定は、名前がいちばん長く合う受け口に渡す）。どの起動でも、今のレイヤーの数以上の ID の変更は
 * 通さない（ZMK v0.3.0 は、範囲外の ID の名前を配列の外に書いてしまうため）。
 *
 * 初めて起動したとき（覚えている並びがない）は、今の並びを覚えるだけ（それまでの変更は、今の並びのものとみなす）。
 * 覚えている並びの長さが壊れているときは、元に戻せないので、Studio の変更を全部消してから今の並びを覚える。
 * 覚えている並びは 2 か所（lalapad/layer_ids と lalapad/layer_ids_b）に書く（片方が壊れて消えても、初めての起動と
 * 取り違えないように。先に書く layer_ids を正とし、読めないときだけ layer_ids_b を使う）。
 *
 * 設定を読み出す settings_load_subtree_direct は、Zephyr v3.5.0 では失敗を返さない（いつも 0）。NVS の読み出しでは、
 * 名前か値が読めない項目は Zephyr が消すので（settings_nvs.c の settings_nvs_load）、読み出しに出てこない項目が、
 * 後の起動で出てくることはない（出てこなかった古い変更が、後で別のレイヤーに付くことはない）。
 */

#include <errno.h>
#include <limits.h>
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
#if IS_ENABLED(CONFIG_ZMK_STUDIO)
#include <zmk/studio/core.h>
#endif

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
/* 付け替えるキーで使っている behavior の種類の数の上限（超えた分は確かめられないので、そのキーは書き直さない） */
#define BEHAVIOR_KINDS_MAX 48

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

/* 前回起動したときの並び（設定から読んだもの。使うほう） */
enum stored_state { STORED_NONE, STORED_OK, STORED_UNREADABLE, STORED_BROKEN };
static uint8_t stored_ids[MAX_LAYERS];
static size_t stored_len;
static enum stored_state stored_state;

/* 2 か所から読んだもの（0: layer_ids、1: layer_ids_b） */
struct stored_copy {
    enum stored_state state;
    uint8_t ids[MAX_LAYERS];
    size_t len;
};
static struct stored_copy copies[2];

/* この起動の間、ZMK に Studio の変更を読ませない（付け替えられなかった） */
static bool block_all;

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

/* 付け替えるキーで使っている behavior（local ID）と、引数がレイヤー ID か。
 * 確かめられたものだけ known（引数がレイヤー ID でないものも含む）。確かめられなければ、そのキーは書き直さない */
struct behavior_kind {
    zmk_behavior_local_id_t local_id;
    bool known;
    bool param1;
    bool param2;
};
static struct behavior_kind behavior_kinds[BEHAVIOR_KINDS_MAX];
static size_t behavior_kinds_len;

static int read_stored_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                          void *param) {
    const char *next;
    struct stored_copy *c = NULL;
    if (settings_name_steq(key, "layer_ids", &next) && !next) {
        c = &copies[0];
    } else if (settings_name_steq(key, "layer_ids_b", &next) && !next) {
        c = &copies[1];
    }
    if (c == NULL) {
        return 0;
    }
    if (len == 0) {
        c->state = STORED_NONE; // 消した印
        return 0;
    }
    if (len > sizeof(c->ids)) {
        c->state = STORED_BROKEN;
        return 0;
    }
    const int n = read_cb(cb_arg, c->ids, len);
    if (n <= 0) {
        c->state = STORED_UNREADABLE;
        return 0;
    }
    c->len = n;
    c->state = STORED_OK;
    return 0;
}

/* 2 か所のどちらを使うか（layer_ids が読めればそれ。なければ layer_ids_b） */
static void pick_stored(void) {
    const struct stored_copy *c = copies[0].state == STORED_OK ? &copies[0] : copies[1].state == STORED_OK ? &copies[1] : NULL;
    if (c != NULL) {
        memcpy(stored_ids, c->ids, c->len);
        stored_len = c->len;
        stored_state = STORED_OK;
    } else if (copies[0].state == STORED_UNREADABLE || copies[1].state == STORED_UNREADABLE) {
        stored_state = STORED_UNREADABLE;
    } else if (copies[0].state == STORED_BROKEN || copies[1].state == STORED_BROKEN) {
        stored_state = STORED_BROKEN;
    } else {
        stored_state = STORED_NONE;
    }
}

static bool copy_is_current(const struct stored_copy *c) {
    return c->state == STORED_OK && c->len == sizeof(compiled_ids) && memcmp(c->ids, compiled_ids, c->len) == 0;
}

/* 今の並びを覚える（先に layer_ids、次に layer_ids_b。layer_ids を書けなければ失敗） */
static int save_ids(void) {
    const int err = settings_save_one("lalapad/layer_ids", compiled_ids, sizeof(compiled_ids));
    if (err < 0) {
        LOG_ERR("Failed to save layer ids (err %d)", err);
        return err;
    }
    const int err_b = settings_save_one("lalapad/layer_ids_b", compiled_ids, sizeof(compiled_ids));
    if (err_b < 0) {
        LOG_WRN("Failed to save the backup of layer ids (err %d)", err_b);
    }
    return 0;
}

/* この起動では、ZMK に Studio の変更を読ませず、ZMK Studio をロックする（付け替えられなかった） */
static void block_this_boot(const char *why) {
    LOG_ERR("Layer ids: %s; ZMK Studio changes are not used and ZMK Studio is locked until the next boot", why);
    block_all = true;
#if IS_ENABLED(CONFIG_ZMK_STUDIO)
    zmk_studio_core_lock();
#endif
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

/* 読み出す（Zephyr v3.5.0 の settings_load_subtree_direct は失敗を返さないので、戻り値は見ない。先頭の説明） */
static void collect(void) {
    saved_count = 0;
    saved_overflow = false;
    saved_name_mask = 0;
    saved_name_ok = 0;
    big_names_len = 0;
    saved_order_found = false;
    saved_order_ok = false;
    settings_load_subtree_direct("keymap", collect_cb, NULL);
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
        collect();
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

/* その behavior の分け方（なければ足す。足せなければ NULL） */
static struct behavior_kind *kind_of(zmk_behavior_local_id_t local_id, bool add) {
    for (size_t k = 0; k < behavior_kinds_len; k++) {
        if (behavior_kinds[k].local_id == local_id) {
            return &behavior_kinds[k];
        }
    }
    if (!add || behavior_kinds_len == ARRAY_SIZE(behavior_kinds)) {
        return NULL;
    }
    behavior_kinds[behavior_kinds_len] = (struct behavior_kind){.local_id = local_id};
    return &behavior_kinds[behavior_kinds_len++];
}

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

/* behavior の名前から、引数がレイヤー ID かを確かめる（behavior が見つからない・metadata がなければ、確かめられない） */
static void classify(struct behavior_kind *kind, const char *name) {
    const struct device *dev = zmk_behavior_get_binding(name);
    struct behavior_parameter_metadata meta = {0};
    if (dev == NULL || behavior_get_parameter_metadata(dev, &meta) < 0) {
        LOG_WRN("Cannot check the parameters of %s; its saved keys are dropped", name);
        return;
    }
    for (size_t s = 0; s < meta.sets_len; s++) {
        kind->param1 = kind->param1 || values_have_layer(meta.sets[s].param1_values, meta.sets[s].param1_values_len);
        kind->param2 = kind->param2 || values_have_layer(meta.sets[s].param2_values, meta.sets[s].param2_values_len);
    }
    kind->known = true;
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
        if (end == next || *end != '\0' || id > UINT16_MAX || len == 0 || len >= sizeof(name)) {
            return 0;
        }
        struct behavior_kind *kind = kind_of(id, false);
        if (kind == NULL || kind->known) {
            return 0; // 付け替えるキーで使っていない
        }
        const int n = read_cb(cb_arg, name, len);
        if (n > 0) {
            name[n] = '\0';
            classify(kind, name);
        }
    }
    return 0;
}
#endif

/* 付け替えるキーで使っている behavior を、それぞれ確かめる */
static void classify_behaviors(void) {
    behavior_kinds_len = 0;
    for (size_t i = 0; i < saved_count; i++) {
        if (saved[i].ok && kind_of(saved[i].value.behavior_local_id, true) == NULL) {
            LOG_WRN("Too many kinds of saved keys; some are dropped");
        }
    }
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE)
    settings_load_subtree_direct("behavior", behavior_ids_cb, NULL);
#else
    // CRC16 の local ID は、ここより前の初期化（APPLICATION の既定の優先度）で決まっている
    for (size_t k = 0; k < behavior_kinds_len; k++) {
        const char *name = zmk_behavior_find_behavior_name_from_local_id(behavior_kinds[k].local_id);
        if (name != NULL) {
            classify(&behavior_kinds[k], name);
        }
    }
#endif
}
#else
/* metadata がなければ、引数がレイヤー ID か確かめられない（どのキーも書き直さない） */
static void classify_behaviors(void) { behavior_kinds_len = 0; }
#endif

/* キーの引数がレイヤー ID なら、新しい ID に書き直す。書き直せない（引数を確かめられない・指すレイヤーが消えた）
 * なら false（キーは捨てる。キーマップの割り当てに戻る） */
static bool remap_params(struct binding_setting *value) {
    const struct behavior_kind *b = kind_of(value->behavior_local_id, false);
    if (b == NULL || !b->known) {
        return false;
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
        // main がもう一度初期化して読み込めると、付け替えないまま読んでしまうので、この起動では読ませない
        block_this_boot("settings are not available");
        return 0;
    }
    memset(copies, 0, sizeof(copies));
    settings_load_subtree_direct("lalapad", read_stored_cb, NULL);
    pick_stored();
    if (!valid) {
        return 0; // 番号が使えない（重なっている）: 付け替えない（範囲外の ID は受け口で止める）
    }
    if (stored_state == STORED_UNREADABLE) {
        block_this_boot("cannot read the saved layer order");
        return 0;
    }

    collect();
    const bool changed = stored_state == STORED_BROKEN ||
                         (stored_state == STORED_OK &&
                          (stored_len != sizeof(compiled_ids) || memcmp(stored_ids, compiled_ids, stored_len) != 0));
    if (!changed) {
        // 並びは同じ（か、初めて）。今のレイヤーの数以上の ID の変更を消す（読み込みは受け口でも止める）
        delete_saved(in_range);
        // 初めて・2 か所の片方が古い・ない: 今の並びを書き直す（書けなくても害はない。並びは変わっていないので、
        // 次の起動でも、保存したものは今の並びのものとして扱う）
        if (!copy_is_current(&copies[0]) || !copy_is_current(&copies[1])) {
            save_ids();
        }
        return 0;
    }

    if (saved_overflow || stored_state == STORED_BROKEN) {
        // 付け替えきれない・覚えていた並びが壊れている: 保存した変更を全部消す（別のレイヤーに付いたまま残さない）。
        // 消しきれなければ、並びは覚え直さず、この起動では使わない
        LOG_WRN("Cannot move ZMK Studio changes; resetting them");
        if (delete_all() < 0) {
            block_this_boot("cannot delete ZMK Studio changes");
        } else if (save_ids() < 0) {
            block_this_boot("cannot save the layer order");
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
    classify_behaviors();
    // 古いものを全部消す。1 つでも消せなければやめる（並びを覚え直さないので、次に起動したときに付け替え直す。
    // この起動では、残った古いものを ZMK に読ませず、Studio で今の ID のものを保存させない）
    if (delete_saved(NULL) < 0) {
        block_this_boot("cannot delete old ZMK Studio changes (will retry on next boot)");
        return 0;
    }
    if (save_ids() < 0) {
        // 古いものは消えている（Studio の変更がないだけ）。並びが古いままなので、Studio で今の ID のものを保存させない
        block_this_boot("cannot save the layer order");
        return 0;
    }
    write_new();
    return 0;
}

/* ZMK の設定の読み込み（main の settings_load）より前。APPLICATION の初期化の最後に動かす（behavior の local ID の
 * 初期化（CRC16 のとき）より後） */
SYS_INIT(layer_ids_init, APPLICATION, 99);

/* ---- ZMK の keymap の設定の受け口の手前の受け口 ---- */

static const struct settings_handler_static *keymap_handler(void) {
    static const struct settings_handler_static *found;
    if (found == NULL) {
        STRUCT_SECTION_FOREACH(settings_handler_static, ch) {
            if (strcmp(ch->name, "keymap") == 0) {
                found = ch;
                break;
            }
        }
    }
    return found;
}

/* ZMK の受け口に渡す（key は "keymap/" の後ろ） */
static int pass_to_keymap(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const struct settings_handler_static *h = keymap_handler();
    return h != NULL && h->h_set != NULL ? h->h_set(key, len, read_cb, cb_arg) : 0;
}

/* name の先頭のレイヤー ID が、今のレイヤーの数より小さいか */
static bool layer_in_range(const char *name) {
    char *end;
    const unsigned long layer = name != NULL ? strtoul(name, &end, 10) : ULONG_MAX;
    return name != NULL && end != name && (*end == '/' || *end == '\0') && layer < ZMK_KEYMAP_LAYERS_LEN;
}

static int guard_with_layer(const char *head, const char *name, size_t len, settings_read_cb read_cb,
                            void *cb_arg) {
    if (block_all || !layer_in_range(name)) {
        return 0; // 読ませない
    }
    char key[32];
    snprintf(key, sizeof(key), "%s/%s", head, name);
    return pass_to_keymap(key, len, read_cb, cb_arg);
}

static int guard_bindings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    return guard_with_layer("l", name, len, read_cb, cb_arg);
}

static int guard_names_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    return guard_with_layer("l_n", name, len, read_cb, cb_arg);
}

static int guard_order_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (block_all || name != NULL) {
        return 0;
    }
    return pass_to_keymap("layer_order", len, read_cb, cb_arg);
}

SETTINGS_STATIC_HANDLER_DEFINE(lalapad_keymap_bindings, "keymap/l", NULL, guard_bindings_set, NULL, NULL);
SETTINGS_STATIC_HANDLER_DEFINE(lalapad_keymap_names, "keymap/l_n", NULL, guard_names_set, NULL, NULL);
SETTINGS_STATIC_HANDLER_DEFINE(lalapad_keymap_order, "keymap/layer_order", NULL, guard_order_set, NULL, NULL);

#endif // IS_ENABLED(CONFIG_ZMK_LAYER_IDS_MIGRATE)
