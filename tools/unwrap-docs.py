#!/usr/bin/env python3
"""Join hard-wrapped prose lines in docs/**/*.md into single-line paragraphs."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "docs"

HEADING_RE = re.compile(r"^#{1,6}\s")
ORDERED_LIST_RE = re.compile(r"^\d+\.\s")
UNORDERED_LIST_RE = re.compile(r"^[-*+]\s")
TABLE_RE = re.compile(r"^\|")
NAV_FOOTER_RE = re.compile(r"\b(?:Next|Prev|Previous):\s*\[")
HOME_NAV_RE = re.compile(r"^\[Home\]\(")
BOLD_LABEL_RE = re.compile(r"^\*\*[^*]+\*\*")


def line_kind(line: str, in_code: bool) -> str:
    if in_code:
        return "code"
    stripped = line.strip()
    if not stripped:
        return "blank"
    if line.startswith("```"):
        return "fence"
    if stripped == "---":
        return "hr"
    if HEADING_RE.match(line):
        return "heading"
    if TABLE_RE.match(stripped):
        return "table"
    if ORDERED_LIST_RE.match(stripped) or UNORDERED_LIST_RE.match(stripped):
        return "list"
    if NAV_FOOTER_RE.search(stripped) or HOME_NAV_RE.match(stripped):
        return "nav"
    if stripped.startswith(">"):
        return "blockquote"
    return "prose"


def is_wrapped_continuation(line: str, prev_kind: str) -> bool:
    if not line.strip():
        return False
    if line_kind(line, False) != "prose":
        return False
    if prev_kind in ("prose", "list", "blockquote"):
        return True
    return False


def join_lines(lines: list[str]) -> str:
    parts: list[str] = []
    for line in lines:
        text = line.strip()
        if not parts:
            parts.append(text)
        else:
            parts.append(text)
    return " ".join(parts)


def unwrap_file(text: str) -> str:
    raw_lines = text.splitlines()
    out: list[str] = []
    in_code = False
    in_front_matter = False
    front_matter_done = False
    i = 0

    while i < len(raw_lines):
        line = raw_lines[i]

        if not front_matter_done:
            out.append(line)
            if line.strip() == "---":
                if not in_front_matter:
                    in_front_matter = True
                else:
                    front_matter_done = True
            i += 1
            continue

        if line.startswith("```"):
            in_code = not in_code
            out.append(line)
            i += 1
            continue

        if in_code:
            out.append(line)
            i += 1
            continue

        kind = line_kind(line, False)

        if kind in ("blank", "heading", "hr", "table", "nav", "fence"):
            out.append(line)
            i += 1
            continue

        if kind == "list":
            block = [line]
            i += 1
            while i < len(raw_lines):
                nxt = raw_lines[i]
                if line_kind(nxt, False) == "blank":
                    break
                if is_wrapped_continuation(nxt, "list"):
                    block.append(nxt)
                    i += 1
                    continue
                break
            out.append(join_lines(block))
            continue

        if kind == "blockquote":
            block = [line]
            i += 1
            while i < len(raw_lines):
                nxt = raw_lines[i]
                if line_kind(nxt, False) == "blank":
                    break
                if nxt.strip().startswith(">") or is_wrapped_continuation(nxt, "blockquote"):
                    block.append(nxt)
                    i += 1
                    continue
                break
            out.append(join_lines(block))
            continue

        # prose paragraph
        block = [line]
        i += 1
        while i < len(raw_lines):
            nxt = raw_lines[i]
            if line_kind(nxt, False) == "blank":
                break
            if is_wrapped_continuation(nxt, "prose"):
                block.append(nxt)
                i += 1
                continue
            break
        out.append(join_lines(block))

    result = "\n".join(out)
    if text.endswith("\n"):
        result += "\n"
    return result


def main() -> int:
    docs_dir = ROOT
    if len(sys.argv) > 1:
        docs_dir = Path(sys.argv[1])

    changed = 0
    for path in sorted(docs_dir.rglob("*.md")):
        original = path.read_text(encoding="utf-8")
        updated = unwrap_file(original)
        if updated != original:
            path.write_text(updated, encoding="utf-8", newline="\n")
            changed += 1
            print(path.relative_to(docs_dir.parent))

    print(f"Updated {changed} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
