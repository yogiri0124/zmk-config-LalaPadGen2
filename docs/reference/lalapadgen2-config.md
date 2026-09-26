# LaLaPad Gen2 トラックパッド設定（IQS9151 Kconfig）一覧

出典：[ShiniNet/LaLaPadGen2 guide/ConfigList.md](https://github.com/ShiniNet/LaLaPadGen2/blob/main/guide/ConfigList.md)
（2026-09-26 取得）。このリポジトリが固定しているドライバ `zmk-driver-iqs9151` **v1.0.0** の
`drivers/input/Kconfig` と照合し、全48項目の名前・既定値が一致することを確認済み。
「現在値」列は同日時点のこのリポジトリの設定（編集後は古くなるので、最新値は各 `.conf` を見ること）（`左 / 右`、`—` は未記載＝既定値が使われる）。

## 書く場所と反映範囲

| ファイル | 反映範囲 |
| --- | --- |
| `config/lalapadgen2.conf` | 左右両方 |
| `config/boards/shields/lalapadgen2/lalapadgen2_left.conf` | 左のみ |
| `config/boards/shields/lalapadgen2/lalapadgen2_right.conf` | 右のみ |
| `config/lalapadgen2_left.conf` | 左のみ（親機専用の設定を置く。LaLapad-Gen2-Editor の管理外） |

- 現状は、左右の `.conf` に同じ値を明示的に書いている。片側だけ変える依頼なら該当側だけを編集する。
  両側を変える依頼なら左右両方を同じように編集する（共通 `.conf` へ移すのは依頼があるときだけ）。
- 同じ項目を共通と左右の両方に書くと、どちらが効くか分かりにくくなるので避ける。
- 左が central（`Kconfig.defconfig`）。トラックパッドの設定は、そのトラックパッドが付いている側で効く。
- `入力値` は bool が `y / n`、数値は `最小..最大`。`整数` は Kconfig に範囲制約がない項目。
- **Rotation** は choice。4つのうち1つだけを `y` にする（他は書かないか `n`）。
- 範囲外の値や存在しない名前は、ビルドの警告・エラーになる。変更後は CI ログの Kconfig 警告を確認する。


## 1. Driver Core

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151` | `y` | `y / n` | IQS9151ドライバ有効化 | 共通 `y` |
| `CONFIG_INPUT_IQS9151_LOG_LEVEL` | `INPUT_LOG_LEVEL`（LOG有効時）/ `0` | `0..4` | ドライバログレベル | — / — |
| `CONFIG_INPUT_IQS9151_INIT_PRIORITY` | `80` | `整数` | ドライバ初期化優先度 | — / — |

## 2. Rotation

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151_ROTATE_0` | `y` | `y / n` | 回転なし | `y` / `y` |
| `CONFIG_INPUT_IQS9151_ROTATE_90` | `n` | `y / n` | 90度回転 | — / — |
| `CONFIG_INPUT_IQS9151_ROTATE_180` | `n` | `y / n` | 180度回転 | — / — |
| `CONFIG_INPUT_IQS9151_ROTATE_270` | `n` | `y / n` | 270度回転 | — / — |

## 3. IC Parameter Overrides

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151_RESOLUTION_X` | `2457` | `0..4095` | X解像度設定 | `2457` / `2457` |
| `CONFIG_INPUT_IQS9151_RESOLUTION_Y` | `3072` | `0..4095` | Y解像度設定 | `3072` / `3072` |
| `CONFIG_INPUT_IQS9151_ATI_TARGETCOUNT` | `400` | `0..1000` | Trackpad ATIターゲット | `400` / `400` |
| `CONFIG_INPUT_IQS9151_DYNAMIC_FILTER_BOTTOM_SPEED` | `30` | `0..2047` | Dynamic Filter Bottom Speed | `30` / `30` |
| `CONFIG_INPUT_IQS9151_DYNAMIC_FILTER_TOP_SPEED` | `511` | `0..2047` | Dynamic Filter Top Speed | `511` / `511` |
| `CONFIG_INPUT_IQS9151_DYNAMIC_FILTER_BOTTOM_BETA` | `20` | `0..255` | Dynamic Filter Bottom Beta | `20` / `20` |

## 4. Gesture Detection and Thresholds

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151_1F_TAP_ENABLE` | `y` | `y / n` | 1F Tap 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_1F_TAP_MAX_MS` | `250` | `1..1000` | 1F Tap/2回目Tap 判定の最大時間 | `250` / `250` |
| `CONFIG_INPUT_IQS9151_1F_TAP_MOVE` | `50` | `1..1000` | 1F Tap 移動しきい値 | `50` / `50` |
| `CONFIG_INPUT_IQS9151_1F_PRESSHOLD_ENABLE` | `y` | `y / n` | 1F TapDrag 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_1F_TAPDRAG_GAP_MAX_MS` | `160` | `1..1000` | 1F Tap後にBTN0を保持して2回目タッチを待つ最大時間 | `160` / `160` |
| `CONFIG_INPUT_IQS9151_2F_TAP_ENABLE` | `y` | `y / n` | 2F Tap 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_2F_TAP_MAX_MS` | `250` | `1..1000` | 2F Tap 最大時間 | `250` / `250` |
| `CONFIG_INPUT_IQS9151_2F_TAP_MOVE` | `50` | `1..1000` | 2F Tap 移動しきい値（重心/距離） | `50` / `50` |
| `CONFIG_INPUT_IQS9151_2F_PRESSHOLD_ENABLE` | `y` | `y / n` | 2F TapDrag 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_2F_TAPDRAG_GAP_MAX_MS` | `200` | `1..1000` | 2F Tap後にBTN1を保持して2回目2Fタッチを待つ最大時間 | `200` / `200` |
| `CONFIG_INPUT_IQS9151_SCROLL_X_ENABLE` | `y` | `y / n` | 2F 横スクロール有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_SCROLL_Y_ENABLE` | `y` | `y / n` | 2F 縦スクロール有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_2F_SCROLL_START_MOVE` | `50` | `1..2000` | 2F Scroll 開始しきい値 | `50` / `50` |
| `CONFIG_INPUT_IQS9151_2F_PINCH_ENABLE` | `y` | `y / n` | 2F Pinch 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_2F_PINCH_START_DISTANCE` | `100` | `1..2000` | 2F Pinch 開始しきい値 | `100` / `100` |
| `CONFIG_INPUT_IQS9151_2F_PINCH_WHEEL_GAIN_X10` | `40` | `1..100` | 2F Pinch `REL_WHEEL` ゲイン（x10） | `40` / `40` |
| `CONFIG_INPUT_IQS9151_3F_TAP_ENABLE` | `y` | `y / n` | 3F Tap 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_3F_TAP_MAX_MS` | `200` | `1..1000` | 3F Tap 最大時間 | `200` / `200` |
| `CONFIG_INPUT_IQS9151_3F_TAP_MOVE` | `35` | `1..1000` | 3F Tap 移動しきい値 | `35` / `35` |
| `CONFIG_INPUT_IQS9151_3F_PRESSHOLD_ENABLE` | `y` | `y / n` | 3F TapDrag 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_3F_TAPDRAG_GAP_MAX_MS` | `200` | `1..1000` | 3F Tap後にBTN2を保持して2回目3Fタッチを待つ最大時間 | `200` / `200` |
| `CONFIG_INPUT_IQS9151_3F_SWIPE_THRESHOLD` | `200` | `0..1000` | 3F Swipe しきい値 | `200` / `200` |

## 5. Inertia

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_ENABLE` | `y` | `y / n` | 1Fカーソル慣性 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_DECAY` | `950` | `0..1000` | 1Fカーソル慣性 減衰率 | `950` / `950` |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_RECENT_WINDOW_MS` | `60` | `1..500` | 1Fカーソル慣性の recent-window 判定時間 | `60` / `60` |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_STALE_GAP_MS` | `35` | `1..500` | 最終1F移動から release までの最大許容時間 | `35` / `35` |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_MIN_SAMPLES` | `2` | `1..12` | 1Fカーソル慣性に必要な直近移動サンプル数 | `2` / `2` |
| `CONFIG_INPUT_IQS9151_CURSOR_INERTIA_MIN_AVG_SPEED` | `10` | `1..500` | 1Fカーソル慣性に必要な平均速度 | `10` / `10` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_ENABLE` | `y` | `y / n` | 2Fスクロール慣性 有効/無効 | `y` / `y` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_DECAY` | `980` | `0..1000` | 2Fスクロール慣性 減衰率 | `980` / `980` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_RECENT_WINDOW_MS` | `60` | `1..500` | 2Fスクロール慣性の recent-window 判定時間 | `60` / `60` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_STALE_GAP_MS` | `35` | `1..500` | 最終2Fスクロールから release までの最大許容時間 | `35` / `35` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_MIN_SAMPLES` | `1` | `1..12` | 2Fスクロール慣性に必要な直近スクロールサンプル数 | `1` / `1` |
| `CONFIG_INPUT_IQS9151_SCROLL_INERTIA_MIN_AVG_SPEED` | `4` | `1..500` | 2Fスクロール慣性に必要な平均速度 | `4` / `4` |

## 6. Test（開発用。キーボードの設定では使わない）

| Kconfig名 | 既定値 | 入力値 | 概要 | 現在値（左 / 右） |
| --- | --- | --- | --- | --- |
| `CONFIG_INPUT_IQS9151_TEST` | `n` | `y / n` | ZTEST用の内部テストフック有効化（`depends on ZTEST`） | — / — |
