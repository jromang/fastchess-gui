# Development notes

- `tournament/src/tour` is the tournament library (`fcg_tournament`): model, schedule, Swiss and knockout
  pairings, journal, state, the runner that drives fastchess, standings and ratings. It has no GUI dependency and
  its own tests, `fcg-tournament-tests`. [tournament.md](tournament.md) describes it.
- `gui/src` holds the application, `gui/tests` its tests (`fastchess-gui-tests`, including real tournaments with
  fastchess' random mover engine). The code is written in English; every user visible string goes through `TR()`
  or `trf()`, `trn()` when it holds a count (`trn("{} game", "{} games", n, n)`: the right plural in every
  language), and `N_()` marks the strings translated later through a variable. The translations are the `.po`
  files of `gui/i18n`, embedded in the program and read with tinygettext. `python3 gui/i18n/update.py` writes
  `messages.pot` from the sources and adds new messages to every `.po` file (removing those gone). A test fails
  when a message has no translation in a language, when a translation does not keep the `{}` and `%d`
  placeholders of the English text, or when a plural message lacks one of its language's forms.
- The user guide is `docs/guide.md` and its translations `docs/guide.<code>.md`, embedded in the program too.
  `tools/guide_sync.py` lists the translated sections missing or older than the English text, and `--stamp` marks
  them up to date once translated; a test fails while one is. [help.md](help.md) describes the guide and the help.
- The fastchess hooks are listed in the README. `make` in `app/` still builds and tests the upstream command line
  tool.
