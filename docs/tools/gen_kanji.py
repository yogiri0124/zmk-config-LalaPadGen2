#!/usr/bin/env python3
"""漢字の表（docs/google-ime/kanji.tsv）から、次の 2 つを作る。

- lalapad/include/dt-bindings/kana/kanji.h
    keymap で使う名前（`#define KJ_HI 1` など）。`&kanji_key KJ_HI` のように書く。
- docs/google-ime/romantable.txt の漢字の行
    符号（「;」＋英字 2 文字）を漢字に変える、Google 日本語入力のローマ字テーブルの行。
    ほかの行（かな・記号）はそのまま残し、漢字の行（「;」＋英字 2 文字の行）だけを入れ替えて末尾に並べる。

使い方（リポジトリのどこからでも）:
    python docs/tools/gen_kanji.py          # 作る（書き換える）
    python docs/tools/gen_kanji.py --check  # 作ったものが表と合っているかだけ確かめる（合っていなければ終了コード 1）

表を変えたら、このスクリプトを実行し、romantable.txt を Google 日本語入力に読み込み直す。
kanji.h が変わったとき（名前を足した・変えた・消したとき）は、ファームウェアのビルドも要る。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "docs" / "google-ime" / "kanji.tsv"
HEADER = ROOT / "lalapad" / "include" / "dt-bindings" / "kana" / "kanji.h"
ROMANTABLE = ROOT / "docs" / "google-ime" / "romantable.txt"

# lalapad/src/behavior_kanji.c と合わせる（符号は「;」＋英字 2 文字。番号 0 は使わない）
PREFIX = ";"
LETTERS = "abcdefghijklmnopqrstuvwxyz"
MAX_ID = len(LETTERS) * len(LETTERS) - 1

NAME_RE = re.compile(r"^KJ_[A-Z0-9_]+$")
CODE_LINE_RE = re.compile(r"^;[a-z]{2}\t")


def code_of(kanji_id: int) -> str:
    return PREFIX + LETTERS[kanji_id // len(LETTERS)] + LETTERS[kanji_id % len(LETTERS)]


def read_table() -> list[tuple[int, str, str, str]]:
    """表を読む。戻り値は (番号, 名前, 出力, メモ) の番号順の一覧。誤りがあれば止める"""
    errors: list[str] = []
    rows: list[tuple[int, str, str, str]] = []
    seen_ids: dict[int, int] = {}
    seen_names: dict[str, int] = {}
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
        memo = "\t".join(cols[3:]).strip()
        if not id_text.isdigit() or not 1 <= int(id_text) <= MAX_ID:
            errors.append(f"{lineno} 行目: 番号「{id_text}」は 1〜{MAX_ID} の整数にする")
            continue
        kanji_id = int(id_text)
        if not NAME_RE.match(name):
            errors.append(f"{lineno} 行目: 名前「{name}」は KJ_ で始まる英大文字・数字・_ にする")
        if output.strip() != output or output == "":
            errors.append(f"{lineno} 行目: 出力が空か、前後に空白がある")
        if kanji_id in seen_ids:
            errors.append(f"{lineno} 行目: 番号 {kanji_id} は {seen_ids[kanji_id]} 行目と同じ")
        if name in seen_names:
            errors.append(f"{lineno} 行目: 名前 {name} は {seen_names[name]} 行目と同じ")
        seen_ids.setdefault(kanji_id, lineno)
        seen_names.setdefault(name, lineno)
        rows.append((kanji_id, name, output, memo))
    if errors:
        sys.exit(f"{TABLE.relative_to(ROOT)} に誤りがある:\n" + "\n".join(errors))
    return sorted(rows)


def make_header(rows: list[tuple[int, str, str, str]]) -> str:
    lines = [
        "/*",
        " * 漢字入力 (&kanji_key) の漢字の番号。",
        " * このファイルは docs/tools/gen_kanji.py が docs/google-ime/kanji.tsv から作る。直接書き換えないこと。",
        " * keymap では `&kanji_key KJ_…` のように使う。どの番号がどの漢字になるかは、PC 側のローマ字テーブル",
        " * （docs/google-ime/romantable.txt）で決まる（キーボードは番号の符号「;」＋英字 2 文字を送るだけ）。",
        " */",
        "",
        "#pragma once",
        "",
        "/* 番号の上限（符号が英字 2 文字なので 26 × 26 - 1。0 は使わない） */",
        f"#define KJ_MAX_ID {MAX_ID}",
    ]
    if rows:
        lines.append("")
        for kanji_id, name, output, memo in rows:
            comment = f"{output}（{code_of(kanji_id)}）" + (f" {memo}" if memo else "")
            lines.append(f"#define {name} {kanji_id} /* {comment} */")
    return "\n".join(lines) + "\n"


def make_romantable(rows: list[tuple[int, str, str, str]]) -> str:
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
    kept += [f"{code_of(kanji_id)}\t{output}" for kanji_id, _, output, _ in rows]
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
        print(f"表と合っている（漢字 {len(rows)} 個）")
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    sys.exit(main())
