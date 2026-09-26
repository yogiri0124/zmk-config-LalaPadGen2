---
description: 依頼内容を受けて、ブランチ作成→編集→Codexレビュー→ビルド確認→mainマージ→.uf2取得まで一気に実行する
argument-hint: <依頼内容>
---

# /ship

依頼内容: **$ARGUMENTS**

CLAUDE.md と AGENTS.md のルールに従い、以下を確認なしで最後まで実行すること。

## 0. 事前確認
- `git status` を確認。未コミットの変更があれば報告してから進める（破棄しない）。
- メインのフォルダの `.review/latest.md` があれば読む。
- メインのフォルダが main で最新であることを確認（`git pull --ff-only`）。

## 1. ブランチ作成（作業用フォルダ）
- 依頼内容から短い英語スラッグを作り、CLAUDE.md「作業用フォルダ（git worktree）」の手順で `git worktree add .worktrees/feat-<slug> -b feat/<slug> main`（不具合修正なら `fix/<slug>`）。
- 以降の編集・コミット・レビュー・push は作業用フォルダで行う。

## 2. 編集してコミット
- AGENTS.md の「編集範囲」内だけを編集する。依頼以外の変更・空白整形は入れない。
- 各レイヤーの bindings が68個であること、独自 behavior と combos が残っていることを自分でも確認する。
- 日本語のコミットメッセージでコミットする。

## 3. Codexレビュー
- CLAUDE.md の「Codex によるレビューの呼び出し方」のコマンドで作業用フォルダから実行し、メインのフォルダの `.review/latest.md`（コマンド中の `"$R/latest.md"`）を読む。

## 4. 修正ループ（最大3往復）
- 「必須修正」または「マージを妨げる確認事項」があれば修正してコミット → 再レビュー。ユーザーの判断が必要な確認事項なら止めて報告する。
- レビューは初回を含め最大3回。3回目のレビューでも合格しなければ、**ここで作業を止めて**ブランチ名・未解決の事項を報告する（push しない）。

## 5. push とビルド確認（合格＝必須修正なし かつ マージを妨げる確認事項なし）
- `git push -u origin <branch>`（force は禁止）。
- CLAUDE.md「GitHub Actions / Artifacts」の手順で、対象コミット SHA に一致する run を特定し、全ビルド対象の成功を確認する。
- 失敗したら `gh run view <id> --log-failed` を確認して修正 → 手順3へ（往復回数に含める）。

## 6. マージと .uf2 取得
- メインのフォルダで `git status` を確認し、`git pull --ff-only` で main が進んでいないか確認（進んでいたら作業用フォルダで `git merge main` して手順3からやり直す）。
- メインのフォルダで `git merge --no-ff <branch> -m "<日本語メッセージ>"` → `git push origin main`。
- マージコミット SHA の CI 成功を確認し、その run から `firmware/<マージコミットSHA>/`（メインのフォルダ）に取得する。左・右・settings_reset の3種類がそろっていることを確認する。
- `git worktree remove .worktrees/<名前>` で作業用フォルダを削除する。

## 7. 報告
以下を日本語で報告する：
- 変更内容（ファイルと要点）
- レビュー要約（往復回数、最終的な推奨・確認事項）
- `.uf2` の場所（左右・settings_reset それぞれのパス）と元の run URL・コミット SHA
- 実機で確認すべき項目（CI 合格と実機未確認を分けて書く）。キーマップ変更時は Studio の「Restore Stock Settings」が必要な場合があることを添える
