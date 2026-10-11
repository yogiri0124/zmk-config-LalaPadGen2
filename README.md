# zmk-config-LalaPadGen2（カスタム版）

分割キーボード **LaLaPad Gen2**（左右にトラックパッドを内蔵）の ZMK ファームウェア設定です。
[ShiniNet/zmk-config-LalaPadGen2](https://github.com/ShiniNet/zmk-config-LalaPadGen2) をもとに、配列・かな入力・トラックパッドまわりを大きく作り替えています。
キーボード本体（基板・ケース・組み立て方）については、[ShiniNet/LaLaPadGen2](https://github.com/ShiniNet/LaLaPadGen2) を見てください。

- ZMK v0.3.0 ／ ボード: Seeed XIAO BLE ／ ZMK Studio 対応
- ホストは **Windows・英語配列（US）・Google 日本語入力** を前提にしています

## 元のファームウェアとの違い

| 項目 | 元（ShiniNet 版） | このリポジトリ |
| --- | --- | --- |
| 親機（PC とつながる側） | 右 | **左**（ZMK Studio も左を USB 接続） |
| 基本の配列 | QWERTY | **Night 配列**（QWERTY はレイヤーで切り替え） |
| レイヤー数 | 4 | **10**（下の表） |
| かな入力 | なし | **独自のかな配列**（親指シフト方式。キーボードがローマ字を送る） |
| マウス操作 | クリックは常に同じ位置 | **オートマウスレイヤー**（トラックパッドに触れている間だけ、文字キーがクリックなどに変わる） |
| スクロール | 縦横自由 | 加えて、**押している間だけ縦か横にそろえる**キー |
| カーソル速度 | レイヤー 1・2 で低速 | 加えて、**カーソル低速のキー**（押している間／切り替え／次のキーまで） |
| 2本指ピンチ（拡大縮小） | 有効 | **無効** |
| レイヤー切り替え時の LED | 点灯する | **点灯しない** |
| カッコ・記号 | 通常のキーのみ | **「」『』()“” の自動補完、… ― ～ の専用キー** |
| 長押しキー（mt2）の判定 | 200ms | **350ms** |
| トラックパッドのドライバ | ShiniNet/zmk-driver-iqs9151 v1.0.0 | それに「触れている／離れた」の通知を足した複製（[yogiri0124/zmk-driver-iqs9151](https://github.com/yogiri0124/zmk-driver-iqs9151) `v1.0.0-lalapad-touch.1`） |
| 追加モジュール | — | [kot149/zmk-scroll-snap](https://github.com/kot149/zmk-scroll-snap)、このリポジトリの `kana/`（自作） |

## キーの位置番号

キーマップでは、キーを 0〜67 の番号で扱います。

```
左手                          右手
 0  1  2  3  4               5  6  7  8  9      上段
10 11 12 13 14              15 16 17 18 19      中段
20 21 22 23 24              25 26 27 28 29      下段
30 31 32 33 34 35        36 37 38 39 40 41      親指の段（33〜38 が親指）
   42 43 44 45 46        47 48 49 50 51         5方向スイッチなど
52 53 54                          55 56 57      トラックパッドのタップ（左／右）
58 59 60 61 62              63 64 65 66 67      トラックパッドのジェスチャー（左／右）
```

## レイヤー

| 番号 | 名前 | 入り方 | 内容 |
| --- | --- | --- | --- |
| 0 | Default | — | **Night 配列**。34 は R（長押しで Shift）、35 Enter、36 Backspace、37 Space |
| 1 | qwerty | 42 を押すたびに切り替え | QWERTY 配列。文字のキーを差し替え、34 は左 Shift だけのキーになる（R は 3 にある）。ほかは Default のまま |
| 2 | secondary_layer | 33 を押している間 | 数字と記号（～ # … 「」 『』 () % * ` =） |
| 3 | tertiary_layer | 38 を押している間 | 記号（& “” @ ! - ― \ $ ^）と、矢印・Home／End・PageUp／Down |
| 4 | system_layer | 33 と 38 を同時に押している間 | F1〜F15、Bluetooth の切り替え、トラックパッドの速度調整、リセット |
| 5 | mouse_layer | トラックパッドを操作すると自動で | クリック、タブ操作、切り取り・コピー・貼り付けなど |
| 6 | kana | 51 を押すと入る（もう一度押すと出る） | かな（単打） |
| 7 | kana_left | かなモード中、34 を押している間 | かな（左親指シフト） |
| 8 | kana_right | かなモード中、37 を押している間 | かな（右親指シフト）と句読点・記号 |
| 9 | kana_symbol | かなモード中、38 を押している間 | 空き（あとで割り当てる用） |

### Default（Night 配列）

[Night](https://valorance.net/night/) は、親指に文字（R）を置く配列です。

```
B F L K Q        P G O U .
N S H T M        Y C A E I
X V J D Z        ' W / ; ,
       R ⏎    ⌫ ␣
```

- 20（X）と 34（R）は、長押し（350ms）で左 Shift になります。
- 0＋1 の同時押しで Esc、10＋11 の同時押しで Tab（Default・qwerty・数字・記号・マウスの各レイヤーで有効）。

### マウスレイヤー（オートマウス）

トラックパッドを操作すると自動でオンになり、次のキーが使えます。

| 位置 | 働き |
| --- | --- |
| 11・12・13 | 中クリック・右クリック・左クリック |
| 3 | 左クリックを押しっぱなしにする（ドラッグ用。もう一度押すか、左クリックで解除） |
| 1 ／ 2 | Ctrl+W（タブを閉じる）／ Ctrl+Shift+T（閉じたタブを開き直す） |
| 5 ／ 6 | Ctrl+Shift+Tab ／ Ctrl+Tab（前のタブ／次のタブ） |
| 10 ／ 20 | Ctrl+A（すべて選択）／ Ctrl+Z（元に戻す） |
| 21・22・23 | Ctrl+X・Ctrl+C・Ctrl+V |
| 4・14・15・16 | 矢印キー（↑ ↓ ← →） |
| 34 | 押している間だけ、2本指スクロールを縦か横のどちらかにそろえる |

オンのまま保たれる条件と、消える条件は次のとおりです。

- **保たれる**: トラックパッドに指が触れている間／マウスレイヤーのキーを押している間
- **消える**: 指を離してから（キーを離してから）700ms たったとき／マウスレイヤーに割り当てのないキーを押したとき（そのキーは、普通の文字として入力されます）
  - 例外: 30〜32（Ctrl・Win・Alt）と 35（Enter）は、押してもマウスレイヤーは消えず、そのキーとして働きます（Ctrl+クリックなどのため）。17・18 は、押してもマウスレイヤーは消えず、何も入力されません。
- 文字を打った直後（150ms 以内）のトラックパッド操作では、オンになりません（打鍵中の誤動作防止）。

## かな入力

キーボード側で「かな 1 文字 → ローマ字の打鍵」に変えて送る、独自のかな配列です。PC 側は、ふつうのローマ字入力のままで使えます。

- **51** を押すと、IME をオンにしてかなモードに入ります。もう一度 51 を押すと、IME をオフにして戻ります。
- 単打・左親指シフト（34）・右親指シフト（37）の 3 面に、清音・濁音・半濁音・小書きを割り当てています。
- スペース（33）・エンター（35）・バックスペース（36）は、かなと同じ順番で送られるので、速く打っても追い越しません。
- しばらく打っていなかった後の最初の 1 文字の前に、IME オンを自動で送り直します。

かなのレイヤーに置くキーは、次の 2 種類です。

| 書き方 | 種類 | 例 |
| --- | --- | --- |
| `&kana_key KN_…` | かな | `KN_A`（あ）、`KN_KA`（か）、`KN_XTU`（っ） |
| `&kana_key KE_…` | 編集用のキー | `KE_SPACE`、`KE_ENTER`、`KE_BSPC` |
| `&ks_…` `&sym_…` `&pair_…` | 記号・カッコ | `ks_touten`（、）、`ks_kuten`（。）、`ks_chouon`（ー）、`ks_excl`（！）、`ks_ques`（？）、`sym_ellipsis`（…）、`pair_kagi`（「」） |

### 記号のキーを足すには

記号とカッコのキーは、キーマップ（`config/lalapadgen2.keymap`）の `behaviors` の中に、送る打鍵を並べて書くだけで作れます（プログラムの変更は要りません）。

```
ks_touten: ks_touten {
    compatible = "zmk,behavior-kana-seq";
    display-name = "、";
    #binding-cells = <0>;
    keys = <COMMA>;
    ime-on;
};
```

- `keys`: 順に 1 回ずつ押すキー。
- `ime-on`: 打つ前に IME をオンにする（IME のローマ字テーブルで出す記号に付ける）。
- かなと同じ送信待ちの列に並ぶので、かなのレイヤーにも、ほかのレイヤーにも置けます。速く打っても順番は変わりません。
- かなモードを抜ける途中に押したときは、送らずに捨てます（IME をオフにした後に、打鍵が出てしまわないように）。

## カスタムキー

キーマップ（`config/lalapadgen2.keymap`）に定義してある、このリポジトリ独自のキーです。名前の頭で種類を分けています。

| 名前 | 働き |
| --- | --- |
| `mt2` | 押すとキー、長押し（350ms）で別のキー。`&mt2 長押しのキー 押したときのキー` |
| `clk_lock_l` / `_m` / `_r` | 左・中・右クリックを押しっぱなしにする（もう一度押すか、同じボタンのクリックで解除） |
| `cur_slow_hold` / `_toggle` / `_once` | カーソルを 1/3 の速さにする（押している間／押すたびに切り替え／次のキーまで） |
| `mod_lock_alt` / `_ctrl` / `_shift` / `_win` | 修飾キーを押しっぱなしにする（もう一度押すと解除） |
| `scr_snap_hold` | 押している間だけ、スクロールを縦か横にそろえる |
| `ks_touten` / `ks_kuten` / `ks_chouon` / `ks_excl` / `ks_ques` | 、／。／ー／！／？ を入力する（先に IME をオンにする） |
| `pair_kagi` / `pair_nijukagi` / `pair_paren` / `pair_cquote_ime` | 「」／『』／()／“” を入力して、カーソルを中に入れる |
| `sym_ellipsis` / `sym_hbar_ime` / `sym_tilde_ime` | … ／ ― ／ ～ を入力する（Google 日本語入力のローマ字テーブル経由） |
| `sym_hbar` / `sym_tilde` / `pair_cquote` | ― ／ ～ ／ “” を Windows の Alt コードで入力する予備のキー（ローマ字テーブルを登録していない PC 用） |
| `kana_key` / `kana_sw` | かなと編集用のキーを送る／かなモードのオン・オフ |

まだどのレイヤーにも置いていないキーもあります（`clk_lock_m`・`clk_lock_r`、`cur_slow_*`、`mod_lock_*`、Alt コード版の 3 つ）。エディタで好きな位置に置けます。

## PC 側の準備（Google 日本語入力）

`…` `―` `～` `「」` `『』` `“”` のキーは、Google 日本語入力のローマ字テーブルに登録した打ち方を使います。
[docs/google-ime/romantable.txt](docs/google-ime/romantable.txt) を読み込んでください（手順は [docs/google-ime/README.md](docs/google-ime/README.md)）。

| 入力 | 出力 | 備考 |
| --- | --- | --- |
| `z=` | ― | 追加した登録 |
| `z-` | ～ | 標準の 〜（波ダッシュ）を全角チルダに変更 |
| `z{` `z}` | “ ” | 追加した登録 |
| `z.` `z[` `z]` | … 『 』 | 標準のまま |

## 対応しているエディタ

キーマップは、次の 3 つのどれでも編集できる形にしてあります。

- [ZMK Studio](https://zmk.studio/)（左を USB でつないで使う。自作のキーも一覧から選べます）
- [Keymap Editor](https://nickcoutsos.github.io/keymap-editor/)
- [LaLapad-Gen2-Editor](https://github.com/yuzurin0620/LaLapad-Gen2-Editor)

ZMK Studio で保存した配置は、キーボード本体に保存され、このリポジトリのキーマップより優先されます。
新しいファームウェアを書き込んでも配置が変わらないときは、ZMK Studio の「Restore Stock Settings」を行ってください。

## ビルドと書き込み

GitHub に push すると、GitHub Actions が 3 つのファームウェアを作ります（Actions の実行結果の Artifacts からダウンロードできます）。

| ファイル | 書き込む先 |
| --- | --- |
| `lalapadgen2_left rgbled_adapter-seeeduino_xiao_ble-zmk.uf2` | 左（親機） |
| `lalapadgen2_right rgbled_adapter-seeeduino_xiao_ble-zmk.uf2` | 右（子機） |
| `settings_reset-seeeduino_xiao_ble-zmk.uf2` | 設定をすべて消したいときだけ使う |

1. 左右それぞれ、リセットボタンを素早く 2 回押して、USB ドライブとして表示させます。
2. 対応する `.uf2` をコピーします（自動で再起動して、書き込みが終わります）。

元のファームウェアから乗り換えるときは、親機が右から左に変わるので、左右とも書き込み直し、PC とのペアリングもやり直してください。

## フォルダの構成

| 場所 | 内容 |
| --- | --- |
| `config/lalapadgen2.keymap` | キーマップ（レイヤーと、カスタムキーの定義） |
| `config/*.conf`、`config/boards/shields/lalapadgen2/` | ファームウェアとトラックパッドの設定、シールドの定義 |
| `kana/` | 自作モジュール（かな入力、オートマウスレイヤー、カーソル低速、クリック・修飾キーの押しっぱなし、スクロールスナップの切り替え、Alt コード入力） |
| `docs/reference/` | キーコードや設定項目の資料 |
| `docs/google-ime/` | Google 日本語入力のローマ字テーブル |
| `AGENTS.md`、`CLAUDE.md` | AI エージェント（Claude Code・Codex）で編集するときの運用ルール |

## クレジット

- 元のファームウェア設定・キーボード本体・トラックパッドのドライバ: [ShiniNet](https://github.com/ShiniNet)
- スクロールスナップ: [kot149/zmk-scroll-snap](https://github.com/kot149/zmk-scroll-snap)
- LED ウィジェット: [caksoylar/zmk-rgbled-widget](https://github.com/caksoylar/zmk-rgbled-widget)
- Night 配列: [Valorance](https://valorance.net/night/)

ライセンスは、元のリポジトリと同じ MIT です（[LICENSE](LICENSE)）。
