/*
 * かな入力モジュールの共通部分（送信キューと、かな配列モードの状態）。
 * behavior_kana.c（かな）・behavior_kana_seq.c（記号などの打鍵の列）・behavior_kana_mode.c（オン／オフ）・
 * behavior_kana_sync.c（キューの合図）から使う。
 * behavior_alt_code.c も、送信キューの目印（kana_output_set_marker_handler・kana_output_add_marker）、
 * 修飾キーを隠す仕組み（kana_output_mask_mods）、解除処理中かどうか（kana_mode_exiting）を使う
 * （かな入力のモジュールがあるビルドのとき）。
 * KANA_CHECK_MS と KANA_MASKED_MODS は、かな入力のモジュールがないビルドでも behavior_alt_code.c から使う
 * （宣言とマクロだけのため）。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/modifiers.h>

/* 時間の設定（DT のプロパティ prop）が 0〜max ms かをビルド時に確かめる（各 behavior の DT_DRV_COMPAT で使う）。
 * 負の値は送るときに大きな待ち時間になってしまうため */
#define KANA_CHECK_MS(n, prop, max)                                                                \
    BUILD_ASSERT(DT_INST_PROP(n, prop) >= 0 && DT_INST_PROP(n, prop) <= (max),                     \
                 #prop " is out of range (0-" #max " ms)");

/* かな・記号の打鍵の列や Alt コードを送る間に隠す修飾キー（左右の Shift と Ctrl）。
 * Alt・Win は隠さない: 押したままの Alt・Win を隠すと、PC には Alt・Win だけを押して離したように見え、
 * メニューバーやスタートメニューが開いて、続く打鍵がそちらに入ってしまうため */
#define KANA_MASKED_MODS (MOD_LSFT | MOD_RSFT | MOD_LCTL | MOD_RCTL)

/* keycode を1回タップする操作（押す→離す）を behavior queue に並べる。
 * 戻り値: 0 = 登録できた
 *         -EBUSY  = 前のキーの「離す」を再試行中なので何も登録していない
 *         -ENOSPC = キューが満杯で何も登録していない
 *         -EAGAIN = 「押す」だけ登録できた。「離す」は登録できるまで自動で再試行する
 *                   （呼び出し側は、同じ文字の残りのキーを送らずに打ち切ること） */
int kana_output_tap(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                    uint32_t tap_ms, uint32_t wait_ms);

/* keycode の「押す」だけをキューに並べる（押しっぱなしにするキー用）。
 * 戻り値: 0 = 登録できた / -EBUSY = 「離す」を再試行中なので登録していない / -ENOSPC = キューが満杯 */
int kana_output_press(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                      uint32_t tap_ms);

/* keycode の「離す」をキューに並べる。満杯なら登録できるまで順番どおりに自動で再試行する（必ず離される） */
void kana_output_release(const struct zmk_behavior_binding_event *event, uint32_t keycode,
                         uint32_t wait_ms);

/* 必要なら IME オン (LANGUAGE_1) のタップをキューに並べる（かな・記号を送る前に呼ぶ）。
 * resync_ms > 0: 次のときだけ送る（続けて打っている間は送らない）。
 *                - 最後にこの関数を呼んでから（またはかな配列モードに入って IME オンを並べてから）
 *                  resync_ms 以上たっているとき
 *                - まだ一度も送っていないとき（起動後）と、接続先が切り替わった後・トラックパッドの
 *                  クリック・ジェスチャー（キー位置 52〜67）を押した後の最初の1回
 * resync_ms = 0: 毎回送る。
 * 戻り値: 0 = 送らなくてよい、または登録できた / -EBUSY・-ENOSPC = 登録できなかった（次に呼んだときに改めて送る）
 *         -EAGAIN = 「押す」だけ登録できた（「離す」は自動で再試行する）。
 * 負の値のとき、呼び出し側は続く文字を送らずに打ち切ること */
int kana_output_ime_on(const struct zmk_behavior_binding_event *event, uint32_t tap_ms,
                       uint32_t wait_ms, uint32_t resync_ms);

/* かな・記号の打鍵の列 keys を送る: 修飾キーを隠す → （ime_on なら）kana_output_ime_on(resync_ms) →
 * keys を順にタップ → 修飾キーを戻す、をキューに並べる。
 * 押している Shift・Ctrl や、&sk の Shift などが、ローマ字や ← にかからないようにする
 * （隠すのは KANA_MASKED_MODS（Shift・Ctrl）だけ。Alt・Win はそのままかかる。
 * Caps Lock は PC 側の状態なので隠せない）。修飾キーを戻す合図は、満杯でも再試行して必ず並べる。
 * 制約: 隠すのは PC へ送るレポート全体なので、隠している間（列を送り終えるまでの数〜数十 ms）に
 * ほかのキー（&kp など、キューを通らないもの）を Shift・Ctrl といっしょに押すと、そのキーにも
 * Shift・Ctrl がかからない。
 * 戻り値: 0 = すべて登録できた / 負の値 = 途中で打ち切った（kana_output_tap と同じ意味。残りは送っていない） */
int kana_output_send_keys(const struct zmk_behavior_binding_event *event, const uint32_t *keys,
                          size_t keys_len, uint32_t tap_ms, uint32_t wait_ms, bool ime_on,
                          uint32_t resync_ms);

/* かな配列モード中で、かつ解除処理中でなければ true（&kana_key の入力を受け付ける） */
bool kana_input_enabled(void);

/* かな配列モードの解除処理中（受付停止からレイヤー解除まで）なら true。この間の入力は捨てる */
bool kana_mode_exiting(void);

/* かな配列モードをオンにする: IME オン (LANGUAGE_1) をキューに並べ、かなレイヤーを即座に有効化する
 * （IME オンを並べられなかったときは、オンにしない）。
 * layer は keymap の宣言順の番号（&mo などと同じくレイヤー ID として扱う） */
void kana_mode_on(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                  uint32_t tap_ms, uint32_t wait_ms);

/* かな配列モードをオフにする: 即座に &kana_key の受付を止め、IME オフ (LANGUAGE_2) と完了の合図を
 * キューに並べ、合図が処理された（それより前の送信がすべて実行された）時点でかなレイヤーを解除する。
 * sub_layers（親指シフト面など）も同じ時点で解除する（親指を押したまま抜けてもシフト面を残さない）。
 * かな配列モードでないときは、layer と sub_layers の解除だけをすぐに行う（Studio の &tog などで
 * かなのレイヤーだけが有効になったときに抜けられるように）。layer は kana_mode_on と同じレイヤー ID */
void kana_mode_off(const struct zmk_behavior_binding_event *event, zmk_keymap_layer_id_t layer,
                   const zmk_keymap_layer_id_t *sub_layers, size_t sub_layers_len, uint32_t tap_ms,
                   uint32_t wait_ms);

/* 目印が処理されたときに呼ばれる関数（目印は並べた順に処理される） */
typedef void (*kana_output_marker_cb_t)(void);

/* 目印が処理されたときに呼ぶ関数を登録する（1つだけ。Alt コードが初期化のときに登録する） */
void kana_output_set_marker_handler(kana_output_marker_cb_t cb);

/* 送信キューの末尾に目印を並べる。それより前に並べた送信（かな・記号・マクロなど）がすべて実行されたとき、
 * 登録した関数が呼ばれる。キューの処理が待ち時間で止まっていなければ（それより前の送信がすべて済んで
 * いれば）、この関数の中ですぐに呼ばれる。
 * 戻り値: 0 = 並べた / -EBUSY = 「離す」を再試行中なので並べていない / -ENOSPC = キューが満杯で並べていない */
int kana_output_add_marker(const struct zmk_behavior_binding_event *event);

/* 修飾キーを隠す（PC へ送るレポートから外す）仕組みの使い手 */
enum kana_mask_owner {
    KANA_MASK_KANA,     /* かな・記号の打鍵の列（kana_output_send_keys） */
    KANA_MASK_ALT_CODE, /* Alt コードの送信（behavior_alt_code.c） */
    KANA_MASK_OWNER_COUNT,
};

/* owner が隠したい修飾キー（MOD_LSFT などの組み合わせ。0 = 隠すのをやめる）を設定する。
 * 実際に隠すのは、隠している使い手すべてが隠したいもの（共通部分）。PC へ送る修飾キーが変わったら
 * キーボードのレポートをすぐ送る（隠すのをやめたとき、押したままの修飾キーを PC に戻すため） */
void kana_output_mask_mods(enum kana_mask_owner owner, uint8_t mods);

/* 完了の合図 (&kana_sync) がキューで処理されたときに behavior_kana_sync.c から呼ばれる。
 * token は kana_output.c が並べた合図の番号（1 から。0 や未知の番号は何もしない） */
void kana_output_on_sync(uint32_t token);
