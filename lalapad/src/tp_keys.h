/*
 * トラックパッドをキー操作に変えるモード（behavior_tp_keys.c・behavior_tp_stroke.c が定義し、
 * tp_keys.c の入力処理が使う）。
 *
 * 出すものは、モードの定義の直下（既定。どのレイヤーでも使う）と、子ノード（表。layers に書いたレイヤーで使う）に、
 * 左右のトラックパッドごとに書ける。モードのキーを押したときに有効なレイヤーを上から見て、表のあるレイヤーの表を使う
 * （表のないレイヤーは素通り。表の中の &trans は、その下のレイヤーの表、最後に既定を見る）。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

/* bindings の並び（zmk,behavior-tp-keys）: 1 本指を上・下・左・右へ動かしたとき、2 本指を上・下・左・右へ
 * 動かしたとき（どちらも指を動かした向き。2 本指はスクロールの向きではない） */
#define TP_KEYS_BINDINGS_LEN 8
#define TP_KEYS_CURSOR_FIRST 0
#define TP_KEYS_SCROLL_FIRST 4
/* 1 本指・2 本指それぞれの数（上・下・左・右の順） */
#define TP_KEYS_DIRS 4
#define TP_KEYS_UP 0
#define TP_KEYS_DOWN 1
#define TP_KEYS_LEFT 2
#define TP_KEYS_RIGHT 3

/* 左右のトラックパッド（左は親機につないだもの、右は分割の通信で届くもの） */
#define TP_KEYS_SIDE_LEFT 0
#define TP_KEYS_SIDE_RIGHT 1
#define TP_KEYS_SIDES 2

enum tp_keys_kind {
    /* zmk,behavior-tp-keys: 動かした量に応じて、その向きの binding を送る */
    TP_KEYS_KIND_MOTION,
    /* zmk,behavior-tp-stroke: 描いた向きの並びで binding を送る */
    TP_KEYS_KIND_STROKE,
};

/* 表（モードの定義の直下の既定、または子ノード 1 つ） */
struct tp_keys_table {
    /* 使うレイヤーの ID（num_layers が 0 なら既定。モードの定義の直下） */
    const uint8_t *layers;
    size_t num_layers;
    /* 左右のトラックパッドで出すもの（それぞれ struct tp_keys_mode の num_bindings 個） */
    const struct zmk_behavior_binding *sides[TP_KEYS_SIDES];
};

struct tp_keys_mode {
    enum tp_keys_kind kind;
    const struct tp_keys_table *tables;
    size_t num_tables;
    /* 表の 1 つの側の数。MOTION: TP_KEYS_BINDINGS_LEN。STROKE: strokes と同じ数 */
    size_t num_bindings;
    /* MOTION: 1 回送るのに必要な動きの量（トラックパッドの生の値）。1 本指・2 本指 */
    uint16_t cursor_step;
    uint16_t scroll_step;
    /* STROKE: 向きの並び（dt-bindings/lalapad/tp_keys.h の TP_STROKEn）と、1 つの向きとみなす動きの量 */
    const uint16_t *strokes;
    uint16_t stroke_step;
    /* 押してから離すまで、離してから次を押すまでの時間 (ms) */
    uint16_t tap_ms;
};

/* zmk,behavior-tp-keys・zmk,behavior-tp-stroke の押した／離した（behavior の config が struct tp_keys_mode）。
 * 最後に押したキーのモードが有効になる */
int tp_keys_binding_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event);
int tp_keys_binding_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event);

/* ---- 表を devicetree から作るマクロ（behavior_tp_keys.c・behavior_tp_stroke.c で使う） ---- */

/* node の prop（phandle-array）の idx 番目を binding にする（ZMK の ZMK_KEYMAP_EXTRACT_BINDING の、プロパティ名を
 * 選べるもの） */
#define TP_KEYS_BINDING(idx, node, prop)                                                           \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_PHANDLE_BY_IDX(node, prop, idx)),                        \
        .param1 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, param1), (0),                \
                              (DT_PHA_BY_IDX(node, prop, idx, param1))),                           \
        .param2 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, param2), (0),                \
                              (DT_PHA_BY_IDX(node, prop, idx, param2))),                           \
    }

#define TP_KEYS_ARRAY_NAME(node, kind) _CONCAT(_CONCAT(tp_keys_, kind), DT_DEP_ORD(node))

/* 表の子ノードの prop があれば、その binding の配列を作る */
#define TP_KEYS_TABLE_ARRAY(node, prop, kind)                                                      \
    IF_ENABLED(DT_NODE_HAS_PROP(node, prop),                                                       \
               (static const struct zmk_behavior_binding TP_KEYS_ARRAY_NAME(node, kind)[] = {      \
                    LISTIFY(DT_PROP_LEN(node, prop), TP_KEYS_BINDING, (, ), node, prop)};))

/* 左（右）に使う配列: left-bindings（right-bindings）があればそれ、なければ bindings */
#define TP_KEYS_SIDE_ARRAY(node, side_prop, side_kind)                                             \
    COND_CODE_1(DT_NODE_HAS_PROP(node, side_prop), (TP_KEYS_ARRAY_NAME(node, side_kind)),          \
                (TP_KEYS_ARRAY_NAME(node, both)))

#define TP_KEYS_SIDE_LEN(node, side_prop)                                                          \
    COND_CODE_1(DT_NODE_HAS_PROP(node, side_prop), (DT_PROP_LEN(node, side_prop)),                 \
                (DT_PROP_LEN_OR(node, bindings, 0)))

#define TP_KEYS_CHECK_LAYER(node, prop, idx)                                                       \
    BUILD_ASSERT(DT_PROP_BY_IDX(node, prop, idx) < ZMK_KEYMAP_LAYERS_LEN,                          \
                 "tp-keys/tp-stroke: a table's layers is out of range");

/* 表の子ノード 1 つ分の配列と確認（num は 1 つの側の数） */
#define TP_KEYS_TABLE_DEFINE(node, num)                                                            \
    BUILD_ASSERT((DT_NODE_HAS_PROP(node, left_bindings) || DT_NODE_HAS_PROP(node, bindings)) &&    \
                     (DT_NODE_HAS_PROP(node, right_bindings) || DT_NODE_HAS_PROP(node, bindings)), \
                 "tp-keys/tp-stroke: a table needs bindings (or both left-bindings and "           \
                 "right-bindings)");                                                               \
    BUILD_ASSERT(TP_KEYS_SIDE_LEN(node, left_bindings) == (num) &&                                 \
                     TP_KEYS_SIDE_LEN(node, right_bindings) == (num),                              \
                 "tp-keys/tp-stroke: a table has the wrong number of bindings");                   \
    IF_ENABLED(DT_NODE_HAS_PROP(node, layers),                                                     \
               (DT_FOREACH_PROP_ELEM(node, layers, TP_KEYS_CHECK_LAYER)                            \
                    static const uint8_t TP_KEYS_ARRAY_NAME(node, layers)[] =                      \
                        DT_PROP(node, layers);))                                                   \
    TP_KEYS_TABLE_ARRAY(node, bindings, both)                                                      \
    TP_KEYS_TABLE_ARRAY(node, left_bindings, left)                                                 \
    TP_KEYS_TABLE_ARRAY(node, right_bindings, right)

/* モードの定義の直下に bindings・left-bindings・right-bindings のどれかがあるか（あれば既定の表にする） */
#define TP_KEYS_HAS_DEFAULT(node)                                                                      UTIL_OR(UTIL_OR(DT_NODE_HAS_PROP(node, bindings), DT_NODE_HAS_PROP(node, left_bindings)),                  DT_NODE_HAS_PROP(node, right_bindings))

/* モードの定義（inst の node）の表をすべて作る（直下の既定と子ノード）。num は 1 つの側の数。
 * 表の配列 name を作る。既定があれば先頭に置く */
#define TP_KEYS_TABLES_DEFINE(node, name, num)                                                     \
    IF_ENABLED(TP_KEYS_HAS_DEFAULT(node), (TP_KEYS_TABLE_DEFINE(node, num)))                       \
    DT_FOREACH_CHILD_VARGS(node, TP_KEYS_TABLE_DEFINE, num)                                        \
    static const struct tp_keys_table name[] = {                                                   \
        IF_ENABLED(TP_KEYS_HAS_DEFAULT(node), (TP_KEYS_TABLE_ENTRY(node)))                         \
            DT_FOREACH_CHILD(node, TP_KEYS_TABLE_ENTRY)};                                          \
    BUILD_ASSERT(ARRAY_SIZE(name) > 0, "tp-keys/tp-stroke: needs bindings");

/* struct tp_keys_table の初期化子（後ろに , を付ける） */
#define TP_KEYS_TABLE_ENTRY(node)                                                                  \
    {                                                                                              \
        .layers = COND_CODE_1(DT_NODE_HAS_PROP(node, layers), (TP_KEYS_ARRAY_NAME(node, layers)),  \
                              (NULL)),                                                             \
        .num_layers = DT_PROP_LEN_OR(node, layers, 0),                                             \
        .sides =                                                                                   \
            {                                                                                      \
                [TP_KEYS_SIDE_LEFT] = TP_KEYS_SIDE_ARRAY(node, left_bindings, left),               \
                [TP_KEYS_SIDE_RIGHT] = TP_KEYS_SIDE_ARRAY(node, right_bindings, right),            \
            },                                                                                     \
    },
