# Help in fastchess-gui

How a user finds out what something does, without leaving what they are doing. It follows the rules of #161, rule 9
first: help at the cursor.

## The layers

From the closest to the cursor to the furthest:

1. **Tooltips.** Every control whose effect is not plain from its label has one, saying what it does and, where
   another program names it differently, that name too (rule 10). The long grey help lines are tooltips.
2. **F1 at the cursor.** F1 opens the guide at the section of what is under the mouse: a row of the setup, a card of
   the home page, a tab, a column of the standings. Without a topic under the mouse, the section of the panel last
   clicked, then of the page shown. A tooltip whose control has a topic ends with a muted line, *F1: more in the
   guide*. The round `?` beside a page title opens the page's section.
3. **The guide panel.** The guide is in the program, in the language of the interface, for the version that runs,
   without a network. It opens at the right of the page (not a floating window, rule 11), which keeps working beside
   it: the reader sees the setting and its explanation together.
4. **The command palette.** Ctrl+K finds the guide's sections by their titles (*Guide: Adjudication*), and *Search
   the guide for "…"* runs the guide's search with what was typed.
5. **Messages.** A problem the program reports (an engine that cannot start, tablebase files missing, the memory)
   has its fix in place (rule 7) and, where the guide explains more, a `?` to its section of *Troubleshooting*.
6. **Getting started.** A profile without engines shows the steps on the home page: add engines, set up a
   tournament, start it, each with its button, and the guide's *Quick start*.
7. **Reporting a problem.** *Report a problem…* (Settings, Log, the palette, a failure at startup) opens a GitHub issue
   filled with the version, the system and the log.

## The guide

One text, `docs/guide.md`, in English, read on GitHub and in the program. Its translations are
`docs/guide.<code>.md`, one per language of the interface (`fr`, `de`, `es`, `it`, `pt_BR`, `ru`, `zh_CN`, the codes
of `gui/i18n`). All are embedded in the program at build time and shipped beside it in the release archives.

It is a reference, so every section says exactly what the program does, plus four parts a reference lacks:

- **Quick start**: a first tournament in a few steps, then a test of a new version of an engine;
- **Keyboard shortcuts**: every key, in one table;
- **Tips** at the end of the setup's sections: which value to choose, and why;
- **Troubleshooting** (symptom, cause, fix) and a **Glossary** of the testers' words (Elo, nElo, LOS, LLR, SPRT,
  pentanomial, TF…).

What it no longer holds: the history of earlier versions (the release notes have it) and the notes for developers
(`docs/design/development.md`).

### Sections and anchors

A section is a heading (`##`, `###` or `####`) and what follows it up to the next heading. Its **id** is the GitHub
anchor of its English heading (`new-tournament`), so the English titles are unique (a test checks it), and renaming
one changes its id (the program's topics are checked against the ids, see *Tests*).

A translated file has the same sections in the same order, each heading followed by a comment that ties it to the
English one and to the English text it was translated from:

```markdown
## Nouveau tournoi
<!-- en: new-tournament 5f3a9c21 -->
```

`5f3a9c21` is the FNV-1a hash (32 bits, hexadecimal) of the English section: its heading line and its text, each
line without trailing spaces, without the blank lines at both ends. When the English section changes, its hash no
longer matches: the translation is stale. GitHub shows nothing of the comment.

`tools/guide_sync.py` lists, for each language, the sections missing, stale or out of order, and `--stamp` writes the
current hashes into a translation once it was brought up to date (or after an English change that needs no new
translation, such as a typo).

Rules for translations: the same sections, the same inline code spans and code blocks (commands, file names and
option names are not translated), the labels of the interface as its `.po` file translates them, the register of the
interface (French *tu*, German *du*, Spanish *tú*, Italian *tu*, Brazilian Portuguese *você*, Russian *вы*), and
internal links to the translated heading's anchor in the same file.

### Showing a translation

The panel shows the guide of the interface's language. A section that is missing or stale in it is shown in English,
under a muted line saying so: an outdated explanation is worse than one in another language. The tests make sure a
release has none.

## The panel

At the right of every page, the whole height under the title bar, as wide as the user drags its left edge (kept in
`settings.json`), at most 60 % of the window.

- **Header**: back and forward (Alt+Left, Alt+Right, the mouse's side buttons), the contents (the sections as a tree,
  the current one marked), the search field (Ctrl+F while the panel has the keyboard), *Open in the browser* (GitHub,
  at the version that runs and the same section, in the same language) and close (F1 again, or Escape in the panel).
- **Body**: one `##` section with its subsections, scrolled to the section asked for, which is briefly highlighted.
  Headings, paragraphs, bold, italics, code, lists, tables, code blocks with a *Copy* button, pictures, and links: to
  a section (opened in the panel) or a web page (opened in the browser, its address at the cursor). *Previous* and
  *Next* at the bottom.
- **Search**: every word typed, in any order, letter case and accents aside, in the current language (and English
  when a section is shown in English); the sections found with the text around the match, the best first (in a title
  before in the text). A click opens the section at the first match, every match highlighted.

The panel never takes the keyboard from a text field it did not open, and closing it gives the page its width back.

## Rendering

The Markdown is parsed with md4c (C, MIT) into a small model: blocks (headings, paragraphs, list items with their
depth, tables, code blocks, pictures, rules) of inline runs (text, bold, italic, code, link, key). The layout breaks
lines between words, and between any two characters of Chinese or Japanese text, which has no spaces. Fonts: Inter for
the text, Inter SemiBold for bold and headings, Inter Italic for italics, ImGui's scalable default font (monospaced) for
code blocks. Each block's layout is kept for the width it was made at, and only the blocks in sight are drawn.

## Topics

A topic is the id of a section. `guide::Topic` names those the program opens (`topics.hpp`), and a test checks each
is a section of the English guide. A view marks what its topic covers:

```cpp
ui::topic(guide::topics::Adjudication);           // the last item: hovered, it is the topic under the cursor
ui::TopicRegion region(guide::topics::Openings);  // a region: the mouse inside it, until the end of the scope
```

The innermost topic under the mouse wins. F1 reads it at the start of the next frame.

## Tests

- every topic of `topics.hpp` is a section of the English guide; the English headings are unique;
- every translation has every English section, in order, with the hash of the current English text, the same inline
  code spans and code blocks, and internal links that reach a heading of the same file;
- the parser: headings and their ids, the stamps, the blocks and runs of a sample;
- the layout: words wrapped at the width, Chinese broken between characters, a word longer than the line cut;
- the search: words in any order, accents and case aside, titles first.
