# zmk-keycodes.md を生成する: python gen_zmk_keycodes.py <ZMK v0.3.0 の keys.h> zmk-keycodes.md
import re,sys
src=open(sys.argv[1],encoding='utf-8').read().splitlines()
groups=[];cur=None
for l in src:
    m=re.match(r'/\* (.*) \*/$',l.strip())
    if m: cur=[m.group(1),[]];groups.append(cur);continue
    m=re.match(r'#define (\w+) ',l)
    if m and cur is not None: cur[1].append(m.group(1)+(' (非推奨)' if 'DEPRECATED' in l else ''))
out=[r'''# ZMK キーコード一覧（ZMK v0.3.0）

`&kp` / `&sk` / `&kt` / `&mt` の引数に使える名前の一覧。**このリポジトリが固定している ZMK v0.3.0** の
[`app/include/dt-bindings/zmk/keys.h`](https://github.com/zmkfirmware/zmk/blob/v0.3.0/app/include/dt-bindings/zmk/keys.h)（MIT License）から機械的に生成した。
説明は同ファイルのコメント（英語、USB HID の用語）をそのまま使っている。OS ごとの対応状況は公式の
[List of Keycodes](https://zmk.dev/docs/keymaps/list-of-keycodes) を参照。

- 同じ行の名前はすべて同じキー（別名）。ファイル内の既存の書き方に合わせる。
- `(非推奨)` が付いた名前は keys.h で `DEPRECATED (DO NOT USE)` とされているもの。v0.3.0 ではまだ使えるが、新しく書くときは使わない。
- ここにない名前は存在しない（ビルドエラーになる）。v0.3.0 より新しい版で追加された名前は使えない。

## 修飾キー関数

キーコードを修飾キー付きにする：`LC()` 左Ctrl、`LS()` 左Shift、`LA()` 左Alt、`LG()` 左GUI(Win/Cmd)、
`RC()` `RS()` `RA()` `RG()` は右側。入れ子可：`&kp LC(LS(T))` ＝ Ctrl+Shift+T。

## 日本語環境（JIS 配列ホスト）での注意

ZMK は **キーの位置（HID usage）** を送り、どの文字になるかはホスト OS のキーボード配列で決まる。
JIS 配列として認識されている PC では、US 配列の名前と出る文字が一致しないことがある。

| 名前 | JIS 配列ホストでの意味（Windows の例） |
| --- | --- |
| `LANGUAGE_1` (`LANG1`) | かな |
| `LANGUAGE_2` (`LANG2`) | 英数 |
| `INTERNATIONAL_1` (`INT1`) | ろ（`\` と `_`） |
| `INTERNATIONAL_2` (`INT2`) | カタカナ/ひらがな |
| `INTERNATIONAL_3` (`INT3`) | `¥` と縦棒 |
| `INTERNATIONAL_4` (`INT4`) | 変換 |
| `INTERNATIONAL_5` (`INT5`) | 無変換 |
| `GRAVE` | 半角/全角 |

- 例：`&kp AT`（US の Shift+2）は、JIS 配列ホストでは `"` になる。記号キーを変更する依頼では、
  ホストが US / JIS のどちらで認識しているかを確認事項として報告する。
- 上表は一般的な Windows/JIS の挙動。OS・IME 設定で変わるため、実機で確認すること。

## 一覧

| 説明 (keys.h のコメント) | 名前（別名） |
| --- | --- |''']
def esc(t): return t.replace('|', chr(92)+'|').replace('`', chr(92)+'`')
for c,names in groups:
    if names: out.append('| %s | %s |'%(esc(c), ' '.join(('`%s` (非推奨)'%n[:-6]) if n.endswith(' (非推奨)') else '`%s`'%n for n in names)))
open(sys.argv[2],'w',encoding='utf-8',newline=chr(10)).write(chr(10).join(out)+chr(10))
