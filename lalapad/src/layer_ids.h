/*
 * レイヤーの見分けの番号（レイヤーのノードのラベル layer_id_<番号>。lalapad/src/layer_ids.c）。
 * キーマップのレイヤー ID（宣言順の番号）ごとに、名前を変えても並べ替えても変わらない番号を持つ。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* レイヤー ID ごとの見分けの番号の数（レイヤーの数）。どのレイヤーにも番号がなければ 0。
 * *ids には、レイヤー ID の順の番号を返す（1〜255。ラベルのないレイヤーは 0） */
size_t lalapad_layer_ids(const uint8_t **ids);
