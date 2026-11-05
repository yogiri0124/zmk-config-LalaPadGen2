# ZMK Behavior 一覧（ZMK v0.3.0）

キーマップの `bindings` に書く `&xxx` の早見表。**このリポジトリが固定している ZMK v0.3.0** の公式ドキュメント
（[Behaviors Overview](https://github.com/zmkfirmware/zmk/blob/v0.3.0/docs/docs/keymaps/behaviors/index.mdx)、
[Behavior 設定](https://github.com/zmkfirmware/zmk/blob/v0.3.0/docs/docs/config/behaviors.md)、
[Combo 設定](https://github.com/zmkfirmware/zmk/blob/v0.3.0/docs/docs/config/combos.md)）を確認して日本語でまとめたもの（2026-09-26）。
最新版の説明は [zmk.dev/docs/keymaps/behaviors](https://zmk.dev/docs/keymaps/behaviors)。最新版にあって v0.3.0 にない機能は使えない。

キーコード名は [zmk-keycodes.md](zmk-keycodes.md)、トラックパッド設定は [lalapadgen2-config.md](lalapadgen2-config.md) を参照。

## 基本ルール

- 1つのキー位置 = `&behavior` 1個 + その引数。引数の数（binding-cells）は behavior ごとに決まっている。
- 下の表の「引数」は**キーマップに書く値の数**。`BT_NXT` や `BL_ON` などの定数は中身が「コマンド 0」の2値に展開されるマクロなので、1つ書くだけで2セル分になる（`&bt` と `&bl` は2セル）。
- レイヤー番号は `keymap` ノード内の**定義順**で 0 から数える。このリポジトリでは `DEFAULT_LAYER`=0、`CONTROL_LAYER`=1、`SECONDARY_LAYER`=2、`TERTIARY_LAYER`=3、`SYSTEM_LAYER`=4、`MOUSE_LAYER`=5、`KANA_LAYER`=6、`KANA_L_LAYER`=7、`KANA_R_LAYER`=8、`KANA_SYM_LAYER`=9、`LEFT_HAND_LAYER`=10、`LEFT_HAND2_LAYER`=11、`LEFT_HAND3_LAYER`=12、`LEFT_HAND4_LAYER`=13 を `#define` している（エディタが数字で書いた `&mo 11` なども同じレイヤーを指す）。
- 引数に使う定数は include が必要：`bt.h`（`BT_*`）、`outputs.h`（`OUT_*`）、`pointing.h`（`LCLK`・`MOVE_*`・`SCRL_*`）、`keys.h`（キーコード）。このリポジトリの keymap は全部 include 済み。

## 組み込み behavior

### キー入力

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&kp キー` | 1 | キーを押す。`&kp LC(C)` のように修飾キー関数も使える |
| `&mt 修飾 キー` | 2 | 押し続けで修飾キー、タップでキー。既定は `hold-preferred`、200ms |
| `&kt キー` | 1 | キーの押下状態をトグルする |
| `&sk キー` | 1 | 次のキーと組み合わせて1回だけ効く（ワンショット）。既定 1000ms で解除 |
| `&gresc` | 0 | Shift か GUI が押されていれば `` ` ``、それ以外は Esc |
| `&caps_word` | 0 | 単語の間だけ Caps。`UNDERSCORE BACKSPACE DELETE` と英数字以外で解除 |
| `&key_repeat` | 0 | 直前に送ったキーをもう一度送る |

### その他

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&trans` | 0 | 透過。下のレイヤー（有効なもの）のキーを使う |
| `&none` | 0 | 何もしない。下のレイヤーにも渡さない |

### レイヤー

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&mo n` | 1 | 押している間だけレイヤー n を有効 |
| `&lt n キー` | 2 | 押し続けでレイヤー n、タップでキー。既定は `tap-preferred`、200ms |
| `&to n` | 1 | レイヤー n だけを有効にする（デフォルトレイヤーは残る） |
| `&tog n` | 1 | レイヤー n の有効/無効を切り替える |
| `&sl n` | 1 | 次のキーを押すまでレイヤー n（ワンショット） |

### マウス操作（`CONFIG_ZMK_POINTING=y` が必要。このリポジトリは有効）

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&mkp ボタン` | 1 | `LCLK`(`MB1`) `RCLK`(`MB2`) `MCLK`(`MB3`) `MB4` `MB5` |
| `&mmv 方向` | 1 | カーソル移動：`MOVE_UP` `MOVE_DOWN` `MOVE_LEFT` `MOVE_RIGHT` |
| `&msc 方向` | 1 | スクロール：`SCRL_UP` `SCRL_DOWN` `SCRL_LEFT` `SCRL_RIGHT` |

### 接続・出力

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&bt BT_SEL n` | 2 | BLE プロファイル n（0 始まり）を選ぶ |
| `&bt BT_NXT` / `&bt BT_PRV` | 1 | 次 / 前のプロファイル |
| `&bt BT_CLR` | 1 | **選択中プロファイルのペアリング情報を消す** |
| `&bt BT_CLR_ALL` | 1 | **全プロファイルのペアリング情報を消す** |
| `&bt BT_DISC n` | 2 | プロファイル n を切断（接続中かつ非選択のとき） |
| `&out OUT_USB` / `OUT_BLE` / `OUT_TOG` | 1 | 出力先を USB / BLE / 切替。選択はフラッシュに保存される |

- このキーボードの `BT_MAX_CONN` は 5（左＝central の `Kconfig.defconfig`）。うち1つは左右の分割接続に使う。`BT_SEL` の番号を追加・変更するときは、使えるプロファイル数を確認事項として報告する。

### リセット・電源・その他

| 書き方 | 引数 | 動作 |
| --- | --- | --- |
| `&sys_reset` | 0 | 再起動 |
| `&bootloader` | 0 | ブートローダーに入る（書き込み待ち） |
| `&soft_off` | 0 | 電源オフ。追加の有効化設定が必要（公式の Soft Off ページ参照） |
| `&ext_power EP_ON` / `EP_OFF` / `EP_TOG` | 1 | 外部電源出力の制御。有効化設定が必要 |
| `&studio_unlock` | 0 | ZMK Studio のロック解除（このリポジトリは `CONFIG_ZMK_STUDIO_LOCKING=n`） |
| `&bl BL_ON` / `BL_OFF` / `BL_TOG` / `BL_INC` / `BL_DEC` / `BL_CYCLE` | 1 | バックライト。**このキーボードは未使用** |
| `&bl BL_SET 値` | 2 | バックライトの明るさを指定（値が必須）。**未使用** |
| `&rgb_ug RGB_TOG` など | 1 | RGB アンダーグロー。**このキーボードは未使用**（LED は rgbled_widget） |

誤操作が困るもの（`BT_CLR`、`BT_CLR_ALL`、`&bootloader`、`&sys_reset`、`&soft_off`）を割り当てる・移動するときは、報告に明記する。

## 自分で定義する behavior（`/ { behaviors { ... }; };` に書く）

### Hold-Tap（`compatible = "zmk,behavior-hold-tap"`、`#binding-cells = <2>`）

| プロパティ | 既定 | 意味 |
| --- | --- | --- |
| `bindings` | 必須 | `<&押し続け用>, <&タップ用>`（引数なしで書く） |
| `flavor` | `"hold-preferred"` | `hold-preferred`：時間切れか他キー押下で hold／`balanced`：時間切れか他キーの押して離すで hold／`tap-preferred`：時間切れだけで hold／`tap-unless-interrupted`：時間内に他キーが押されたときだけ hold |
| `tapping-term-ms` | 既定なし（必ず書く。&mt/&lt は 200） | hold と判定するまでの時間 |
| `quick-tap-ms` | 無効 | この時間内に2回目を押すと、押し続けてもタップ扱い |
| `require-prior-idle-ms` | 無効 | 直前にこの時間内で他キーを押していたら即タップ |
| `retro-tap` | false | 他キーを押さずに離したら、時間切れでもタップを出す |
| `hold-while-undecided` / `-linger` | false | 判定前から hold を出しておく |
| `hold-trigger-key-positions` | なし | ここにない位置のキーが押されたらタップ扱い（ホームロウ mod 向け） |
| `hold-trigger-on-release` | false | 上の判定をキーを離すまで遅らせる |

### Tap Dance（`"zmk,behavior-tap-dance"`、`#binding-cells = <0>`）

- `bindings = <&kp A>, <&kp B>, ...;` 1回目・2回目…のタップで使うものを並べる。`tapping-term-ms` 既定 200。
- 中に hold-tap を入れると、その回数で押し続けたときに hold 側が動く。

### Mod-Morph（`"zmk,behavior-mod-morph"`、`#binding-cells = <0>`）

- `bindings = <&通常時>, <&修飾キー押下時>;`、`mods = <(MOD_LSFT|MOD_RSFT)>;`。
- `keep-mods` を指定すると、その修飾キーを外さずに送る。

### Macro

| compatible | `#binding-cells` | 用途 |
| --- | --- | --- |
| `"zmk,behavior-macro"` | `<0>` | 固定の操作列 |
| `"zmk,behavior-macro-one-param"` | `<1>` | 引数1つを中に渡す |
| `"zmk,behavior-macro-two-param"` | `<2>` | 引数2つを中に渡す |

- 制御：`&macro_tap`（既定）、`&macro_press`、`&macro_release`、`&macro_pause_for_release`、`&macro_wait_time ms`、`&macro_tap_time ms`、`&macro_param_1to1` など。
- `wait-ms` 既定 15、`tap-ms` 既定 30。`&macro_press` で押したものは必ず `&macro_release` で離す。
- 例：`bindings = <&macro_tap &kp Z &kp M &kp K>;`
- エディタ管理の `&mc0`〜 は、頼まれない限り書き換えない（AGENTS.md）。

### Sticky Key / Sticky Layer の調整

`&sk { release-after-ms = <2000>; };` のように既存ノードの値を変えられる。既定値は2つで異なる（v0.3.0 の `app/dts/behaviors/sticky_key.dtsi` で確認）。

| プロパティ | `&sk` の既定 | `&sl` の既定 |
| --- | --- | --- |
| `release-after-ms` | 1000 | 1000 |
| `quick-release` | false（次のキーを**離したとき**に解除） | **true**（次のキーを**押したとき**に解除） |
| `lazy` | false | false |
| `ignore-modifiers` | true（修飾キーを押しても解除されない） | **false**（修飾キーを押すと解除される） |

### Caps Word の調整

- `&caps_word { continue-list = <UNDERSCORE MINUS>; };`、`mods`（既定 `MOD_LSFT`）。

### Key Toggle / Toggle Layer の調整

- `toggle-mode` を `on` / `off` / `flip`（既定）で指定した別インスタンスを作れる（例：オンにしかしない `&tog_on`）。

## Combos（`/ { combos { compatible = "zmk,combos"; ... }; };`）

| プロパティ | 既定 | 意味 |
| --- | --- | --- |
| `bindings` | 必須 | 発動する behavior（例：`<&kp ESCAPE>`） |
| `key-positions` | 必須 | 同時押しするキー位置（0〜67） |
| `timeout-ms` | 50 | この時間内に全キーを押すと発動 |
| `require-prior-idle-ms` | 無効 | 直前に他キーを押していたら対象外 |
| `slow-release` | false | 全キーを離したときに解除（既定はどれか1つ離したら解除） |
| `layers` | 全レイヤー | 有効にするレイヤー番号の一覧 |

- 同時に有効になれるコンボは 4 個まで（`CONFIG_ZMK_COMBO_MAX_PRESSED_COMBOS`）。

## Conditional Layers（`compatible = "zmk,conditional-layers"`）

- `if-layers = <2 3>; then-layer = <4>;` 2 と 3 が両方有効なとき 4 を有効にする（このリポジトリの現状の設定。keymap では名前で書いている）。

## このリポジトリ独自の behavior（消さない・依頼なく変えない）

| 名前 | 中身 | 書き方 |
| --- | --- | --- |
| `&mt2` | hold-tap（`tap-preferred`、350ms、quick-tap 200、`<&kp>, <&kp>`）。以前あった require-prior-idle 125 は外した（直前の打鍵から 125ms 以内に押すと、長押ししても Shift にならず rrrr と連続入力になったため。戻すときは keymap の mt2 に `require-prior-idle-ms = <125>;` を足す） | `&mt2 押し続けキー タップキー` |
| `&zip_dyn_scale` | トラックパッドの倍率を増減・リセット（ドライバ v1.0.0） | `&zip_dyn_scale 対象 操作` |
| `&zip_dyn_scale_set` | トラックパッドの倍率を直接指定 | `&zip_dyn_scale_set 対象 値` |

- `zip_dyn_scale` 系の定数（`zip_dynamic_scale.h`）：対象 `ZDS_XY`（カーソル）`ZDS_SC`（スクロール）`ZDS_ALL`（両方）、操作 `ZDS_INC` `ZDS_DEC` `ZDS_RST`。
  `zip_dyn_scale_set` の値は 10 倍表記（`10`＝1.0 倍、`5`＝0.5 倍、`15`＝1.5 倍）。
- トラックパッドのクリック・ジェスチャーは、ドライバの `trackpad-to-pos` によってキー位置 52〜67 の押下として届く（`lalapad/include/dt-bindings/lalapad/tp_positions.h` の `POS_TP_*`。shield の `lalapadgen2.dtsi` と自作モジュールの C の両方が読む）。だから keymap の 52〜67 番目に書いたものが、それぞれのジェスチャーの動作になる。
- 出典：[zmk-driver-iqs9151 v1.0.0 behavior_input_processor_reference.md](https://github.com/ShiniNet/zmk-driver-iqs9151/blob/v1.0.0/documents/behavior_input_processor_reference.md)

### 自作モジュール `lalapad/` の behavior・入力処理

ソースは `lalapad/src/`、プロパティの定義は `lalapad/dts/bindings/`。behavior のインスタンスは keymap の `behaviors` で定義している（Keymap Editor が keymap 内の定義しか認識しないため）。

| compatible | keymap での名前 | 引数 | 主なプロパティ | 働き |
| --- | --- | --- | --- | --- |
| `zmk,behavior-kana` | `kana_key` | 1（`KN_…` `KE_…`。`dt-bindings/kana/kana.h`） | `tap-ms` `wait-ms`（既定 5）、`ime-resync-ms`（既定 1000） | かな1文字のローマ字、Space・Enter・Backspace・Esc・Tab を送信キューで順に送る |
| `zmk,behavior-kana-mode` | `kana_sw` | 1（`KANA_SW_ON` `KANA_SW_OFF`） | `layer`（必須。かな単打面の番号）、`sub-layers`（抜けるときにいっしょに解除するレイヤー）、`tap-ms` `wait-ms` | かなモードのオン・オフ（IME とかなレイヤーの切り替え） |
| `zmk,behavior-kana-seq` | `ks_…` `pair_kagi` `pair_nijukagi` `pair_paren` `pair_cquote_ime` `sym_…_ime` `sym_ellipsis` | 0 | `keys`（必須。キーコードの配列）、`ime-on`、`tap-ms` `wait-ms` `ime-resync-ms` | 決まった打鍵の列を、かなと同じ送信キューで送る |
| `zmk,behavior-alt-code` | `sym_hbar` `sym_tilde` `pair_cquote` `kj_ji` `kj_hito` `kj_hi` | 0 | `codes`（必須）、`cursor-left`、`commit-ime`、`tap-ms` `wait-ms` `ime-wait-ms` | Windows の Alt コードで文字を送る（先に並んでいるかななどの送信を待ってから送る。送信中のほかのキー入力は預かり、送り終えてから届いた順に通す。押すと Num Word を終える） |
| `zmk,behavior-mouse-button-toggle` | `clk_lock_l` `_m` `_r` | 0 | `button`（必須。`LCLK` など） | マウスボタンの押しっぱなし切り替え（同じボタンのクリックや、接続先の切り替えでも解除）。切り替えは、モジュールの `lalapad/dts/behaviors/mouse_button_toggle.dtsi` にある入力デバイス `mouse_lock_input`（`zmk,mouse-button-lock-input`。keymap に書かなくてよい）を通して入力スレッドで行う |
| `zmk,behavior-key-lock` | `mod_lock_…` | 0 | `keycode`（必須） | キーの押しっぱなし切り替え（押しっぱなしかどうかは自分で覚える。ほかのキーで同じキーを押していても独立して切り替わる。接続先の切り替えで解除） |
| `zmk,behavior-cursor-slow` | `cur_slow_hold` `_toggle` `_once` | 0 | `mode`（必須。`CURSOR_SLOW_HOLD` など） | カーソル低速のオン・オフ |
| `zmk,behavior-scroll-snap-hold` | `scr_snap_hold` | 0 | — | 押している間だけスクロールを縦か横にそろえる |
| `zmk,behavior-word-layer` | `num_word` `num_hold` | 1（レイヤー番号） | `continue-list`、`ignore-numbers`、`ignore-alphas`、`ignore-modifiers`、`exit-layers`、`momentary` | 打っている間だけ続くレイヤー（Num Word）。continue-list と ignore のキー以外のキー（キーコード）を押したら解除する。有効な間にもう一度押すと解除。ほかの操作でレイヤーが解除されたら終わる。`exit-layers` のレイヤーが有効になったら終わる（このリポジトリでは記号レイヤー。数字＋記号でシステムレイヤーにならないように）。有効な間は始めない。Alt コードのキーが自分で送るキーでは終わらない。`momentary` を付けると押している間だけ有効（`&mo` と同じ。同じレイヤーの Num Word が有効なら、終えずに引き継ぐ）。ほかのプロパティは urob/zmk-auto-layer の auto-layer と同じ。keymap の hold-tap `num_mo`（タップで `&num_word`、長押しで `&num_hold`）から使う |
| `zmk,behavior-layer-lock` | `lyr_lock` | 0 | — | 置いたレイヤーを固定する／固定をやめる（Layer Lock。固定している間は、そのレイヤーが解除されてもすぐ有効にし直す。固定をやめるとそのレイヤーを解除する。オートマウスレイヤーなら解除せず時間を数え直す。Default と conditional_layers の then-layer は固定しない。かなのレイヤー（kana_sw の layer・sub-layers）の固定は、かなモードを抜けるとやめる（layer_lock_forget）。combos に置くと、いちばん上の有効なレイヤーを固定する。マクロの中では何もしない） |
| `zmk,behavior-alt-tab` | `swap_next` `swap_prev` | 0 | `reverse`（Shift+Tab を送る） | ウィンドウの切り替え。最初に押したときに Alt を押したままにして Tab を送り、押すたびに Tab を送る。押し始めたレイヤーの解除、ほかのキー（キーコードを送るもの。Alt コードの送信が自分で送るキーを除く）の押下、接続先の切り替えで Alt を離す（順方向・逆方向で Alt を共有）。Default に置いたときと、コンボから押したときは、レイヤーの解除では離さない |
| `zmk,behavior-tp-keys` | `tp_arrow` `tp_dial` | 0 | `bindings`（必須。8 つ: 1 本指を上・下・左・右、2 本指を上・下・左・右へ動かしたとき）、`cursor-step` `scroll-step`（既定 150。トラックパッドの生の値）、`tap-ms`（既定 10） | 押している間だけ、トラックパッドを動かした量ごとに、その向きの binding を送る（4 つとも `&trans` の側はカーソル・スクロールのまま）。押している間のタップ・ジェスチャーは捨てる |
| `zmk,behavior-tp-stroke` | `tp_draw` | 0 | `strokes`（必須。`TP_STROKE1`〜`4`。`dt-bindings/lalapad/tp_keys.h`）、`bindings`（必須。strokes と同じ数）、`stroke-step`（既定 300）、`tap-ms`（既定 10） | 押している間に 1 本指で描いた向きの並びを、指（またはキー）を離したときに binding に変える |
| `zmk,input-processor-auto-mouse-layer` | `zip_auto_mouse_layer`（dtsi） | 0 | `layer`（必須。有効にするレイヤー）・`timeout-ms`（解除までの ms。0 なら時間では解除しない）・`excluded-layers`・`excluded-positions`・`passive-positions`（keymap で指定）、`require-prior-idle-ms`、`touch-devices`（親機の overlay で指定）、`touch-stale-ms` | カーソル移動・スクロールでマウスレイヤーを一時的に有効にする（タップ・ジェスチャーや触れただけでは有効にしない）。マウスレイヤーで `&trans` の位置、またはマウスレイヤーより上の有効なレイヤー（かな・左手用など）に割り当てのある位置を押すと解除。`excluded-positions` の位置は解除せず、押している間は時間切れにしない（離してから数え直す）。`passive-positions` の位置は、割り当てが働いても解除も延長もしない。`&none` の位置は、押しても何も入力せず、解除もしない（押している間は時間切れにしない。`passive-positions` の位置は除く）。`excluded-layers` が有効な間は有効にしない。`require-prior-idle-ms` の「直前のキー入力」は、キーコードの押下と、トラックパッド由来でない位置の押下。Alt コードを送っている間も有効にしない |
| `zmk,input-processor-cursor-slow` | `zip_cursor_slow`（`behaviors/cursor_slow.dtsi`） | 2（倍率の分子、分母） | — | カーソル低速の間、カーソル移動を遅くする |
| `zmk,input-processor-scroll-snap-gate` | `zip_scroll_snap_gate_R` `_L`（dtsi） | 0 | `processor`（必須） | `scr_snap_hold` を押している間だけ、スクロールスナップにイベントを渡す |
| `zmk,input-processor-tp-keys` | `zip_tp_keys`（`behaviors/tp_keys.dtsi`） | 0 | — | `tp_` のキーを押している間、トラックパッドの動きをキーに変える。リスナーの入力処理の並びの先頭に入れる |

- 引数なし（0）の behavior は、ZMK Studio から選べるよう `zmk_behavior_get_empty_param_metadata` を返す。
- `kana_sync`（`behaviors/kana.dtsi`）は送信キューの処理完了の合図に使う内部用で、keymap には書かない（ZMK Studio の一覧には出るが、割り当てられない）。
