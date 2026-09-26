@AGENTS.md

# CLAUDE.md — Claude Code 専用ルール

上記 AGENTS.md の共通ルールに加えて、Claude Code は以下に従う。

## 作業開始時

1. メインのフォルダで `git status` を確認する。未コミットの変更があれば、内容をユーザーに報告してから進める（勝手に破棄・stash しない）。
2. `.review/latest.md`（メインのフォルダ）が存在すれば、修正に入る前に必ず読む。

## 作業用フォルダ（git worktree）

ユーザーはメインのフォルダ（リポジトリのルート）を LaLapad-Gen2-Editor で開いている。
作業中のブランチでメインのフォルダの中身を入れ替えないよう、ブランチは作業用フォルダで開く。

- 作成：メインのフォルダで `git pull --ff-only` の後、`git worktree add .worktrees/<名前> -b <ブランチ> main`
  （`<名前>` はブランチ名の `/` を `-` に置き換えたもの。例：`feat/xxx` → `.worktrees/feat-xxx`）
- 既存ブランチを開く：`git worktree add .worktrees/<名前> <ブランチ>`
- 編集・コミット・Codex レビュー・push は作業用フォルダで行う。**メインのフォルダでは `git checkout` でブランチを切り替えない。**
- main の取り込み（main が進んだとき）は作業用フォルダで `git merge main`。
- マージ・main の push・`.uf2` の取得はメインのフォルダで行う（下記「GitHub Actions / Artifacts」）。
- マージ後に `git worktree remove .worktrees/<名前>` で作業用フォルダを削除する。ブランチ自体は残す。
- `.review/` と `firmware/` はメインのフォルダの1か所に集約する（作業用フォルダには作らない）。
- `.worktrees/` は git 管理外（`.gitignore`）。

## Codex によるレビューの呼び出し方

コマンドは Git Bash（Bash ツール）で、作業用フォルダから実行する。`MAIN` はメインのフォルダ（リポジトリのルート）の絶対パス。

```bash
MAIN=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")
R="$MAIN/.review"
mkdir -p "$R"
BASE=$(git rev-parse main); HEAD_SHA=$(git rev-parse HEAD); MB=$(git merge-base main HEAD)
N=1   # レビュー回数（1〜3）
T=1; while [ -e "$R/${HEAD_SHA:0:12}-r$N-t$T.md" ] || [ -e "$R/${HEAD_SHA:0:12}-r$N-t$T.log" ]; do T=$((T+1)); done
RUN=${HEAD_SHA:0:12}-r$N-t$T   # 試行番号 t は既存ファイルと衝突しない番号
OUT=$R/$RUN.md
codex exec -s read-only -C . --ephemeral --color never -o "$OUT" - <<EOF > "$R/$RUN.log" 2>&1
AGENTS.mdのレビュー観点に沿ってmainとの差分をレビューせよ。
依頼内容: <ここに依頼内容>
基準 (main): $BASE / merge-base: $MB / 対象 HEAD: $HEAD_SHA
差分は git diff $MB..$HEAD_SHA と git diff --stat $MB..$HEAD_SHA で確認し、git status --short・git diff --cached・git diff・未追跡ファイルも確認すること。
ファイルは一切変更しないこと。
最終回答は AGENTS.md の「レビュー出力の書式」に従い、日本語で書くこと。対象 HEAD と基準の SHA を必ず記載すること。
EOF
RC=$?; echo "exit=$RC out=$OUT"
```

採用判定（すべて満たしたときだけ `latest.md` を更新する）：

```bash
[ $RC -eq 0 ] && [ -s "$OUT" ] \
  && grep -q '^## 必須修正' "$OUT" && grep -q '^## 推奨' "$OUT" \
  && grep -q '^## 確認事項' "$OUT" && grep -q '^## 要約' "$OUT" \
  && grep -q "対象 HEAD: *$HEAD_SHA" "$OUT" \
  && [ "$(git rev-parse HEAD)" = "$HEAD_SHA" ] \
  && cp "$OUT" "$R/latest.md" && echo 採用 || echo "未レビュー（採用条件を満たさない）"
```

- `-s read-only`：読み取り専用サンドボックス（Codex はファイルを書き換えられない）。
- `-o`：Codex の**最終メッセージだけ**をファイルに書き出す（書き込みは codex CLI 自身が行う）。
- `--ephemeral`：セッションファイルを残さない。途中経過のログは結果と同名の `.log`。結果・ログとも実行ごとに別ファイルになり上書きされない（`.review/` は git 管理外）。
- ヒアドキュメントは変数展開のため `<<EOF`（クォートなし）にする。依頼内容に `$` やバッククォートが含まれる場合はエスケープする。
- 採用判定は AGENTS.md「結果の採用条件」に対応する。満たさなければ未レビュー扱いとし、`latest.md` は更新しない（前回の採用結果を残す）。
- 合格は「## 必須修正」が「なし」**かつ**「確認事項」に「マージを妨げるか：はい」が残っていないこと。妨げる確認事項があれば、解消できるものは解消して再レビューし、ユーザーの判断が必要なものは止めて報告する。レビューは初回を含め最大3回。

## GitHub Actions / Artifacts

- `gh` CLI を使う（PATH にない場合は `"/c/Program Files/GitHub CLI/gh.exe"`）。`gh` が使えない・未ログインの場合はその時点で止めて報告する。
- push 後の run 特定：
  `gh run list --workflow build.yml --branch <branch> --commit <SHA> --event push --json databaseId,headSha,status,conclusion,url`
  現れるまで待ち（最大10分）、`gh run watch <id> --exit-status` で完了を待つ。`gh run view <id> --json jobs` で全ビルド対象のジョブが success であることを確認する。
- 失敗時は `gh run view <id> --log-failed` でログを確認し、修正 → 再レビューの流れに戻る。
- 成功後、**メインのフォルダで** `git status` を確認し（未コミットの変更があれば報告）、`git pull --ff-only` で main が進んでいないか確認してから `git merge --no-ff <branch>` → `git push origin main`。
  main が進んでいた場合は、作業用フォルダで `git merge main` してから再レビュー・CI 確認をやり直す。
- main のマージコミット SHA で同様に run を特定・成功確認し、メインのフォルダで `gh run download <id> -D firmware/<マージコミットSHA>` で取得する。3種類の `.uf2` がそろい空でないことを確認する。
- 最後に `git worktree remove .worktrees/<名前>` で作業用フォルダを削除する。

## その他

- シェルは Git Bash を優先する（PowerShell とのクォートの違いに注意）。
- 独自 behavior・combos・エディタ管理の `&mc*` / `&td*` 定義を触る変更は、依頼に明記されている場合に限る。
