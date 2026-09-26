---
description: 依頼内容を受けて、ブランチ作成→編集→Codexレビュー→ビルド確認→mainマージ→.uf2取得まで一気に実行する
argument-hint: <依頼内容>
---

# /ship

依頼内容: **$ARGUMENTS**

CLAUDE.md と AGENTS.md のルールに従い、以下を確認なしで最後まで実行すること。

## 0. 事前確認
- `git status` を確認。未コミットの変更があれば報告してから進める（破棄しない）。
- `.review/latest.md` があれば読む。
- main にいて最新であることを確認（`git checkout main && git pull --ff-only`）。

## 1. ブランチ作成
- 依頼内容から短い英語スラッグを作り `git checkout -b feat/<slug>`（不具合修正なら `fix/<slug>`）。

## 2. 編集してコミット
- AGENTS.md の「編集範囲」内だけを編集する。依頼以外の変更・空白整形は入れない。
- 各レイヤーの bindings が68個であること、独自 behavior と combos が残っていることを自分でも確認する。
- 日本語のコミットメッセージでコミットする。

## 3. Codexレビュー
- CLAUDE.md の「Codex によるレビューの呼び出し方」のコマンドで実行し、`.review/latest.md` を読む。

## 4. 修正ループ（最大3往復）
- 「必須修正」があれば修正してコミット → 再レビュー。
- 3回目のレビュー後も必須修正が残れば、**ここで作業を止めて**ブランチ名・残った指摘を報告する（push しない）。

## 5. push とビルド確認
- `git push -u origin <branch>`（force は禁止）。
- `gh run list --branch <branch> --limit 1 --json databaseId,headSha` で今回のコミットの run を特定し、`gh run watch <id> --exit-status` で待つ。
- 失敗したら `gh run view <id> --log-failed` を確認して修正 → 手順3へ（往復回数に含める）。

## 6. マージと .uf2 取得
- `git checkout main && git pull --ff-only && git merge --no-ff <branch> -m "<日本語メッセージ>"` → `git push origin main`。
- `gh run download <id> -D firmware/<branch>` でビルド成果物を取得し、`.uf2` の一覧を確認する。

## 7. 報告
以下を日本語で報告する：
- 変更内容（ファイルと要点）
- レビュー要約（往復回数、最終的な推奨・確認事項）
- `.uf2` の場所（左右・settings_reset それぞれのパス）
