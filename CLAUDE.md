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
BASE=$(git rev-parse main); HEAD_SHA=$(git rev-parse HEAD); MB=$(git merge-base main HEAD)
N=1   # レビュー回数（1〜3）
RUN=${HEAD_SHA:0:12}-r$N-$(date +%Y%m%d-%H%M%S)   # 再試行ごとに別名になる
OUT=.review/$RUN.md
codex exec -s read-only -C . --ephemeral --color never -o "$OUT" - <<EOF > ".review/$RUN.log" 2>&1
AGENTS.mdのレビュー観点に沿ってmainとの差分をレビューせよ。
依頼内容: <ここに依頼内容>
基準 (main): $BASE / merge-base: $MB / 対象 HEAD: $HEAD_SHA
差分は git diff $MB..$HEAD_SHA と git diff --stat $MB..$HEAD_SHA で確認し、git status --short・git diff --cached・git diff・未追跡ファイルも確認すること。
ファイルは一切変更しないこと。
最終回答は AGENTS.md の「レビュー出力の書式」に従い、日本語で書くこと。対象 HEAD と基準の SHA を必ず記載すること。
EOF
RC=$?; echo "exit=$RC"
if [ $RC -eq 0 ] && [ -s "$OUT" ]; then cp "$OUT" .review/latest.md; else echo "未レビュー（codex 失敗または出力なし）"; fi
```

- `-s read-only`：読み取り専用サンドボックス（Codex はファイルを書き換えられない）。
- `-o`：Codex の**最終メッセージだけ**をファイルに書き出す（書き込みは codex CLI 自身が行う）。
- `--ephemeral`：セッションファイルを残さない。途中経過のログは結果と同名の `.log`。結果・ログとも実行ごとに別ファイルになり上書きされない（`.review/` は git 管理外）。
- ヒアドキュメントは変数展開のため `<<EOF`（クォートなし）にする。依頼内容に `$` やバッククォートが含まれる場合はエスケープする。
- 採用前に AGENTS.md「結果の採用条件」を確認する（終了コード0、非空、4見出し、記載 SHA＝現在の HEAD）。満たさなければ未レビュー扱い。
- 合格は「## 必須修正」が「なし」**かつ**「確認事項」に「マージを妨げるか：はい」が残っていないこと。妨げる確認事項があれば、解消できるものは解消して再レビューし、ユーザーの判断が必要なものは止めて報告する。レビューは初回を含め最大3回。

## GitHub Actions / Artifacts

- `gh` CLI を使う（PATH にない場合は `"/c/Program Files/GitHub CLI/gh.exe"`）。`gh` が使えない・未ログインの場合はその時点で止めて報告する。
- push 後の run 特定：
  `gh run list --workflow build.yml --branch <branch> --commit <SHA> --event push --json databaseId,headSha,status,conclusion,url`
  現れるまで待ち（最大10分）、`gh run watch <id> --exit-status` で完了を待つ。`gh run view <id> --json jobs` で全ビルド対象のジョブが success であることを確認する。
- 失敗時は `gh run view <id> --log-failed` でログを確認し、修正 → 再レビューの流れに戻る。
- 成功後、`git checkout main && git pull --ff-only` で main が進んでいないか確認してから `git merge --no-ff <branch>` → `git push origin main`。
- main のマージコミット SHA で同様に run を特定・成功確認し、`gh run download <id> -D firmware/<マージコミットSHA>` で取得する。3種類の `.uf2` がそろい空でないことを確認する。

## その他

- シェルは Git Bash を優先する（PowerShell とのクォートの違いに注意）。
- 独自 behavior・combos・エディタ管理の `&mc*` / `&td*` 定義を触る変更は、依頼に明記されている場合に限る。
