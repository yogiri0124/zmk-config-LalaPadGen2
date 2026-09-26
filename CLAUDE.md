@AGENTS.md

# CLAUDE.md — Claude Code 専用ルール

上記 AGENTS.md の共通ルールに加えて、Claude Code は以下に従う。

## 作業開始時

1. `git status` を確認する。未コミットの変更があれば、内容をユーザーに報告してから進める（勝手に破棄・stash しない）。
2. `.review/latest.md` が存在すれば、修正に入る前に必ず読む。

## Codex によるレビューの呼び出し方

コマンドは Git Bash（Bash ツール）で、リポジトリのルートから実行する。

```bash
mkdir -p .review
codex exec -s read-only -C . --ephemeral --color never -o .review/latest.md - <<'EOF' > .review/codex.log 2>&1
AGENTS.mdのレビュー観点に沿ってmainとの差分をレビューせよ。
差分は `git diff main...HEAD` と `git diff --stat main...HEAD` で確認すること（未コミットの変更があれば `git diff` も確認）。
ファイルは一切変更しないこと。
最終回答は AGENTS.md の「レビュー出力の書式」に従い、「必須修正」「推奨」「確認事項」に分けて日本語で書くこと。
差分がない場合は「差分なし」と書き、各区分は「なし」とすること。
EOF
```

- `-s read-only`：読み取り専用サンドボックス（Codex はファイルを書き換えられない）。
- `-o .review/latest.md`：Codex の**最終メッセージだけ**を `.review/latest.md` に書き出す（書き込みは codex CLI 自身が行う）。
- `--ephemeral`：セッションファイルを残さない。
- 途中経過のログは `.review/codex.log` に捨てる（`.review/` は git 管理外）。
- 実行後に `.review/latest.md` を読み、「## 必須修正」が「なし」かどうかで判定する。
- 往復回数は `.review/round` などで数え、3回目のレビューでも必須修正が残れば作業を止めて報告する。

## GitHub Actions / Artifacts

- `gh` CLI を使う。push 後に `gh run list --branch <branch> --limit 1` で run ID を取り、`gh run watch <id> --exit-status` で完了を待つ。
- 失敗時は `gh run view <id> --log-failed` でログを確認し、修正 → 再レビューの流れに戻る。
- 成功後、main にマージ（`git checkout main && git pull --ff-only && git merge --no-ff <branch>`）して `git push origin main`。
- `.uf2` は `gh run download <id> -D firmware/<branch名>` で取得する（`firmware/` は git 管理外）。
- `gh` が使えない場合はその時点で止めて報告する。

## その他

- シェルは Git Bash を優先する（PowerShell とのクォートの違いに注意）。
- 独自 behavior・combos・エディタ管理の `&mc*` / `&td*` 定義を触る変更は、依頼に明記されている場合に限る。
