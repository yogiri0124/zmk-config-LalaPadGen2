/*
 * Windows の Alt コードで文字を送る behavior（behavior_alt_code.c）を、ほかのモジュールから見る。
 */

#pragma once

#include <stdbool.h>

#include <zephyr/sys/util_macro.h>
#include <zmk/events/keycode_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_ALT_CODE)
/* Alt コードの送信が自分で送っているキー入力（IME オフ／オン・Alt・テンキーの数字など）の出来事か
 * （その出来事の listener の中で呼ぶ。ふつうは自分のキー入力を ZMK 本体の hold-tap・sticky key より後から流すので、
 * 自作モジュールの listener には届かない。どちらもないビルドで届いたときに見分けるためのもの） */
bool alt_code_is_own_event(const struct zmk_keycode_state_changed *ev);
/* Alt コードを送っている最中か（システムワークキューから呼ぶ） */
bool alt_code_is_sending(void);
#else
static inline bool alt_code_is_own_event(const struct zmk_keycode_state_changed *ev) {
    return false;
}
static inline bool alt_code_is_sending(void) { return false; }
#endif
