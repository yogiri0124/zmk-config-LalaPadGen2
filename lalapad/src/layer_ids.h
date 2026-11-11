/*
 * レイヤーの見分けの番号（lalapad,layer-ids。lalapad/src/layer_ids.c）。
 * キーマップのレイヤー ID（宣言順の番号）ごとに、名前を変えても並べ替えても変わらない番号を持つ。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* レイヤー ID ごとの見分けの番号の数（レイヤーの数）。番号が使えない（ノードがない・重なっている・0 がある）ときは 0。
 * *ids には、レイヤー ID の順の番号を返す */
size_t lalapad_layer_ids(const uint8_t **ids);
