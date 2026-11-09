#!/usr/bin/env python3
"""漢字の表（docs/google-ime/kanji.tsv）から、次の 2 つを作る。

- lalapad/include/dt-bindings/kana/kanji.h
    keymap で使う名前（`#define KJ_HI 1` など）。`&kanji_key KJ_HI` のように書く。
    あわせて、名前と出力の一覧 KANJI_LIST（ZMK Studio の一覧に漢字を出すため。Noxrix も読む）と、数 KJ_COUNT。
    かなキーの同時押しの一覧 KANJI_CHORDS（lalapad/src/kanji_chord.c が使う）と、数 KJ_CHORD_COUNT。
- docs/google-ime/romantable.txt の漢字の行
    符号（「;」＋英字 2 文字）を漢字に変える、Google 日本語入力のローマ字テーブルの行。
    ほかの行（かな・記号）はそのまま残し、漢字の行（「;」＋英字 2 文字の行）だけを入れ替えて末尾に並べる。

使い方（リポジトリのどこからでも）:
    python docs/tools/gen_kanji.py          # 作る（書き換える）
    python docs/tools/gen_kanji.py --check  # 作ったものが表と合っているかだけ確かめる（合っていなければ終了コード 1）

表を変えたら、このスクリプトを実行し、romantable.txt を Google 日本語入力に読み込み直す。
kanji.h が変わったとき（名前を足した・変えた・消したとき、名前の番号を変えたとき、同時押しを変えたとき）は、
ファームウェアのビルドと書き込みも要る（キーボードはビルドしたときの番号の符号と同時押しの組み合わせを使うため。
ZMK Studio で番号で割り当てたキーは、Studio で番号を直す）。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "docs" / "google-ime" / "kanji.tsv"
HEADER = ROOT / "lalapad" / "include" / "dt-bindings" / "kana" / "kanji.h"
ROMANTABLE = ROOT / "docs" / "google-ime" / "romantable.txt"
KANA_H = ROOT / "lalapad" / "include" / "dt-bindings" / "kana" / "kana.h"
KANA_C = ROOT / "lalapad" / "src" / "behavior_kana.c"

# lalapad/src/behavior_kanji.c と合わせる（符号は「;」＋英字 2 文字。番号 0 は使わない）
PREFIX = ";"
LETTERS = "abcdefghijklmnopqrstuvwxyz"
MAX_ID = len(LETTERS) * len(LETTERS) - 1
# lalapad/src/kanji_chord.c の CHORD_MAX_KEYS と合わせる
CHORD_MIN_KEYS = 2
CHORD_MAX_KEYS = 3

RESERVED_NAMES = {"KJ_MAX_ID", "KJ_COUNT", "KJ_CHORD_COUNT"}
NAME_RE = re.compile(r"^KJ_[A-Z0-9_]+$")
CODE_LINE_RE = re.compile(r"^;[a-z]{2}\t")


@dataclass
class Row:
    kanji_id: int
    name: str
    output: str
    chord: list[str]  # かなの名前（KN_…。ID の小さい順）。同時押しがなければ空
    chord_text: str  # 表に書いた同時押し（例: か+く）
    memo: str


def code_of(kanji_id: int) -> str:
    return PREFIX + LETTERS[kanji_id // len(LETTERS)] + LETTERS[kanji_id % len(LETTERS)]


def read_kana() -> tuple[dict[str, str], dict[str, int]]:
    """かなの文字 → 名前（KN_…）と、名前 → ID。behavior_kana.c の KANA_LIST と kana.h から読む"""
    ids = {m[1]: int(m[2]) for m in re.finditer(r"#define (KN_\w+) (\d+)", KANA_H.read_text(encoding="utf-8"))}
    names = {}
    for m in re.finditer(r'ENTRY\((KN_\w+), "([^"]+)"', KANA_C.read_text(encoding="utf-8")):
        if m[1] != "KN_MAX_ID" and m[1] in ids:
            names[m[2]] = m[1]
    if not names:
        sys.exit(f"{KANA_C.relative_to(ROOT)} からかなの一覧を読めなかった")
    return names, ids


def read_table() -> list[Row]:
    """表を読む。番号順の一覧を返す。誤りがあれば止める"""
    kana_names, kana_ids = read_kana()
    errors: list[str] = []
    rows: list[Row] = []
    seen_ids: dict[int, int] = {}
    seen_names: dict[str, int] = {}
    seen_chords: dict[tuple[str, ...], int] = {}
    text = TABLE.read_text(encoding="utf-8-sig")
    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.rstrip("\r")
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 3:
            errors.append(f"{lineno} 行目: 番号・名前・出力の 3 つが要る（タブ区切り）")
            continue
        id_text, name, output = cols[0].strip(), cols[1].strip(), cols[2]
        chord_text = cols[3].strip() if len(cols) > 3 else ""
        memo = "\t".join(cols[4:]).strip()
        if not id_text.isdigit() or not 1 <= int(id_text) <= MAX_ID:
            errors.append(f"{lineno} 行目: 番号「{id_text}」は 1〜{MAX_ID} の整数にする")
            continue
        kanji_id = int(id_text)
        if not NAME_RE.match(name):
            errors.append(f"{lineno} 行目: 名前「{name}」は KJ_ で始まる英大文字・数字・_ にする")
        elif name in RESERVED_NAMES:
            errors.append(f"{lineno} 行目: 名前 {name} は kanji.h で使っているので使えない")
        # 出力・メモは kanji.h のコメント（/* … */）に入るので、コメントを閉じたり入れ子にしたりする文字列は使えない
        for label, value in (("出力", output), ("メモ", memo)):
            if "*/" in value or "/*" in value:
                errors.append(f"{lineno} 行目: {label}に「*/」「/*」は使えない")
        if output.strip() != output or output == "":
            errors.append(f"{lineno} 行目: 出力が空か、前後に空白がある")
        if kanji_id in seen_ids:
            errors.append(f"{lineno} 行目: 番号 {kanji_id} は {seen_ids[kanji_id]} 行目と同じ")
        if name in seen_names:
            errors.append(f"{lineno} 行目: 名前 {name} は {seen_names[name]} 行目と同じ")
        seen_ids.setdefault(kanji_id, lineno)
        seen_names.setdefault(name, lineno)

        chord: list[str] = []
        if chord_text:
            parts = [p.strip() for p in chord_text.split("+")]
            unknown = [p for p in parts if p not in kana_names]
            if unknown:
                errors.append(f"{lineno} 行目: 同時押し「{chord_text}」の「{'」「'.join(unknown)}」はかなのキーにない"
                              "（かなを + でつなぐ。例: か+く）")
            elif not CHORD_MIN_KEYS <= len(parts) <= CHORD_MAX_KEYS:
                errors.append(f"{lineno} 行目: 同時押し「{chord_text}」は {CHORD_MIN_KEYS}〜{CHORD_MAX_KEYS} 個のかなにする")
            elif len(set(parts)) != len(parts):
                errors.append(f"{lineno} 行目: 同時押し「{chord_text}」に同じかなが 2 回ある")
            else:
                chord = sorted((kana_names[p] for p in parts), key=lambda n: kana_ids[n])
                key = tuple(chord)
                if key in seen_chords:
                    errors.append(f"{lineno} 行目: 同時押し「{chord_text}」は {seen_chords[key]} 行目と同じ組み合わせ")
                seen_chords.setdefault(key, lineno)
        rows.append(Row(kanji_id, name, output, chord, chord_text, memo))
    if errors:
        sys.exit(f"{TABLE.relative_to(ROOT)} に誤りがある:\n" + "\n".join(errors))
    return sorted(rows, key=lambda r: r.kanji_id)


def macro_lines(name: str, entries: list[str]) -> list[str]:
    """関数形式のマクロ name を、1 行に 1 つずつ entries を並べて書く（空なら中身なし）"""
    if not entries:
        return [f"#define {name}"]
    return [f"#define {name} \\"] + [f"    {e} \\" for e in entries[:-1]] + [f"    {entries[-1]}"]


def make_header(rows: list[Row]) -> str:
    lines = [
        "/*",
        " * 漢字入力 (&kanji_key・かなキーの同時押し) の漢字の番号。",
        " * このファイルは docs/tools/gen_kanji.py が docs/google-ime/kanji.tsv から作る。直接書き換えないこと。",
        " * keymap では `&kanji_key KJ_…` のように使う。どの番号がどの漢字になるかは、PC 側のローマ字テーブル",
        " * （docs/google-ime/romantable.txt）で決まる（キーボードは番号の符号「;」＋英字 2 文字を送るだけ）。",
        " */",
        "",
        "#pragma once",
        "",
        "#include <dt-bindings/kana/kana.h>",
        "",
        "/* 番号の上限（符号が英字 2 文字なので 26 × 26 - 1。0 は使わない） */",
        f"#define KJ_MAX_ID {MAX_ID}",
    ]
    if rows:
        lines.append("")
        for r in rows:
            comment = f"{r.output}（{code_of(r.kanji_id)}）"
            if r.chord_text:
                comment += f" 同時押し {r.chord_text}"
            if r.memo:
                comment += f" {r.memo}"
            lines.append(f"#define {r.name} {r.kanji_id} /* {comment} */")
    lines += [
        "",
        "/* 表にある漢字の数と、名前・出力の一覧（behavior_kanji.c が ZMK Studio の一覧を作るのに使う。Noxrix も読む）。",
        " * 出力を変えただけなら、ファームウェアを書き換えなくても入る漢字は変わるが、Studio に出る文字は書き換えるまで前のまま */",
        f"#define KJ_COUNT {len(rows)}",
    ]
    lines += macro_lines("KANJI_LIST(ENTRY)", [f'ENTRY({r.name}, "{c_string(r.output)}")' for r in rows])
    chords = [r for r in rows if r.chord]
    lines += [
        "",
        "/* かなキーの同時押しの数と一覧（lalapad/src/kanji_chord.c が使う。Noxrix も読む）。",
        f" * CHORD(漢字の名前, かなの数, かな 1, かな 2, かな 3)。かなは ID の小さい順、使わない分は 0 */",
        f"#define KJ_CHORD_COUNT {len(chords)}",
    ]
    entries = []
    for r in chords:
        kana = r.chord + ["0"] * (CHORD_MAX_KEYS - len(r.chord))
        entries.append(f"CHORD({r.name}, {len(r.chord)}, {', '.join(kana)})")
    lines += macro_lines("KANJI_CHORDS(CHORD)", entries)
    return "\n".join(lines) + "\n"


def c_string(text: str) -> str:
    """C の文字列リテラルの中身にする（\\ と " を逃がす）"""
    return text.replace("\\", "\\\\").replace('"', '\\"')


def make_romantable(rows: list[Row]) -> str:
    text = ROMANTABLE.read_text(encoding="utf-8")
    kept: list[str] = []
    errors: list[str] = []
    for lineno, line in enumerate(text.replace("\r\n", "\n").split("\n"), start=1):
        if line == "":
            continue
        if CODE_LINE_RE.match(line):
            continue  # 前に作った漢字の行（作り直す）
        if line.startswith(PREFIX):
            # 「;」や「;a」など、漢字の符号と重なる行があると、符号が漢字にならない
            errors.append(f"{lineno} 行目: 「{line.split(chr(9))[0]}」は漢字の符号（{PREFIX}＋英字 2 文字）と重なる")
        kept.append(line)
    if errors:
        sys.exit(f"{ROMANTABLE.relative_to(ROOT)} に誤りがある:\n" + "\n".join(errors))
    kept += [f"{code_of(r.kanji_id)}\t{r.output}" for r in rows]
    return "\r\n".join(kept) + "\r\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="書き換えずに、表と合っているかだけ確かめる")
    args = parser.parse_args()

    rows = read_table()
    outputs = {HEADER: make_header(rows), ROMANTABLE: make_romantable(rows)}
    stale = []
    for path, content in outputs.items():
        current = path.read_bytes() if path.exists() else None
        data = content.encode("utf-8")
        # 改行の違いだけなら書き換えない（git の core.autocrlf で取り出したときに CRLF になることがあるため）
        if current is not None and current.replace(b"\r\n", b"\n") == data.replace(b"\r\n", b"\n"):
            continue
        stale.append(path)
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    for path in stale:
        verb = "表と合っていない" if args.check else "書き換えた"
        print(f"{verb}: {path.relative_to(ROOT)}")
    if not stale:
        chords = sum(1 for r in rows if r.chord)
        print(f"表と合っている（漢字 {len(rows)} 個、同時押し {chords} 個）")
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    sys.exit(main())
