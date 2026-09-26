# AGENTS.md — LaLapad Gen2 ZMK設定 共通ルール

このリポジトリは分割キーボード LaLapad Gen2 の ZMK ファームウェア設定です。
Claude Code（編集担当）と Codex（レビュー担当）の両者がこのルールに従います。

## 基本

- 応答とコミットメッセージは日本語で書く。
- **Claude Code** が編集・コミット・コマンド実行を担当する。
- **Codex** はレビュー専用。書き込んでよいのは `.review/` 以下のみ（通常は読み取り専用サンドボックスで起動され、レビュー結果は呼び出し側が `.review/latest.md` に保存する）。
- 両者とも、このファイルのルールの範囲内であれば、ユーザーの確認なしで編集・コマンド実行してよい。

## 作業の流れ

1. Claude Code が変更ごとにブランチを切る（例：`feat/xxx`、`fix/xxx`）。
2. 編集してコミットする。
3. Claude Code が `codex exec` で Codex にレビューさせ、結果を `.review/latest.md` に保存する。
4. 「必須修正」があれば修正 → 再レビュー。**最大3往復**で打ち切り、それでも残った場合は作業を止めてユーザーに報告する。
5. 必須修正がなくなったらブランチを push し、GitHub Actions のビルド成功を確認する。
6. 成功したら main にマージして push し、Artifacts の `.uf2` を `./firmware/` にダウンロードする。
7. 最後に「変更内容・レビュー要約・`.uf2` の場所」をユーザーに報告する。

## 編集範囲

- 編集してよい：
  - `config/lalapadgen2.keymap`
  - `config/*.conf`
  - `config/boards/shields/lalapadgen2/*.conf`
- 頼まれない限り変更しない：
  - `*.dtsi`、`*.overlay`、`config/west.yml`、`build.yaml`、`.github/workflows/`、`zephyr/module.yml`、`remap_lalapad_tdq/`
- GUIツール「LaLapad-Gen2-Editor」と併用している。エディタ管理のマクロ（`&mc0`〜）とタップダンス（`&td0`〜）の定義部分は、頼まれない限り書き換えない。

## 禁止

- `git push --force`（`-f`、`--force-with-lease` を含む）
- main への直接コミット（マージコミット以外）
- リポジトリ外のファイルの変更・削除

## レビュー観点

指摘は必ず **「必須修正」「推奨」「確認事項」** の3区分に分けて書く。該当なしの区分は「なし」と明記する。

- 各レイヤーの `bindings` が **68個ちょうど** あるか。
- 既存の独自 behavior（`mt2`、`zip_dyn_scale`、`tap_dance_layer_1and2`）と `combos` が消えていないか。
- `&mo` / `&lt`（`&tog`、`&to`、`&sl` や `conditional_layers` を含む）のレイヤー番号が実在するレイヤーを指しているか。
- コンボの `key-positions` が 0〜67 の範囲か。
- 依頼以外の変更（空白整形だけの大量差分を含む）が混ざっていないか。
- 編集範囲外のファイルが変更されていないか。

### レビュー出力の書式

```
# レビュー結果

## 必須修正
- （なければ「なし」）

## 推奨
- （なければ「なし」）

## 確認事項
- （なければ「なし」）

## 要約
（1〜3行）
```
