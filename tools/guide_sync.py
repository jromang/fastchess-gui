#!/usr/bin/env python3
"""The guide's translations against the English text (docs/design/help.md, "Sections and anchors").

    tools/guide_sync.py                  every language: what is missing, stale or out of order (exit 1 if anything)
    tools/guide_sync.py fr de            those languages only
    tools/guide_sync.py --stamp fr       write the current English hashes into docs/guide.fr.md, every section
    tools/guide_sync.py --stamp fr ID... only those sections (after bringing them up to date)
    tools/guide_sync.py --sections       the English sections: level, id, hash

A translated section follows its heading with `<!-- en: <id> <hash> -->`: the id of the English section (the GitHub
anchor of its heading) and the FNV-1a hash of the English text it was translated from. The program's test
(guide_tests.cpp) checks the same rules on the guides embedded in it, with the same slug and hash.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
LANGUAGES = ["fr", "de", "es", "it", "pt_BR", "ru", "zh_CN"]

STAMP = re.compile(r"^<!-- en: ([A-Za-z0-9_\-\u0080-\U0010ffff]+) ([0-9a-f]{8}) -->$")
HEADING = re.compile(r"^(#{1,6}) +(.*?)(?: +#+)? *$")
LINK = re.compile(r"\]\(#([^)\s]+)\)")
CODE_SPAN = re.compile(r"(`+)(.+?)\1")


def lower(cp: int) -> int:
    """Lower case as guide::slug() does it: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic."""
    if 0x41 <= cp <= 0x5A:
        return cp + 0x20
    if 0xC0 <= cp <= 0xDE and cp != 0xD7:
        return cp + 0x20
    if 0x100 <= cp <= 0x137 or 0x14A <= cp <= 0x177:
        return cp + 1 if cp % 2 == 0 else cp
    if 0x139 <= cp <= 0x148 or 0x179 <= cp <= 0x17E:
        return cp + 1 if cp % 2 == 1 else cp
    if cp == 0x178:
        return 0xFF
    if 0x391 <= cp <= 0x3A9 and cp != 0x3A2:
        return cp + 0x20
    if 0x410 <= cp <= 0x42F:
        return cp + 0x20
    if 0x400 <= cp <= 0x40F:
        return cp + 0x50
    return cp


def dropped(cp: int) -> bool:
    """Punctuation and symbols GitHub leaves out of an anchor (outside ASCII)."""
    return (
        0xA0 <= cp <= 0xBF
        or cp in (0xD7, 0xF7)
        or 0x2000 <= cp <= 0x206F
        or 0x20A0 <= cp <= 0x20CF
        or 0x2190 <= cp <= 0x23FF
        or 0x2500 <= cp <= 0x27BF
        or 0x3000 <= cp <= 0x303F
        or 0xFF01 <= cp <= 0xFF0F
        or 0xFF1A <= cp <= 0xFF20
        or 0xFF3B <= cp <= 0xFF40
        or 0xFF5B <= cp <= 0xFF65
    )


def slug(heading: str) -> str:
    out = []
    for ch in heading:
        cp = ord(ch)
        if cp < 0x80:
            if ch.isascii() and (ch.isalnum() or ch in "-_"):
                out.append(chr(lower(cp)))
            elif ch == " ":
                out.append("-")
        elif not dropped(cp):
            out.append(chr(lower(cp)))
    return "".join(out)


def fnv1a(text: str) -> str:
    h = 0x811C9DC5
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


class Section:
    def __init__(self, level, title, line_no, heading_line):
        self.level = level
        self.title = title
        self.line_no = line_no
        self.heading_line = heading_line
        self.anchor = ""
        self.stamp = None  # (id, hash)
        self.body = []

    def text(self) -> str:
        lines = [l.rstrip() for l in self.body if not STAMP.match(l.strip())]
        while lines and not lines[0]:
            lines.pop(0)
        while lines and not lines[-1]:
            lines.pop()
        return "\n".join([self.heading_line.rstrip()] + lines)

    def hash(self) -> str:
        return fnv1a(self.text())

    def code(self):
        """The inline code spans and the code blocks, sorted: what a translation keeps as it is."""
        spans, blocks, block, fenced = [], [], [], False
        for line in self.body:
            if line.startswith("```") or line.startswith("~~~"):
                if fenced:
                    blocks.append("\n".join(block))
                    block = []
                fenced = not fenced
                continue
            if fenced:
                block.append(line.rstrip())
            else:
                spans += [m.group(2).strip() for m in CODE_SPAN.finditer(line)]
        return sorted(spans), blocks

    def links(self):
        return [m.group(1) for m in LINK.finditer("\n".join(l for l in self.body))]


def parse(path: Path):
    sections, current, fenced = [], None, False
    seen = {}
    for no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.startswith("```") or line.startswith("~~~"):
            fenced = not fenced
        m = None if fenced else HEADING.match(line)
        if m and not line.startswith("```"):
            current = Section(len(m.group(1)), m.group(2).strip(), no, line)
            anchor = slug(current.title)
            n = seen.get(anchor, 0)
            seen[anchor] = n + 1
            current.anchor = anchor if n == 0 else f"{anchor}-{n}"
            sections.append(current)
            continue
        if current is None:
            continue
        if not current.body and current.stamp is None:
            s = STAMP.match(line.strip())
            if s:
                current.stamp = (s.group(1), s.group(2))
                continue
        current.body.append(line)
    return sections


def check_english(english):
    problems = []
    anchors = [s.anchor for s in english]
    for s in english:
        if s.anchor != slug(s.title):
            problems.append(f"guide.md:{s.line_no}: heading '{s.title}' is not unique")
    known = set(anchors)
    for s in english:
        for target in s.links():
            if target not in known:
                problems.append(f"guide.md:{s.line_no}: link to #{target}, which is no heading")
    return problems


def check_language(code, english):
    path = DOCS / f"guide.{code}.md"
    if not path.exists():
        return [f"guide.{code}.md: missing"]
    problems = []
    translated = parse(path)
    by_id = {}
    for s in translated:
        if s.level == 1:
            continue
        if s.stamp is None:
            problems.append(f"guide.{code}.md:{s.line_no}: heading '{s.title}' has no <!-- en: … --> line")
            continue
        by_id[s.stamp[0]] = s
    english_ids = [s.anchor for s in english if s.level > 1]
    known = set(english_ids)
    for s in by_id.values():
        if s.stamp[0] not in known:
            problems.append(f"guide.{code}.md:{s.line_no}: '{s.stamp[0]}' is no English section")
    order = [s.stamp[0] for s in translated if s.stamp and s.stamp[0] in known]
    expected = [i for i in english_ids if i in by_id]
    if order != expected:
        for a, b in zip(order, expected):
            if a != b:
                problems.append(f"guide.{code}.md: sections out of order, '{a}' where '{b}' was expected")
                break
    anchors = {s.anchor for s in translated}
    for e in english:
        if e.level == 1:
            continue
        t = by_id.get(e.anchor)
        if t is None:
            problems.append(f"guide.{code}.md: section '{e.anchor}' missing")
            continue
        where = f"guide.{code}.md:{t.line_no}"
        if t.stamp[1] != e.hash():
            problems.append(f"{where}: '{e.anchor}' is stale (translated from {t.stamp[1]}, English is {e.hash()})")
        if t.level != e.level:
            problems.append(f"{where}: '{e.anchor}' is a level {t.level} heading, level {e.level} in English")
        if t.code() != e.code():
            es, eb = e.code()
            ts, tb = t.code()
            if es != ts:
                extra = sorted(set(ts) - set(es))[:3]
                gone = sorted(set(es) - set(ts))[:3]
                problems.append(f"{where}: '{e.anchor}': code spans differ (not in English: {extra}; missing: {gone})")
            if eb != tb:
                problems.append(f"{where}: '{e.anchor}': code blocks differ from the English ones")
        for target in t.links():
            if target not in anchors:
                problems.append(f"{where}: link to #{target}, which is no heading of guide.{code}.md")
    return problems


def stamp(code, english, ids):
    path = DOCS / f"guide.{code}.md"
    hashes = {s.anchor: s.hash() for s in english}
    out, changed = [], 0
    for line in path.read_text(encoding="utf-8").splitlines():
        m = STAMP.match(line.strip())
        if m and m.group(1) in hashes and (not ids or m.group(1) in ids):
            new = f"<!-- en: {m.group(1)} {hashes[m.group(1)]} -->"
            changed += new != line
            line = new
        out.append(line)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"guide.{code}.md: {changed} stamp(s) updated")


def main(argv):
    english = parse(DOCS / "guide.md")
    if argv and argv[0] == "--sections":
        for s in english:
            print(f"{'#' * s.level} {s.anchor} {s.hash()}")
        return 0
    if argv and argv[0] == "--stamp":
        if len(argv) < 2 or argv[1] not in LANGUAGES:
            print("--stamp LANGUAGE [ID...]", file=sys.stderr)
            return 2
        stamp(argv[1], english, set(argv[2:]))
        return 0
    languages = [a for a in argv if a in LANGUAGES] or LANGUAGES
    problems = check_english(english)
    for code in languages:
        problems += check_language(code, english)
    for p in problems:
        print(p)
    if not problems:
        print("The guide and its translations agree.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
