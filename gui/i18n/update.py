#!/usr/bin/env python3
"""Brings the translations up to date with the sources.

    python3 gui/i18n/update.py

Writes messages.pot, every message of gui/src: TR("..."), trf("...", ...), trn("one", "other", n, ...) and N_("...")
for the strings translated through a variable. The ImGui ids ("##id") are not part of a message. Then, in every
<language>.po: messages that are new get an empty translation (shown in English until translated), messages gone
from the sources are removed, the rest is kept. Files are written sorted by message, so diffs stay small.

The tests (gui/tests/i18n_tests.cpp) check that every message has a translation in every language.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCES = os.path.join(HERE, '..', 'src')

STR = r'"((?:[^"\\]|\\.)*)"'
LITS = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'  # "a" "b": clang-format splits long literals


def joined(lits):
    return ''.join(re.findall(STR, lits))


def extract():
    """{msgid: msgid_plural or None}"""
    messages = {}
    for name in sorted(os.listdir(SOURCES)):
        if not name.endswith('.cpp'):
            continue
        text = open(os.path.join(SOURCES, name), encoding='utf-8').read()
        for m in re.finditer(r'\b(TR|trf|N_)\(\s*' + LITS, text):
            messages.setdefault(joined(m.group(2)).split('##')[0], None)
        for m in re.finditer(r'\btrn\(\s*' + LITS + r',\s*' + LITS, text):
            messages[joined(m.group(1))] = joined(m.group(2))
    messages.pop('', None)
    return messages


def parse(path):
    """(comment lines, header msgstr, {msgid: [msgstr...]})"""
    text = open(path, encoding='utf-8').read()
    comments, header, entries = [], '', {}
    for block in re.split(r'\n\s*\n', text):
        def field(name):
            m = re.search(r'^' + re.escape(name) + r' ' + LITS, block, re.M)
            return None if m is None else joined(m.group(1))
        msgid = field('msgid')
        if msgid is None:
            continue
        if msgid == '':
            comments = [l for l in block.splitlines() if l.startswith('#')]
            header = field('msgstr') or ''
            continue
        if field('msgid_plural') is None:
            entries[msgid] = [field('msgstr') or '']
        else:
            forms = []
            while (form := field(f'msgstr[{len(forms)}]')) is not None:
                forms.append(form)
            entries[msgid] = forms
    return comments, header, entries


def write(path, comments, header, messages, entries, nplurals):
    out = comments + ['msgid ""', 'msgstr ""']
    out += ['"' + line + '\\n"' for line in header.split('\\n') if line]
    for msgid in sorted(messages, key=lambda m: m.encode()):
        plural = messages[msgid]
        old = entries.get(msgid, [])
        out.append('')
        out.append(f'msgid "{msgid}"')
        if plural is None:
            out.append(f'msgstr "{old[0] if len(old) == 1 else ""}"')
        else:
            out.append(f'msgid_plural "{plural}"')
            for i in range(nplurals):
                out.append(f'msgstr[{i}] "{old[i] if len(old) == nplurals else ""}"')
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')


def main():
    messages = extract()
    pot = os.path.join(HERE, 'messages.pot')
    comments, header, _ = parse(pot)
    write(pot, comments, header, messages, {}, 2)

    for name in sorted(os.listdir(HERE)):
        if not name.endswith('.po'):
            continue
        path = os.path.join(HERE, name)
        comments, header, entries = parse(path)
        m = re.search(r'nplurals\s*=\s*(\d+)', header)
        nplurals = int(m.group(1)) if m else 2
        write(path, comments, header, messages, entries, nplurals)
        missing = [k for k in messages if not any(entries.get(k, ['']))]
        gone = [k for k in entries if k not in messages]
        print(f'{name}: {len(messages)} messages, {len(missing)} to translate, {len(gone)} removed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
