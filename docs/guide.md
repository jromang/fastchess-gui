# fastchess-gui user guide

The [README](../README.md) explains what fastchess-gui is and how to build it. This page covers the details.

## Language

The interface is in English or French. At startup it follows the first preferred language of the system
(French if that is French, English otherwise). It can be forced in **Setup → PGN and misc → Language**.

## Engines

**Engines → Add…** registers a UCI executable. The engine is started once to read its name, author and options.
Options are edited in a table: modified values are highlighted, and only values that differ from the engine
default are sent with `setoption` at the start of every game. `Threads` and `Hash` come first.

**Duplicate** makes a second entry for the same binary, with other options (a different `Hash`, a skill level,
a different network file…). Entries are named after the UCI `id name` when first added and can be renamed.

*Restart the engine for every game* starts a new process for each game instead of reusing it.

## Setup

- **Engines**: A is the engine under test and B the baseline. Every statistic is given from A's point of view.
- **Match**:
  - *Fixed games*: played in pairs, each opening once with each colour;
  - *Infinite*: until you stop it;
  - *SPRT*: elo0, elo1, alpha, beta, model (`normalized` uses nElo bounds like fishtest and OpenBench,
    `logistic` uses classic Elo, `bayesian` uses BayesElo), and an optional maximum number of games. The
    *Gainer [0, 3]* and *Non-regression [-5, 0]* presets fill in the bounds.
- **Time control**: base time and increment in seconds, optional moves per period, and a margin before a game is
  lost on time; or a fixed number of nodes per move.
- **Concurrency**: the number of games played at the same time. In automatic mode it is measured at startup:
  - fixed nodes: one game per hardware thread, since Hyper-Threading does not change a node count;
  - with a clock: physical cores − 1, so that engines do not slow each other down.

  *CPU affinity* pins every game to one processor, shared by its two engines since they never think at the same
  time: two games keep two cores busy and leave the others idle. fastchess only supports it for single-threaded
  engines, so it is turned off automatically when an engine has `Threads` > 1. A warning appears when
  concurrency × threads exceeds the hardware threads (or, with a clock, the physical cores).
- **Openings**: an EPD or PGN book, random or sequential order, the number of plies kept from a PGN book, the
  starting index and the random seed (0 = a new order for every match).
- **Adjudication**: draw (from a move number, for N consecutive moves, under a score), resign (N consecutive
  moves above a score, optionally both engines agreeing), a maximum number of moves, and Syzygy tablebases.
- **PGN and misc**: event name, extra fields in the move comments (nodes, seldepth, nps, pv), restarting engines
  that crash, the end of match notification and the language.

The setup is locked while a match runs.

## Running a match

**Start** opens a new folder for the match and starts the games.

**Pause** starts no new game pair. Games in progress play to the end, and so does the second game of a pair
whose first game has started: fastchess only counts complete pairs in its pentanomial statistics, so a pause never
throws a result away. **Resume** continues.

**Stop** offers two ways out:

- *Finish the running games, then stop*: pauses, waits for the running games and their pairs, then ends the match.
  Nothing is lost.
- *Stop now*: the running games are interrupted and not counted.

Closing the window during a match asks for confirmation, then stops the engines before quitting. When a match ends
the window flashes and, on Linux, a desktop notification is sent (`notify-send`).

## Windows

| Window | Contents |
|---|---|
| Live | One tab per concurrent game: board with an evaluation bar, clocks, the running search of the side to move (depth/seldepth, score, nodes, nps, PV in SAN), the last search of the other side, the annotated move list and the evaluation curve |
| Games | The finished games, newest first, with filters (wins, draws, losses, losses on time, crashes and illegal moves, adjudications). Selecting one opens a replay: ← → Home End, or click a move or the curve. Export of one game, of all games or of the filtered ones |
| Results | Elo and nElo with their 95 % confidence interval, LOS, score, wins/draws/losses, pentanomial counts, the SPRT bounds and LLR, losses on time and crashes. *Copy summary* puts a fastchess style summary in the clipboard |
| Charts | Elo with its confidence band and LLR (or nElo) against the number of games, the pair distribution, game lengths, how games ended, average depth, NPS and time per move for each engine |
| Engines | The engine list and the UCI option editor |
| Console | fastchess' own output and the messages of the GUI |

Windows can be docked, tabbed and moved; the layout is saved. All numbers come from fastchess' Elo and SPRT
classes, so they are the ones the command line tool would print.

## Files

Everything lives in the profile folder: `~/.local/share/fastchess/fastchess-gui/` on Linux,
`%APPDATA%\fastchess\fastchess-gui\` on Windows.

- `engines.json`: the engine list and their options;
- `setup.json`: the last setup;
- `layout.ini`: the window layout;
- `fastchess-gui.log`: the console of the last session;
- `runs/<date>_<A>_vs_<B>/`, one folder per match:
  - `games.pgn`: move comments in the cutechess / fastchess style, `{+0.35/18 1.234s}`, with the score from the
    point of view of the side that moved;
  - `games-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, with the evaluation from
    White's point of view, as read by ChessBase / Fritz and Lichess;
  - `config.json`: fastchess' tournament state, saved every 20 games.

Both PGN files hold every game of the match; the *Games* window reads them back for the replay and the export.

## Command line

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--quit-after SECONDS]
```

- `--profile DIR` uses another profile folder;
- `--autostart` starts the saved match at launch;
- `--shot` saves a screenshot of the window after the given number of seconds (default 5), with WINDOW brought to
  the front, and quits once every screenshot is taken. It can be repeated. WINDOW is one of `setup`, `live`,
  `games`, `results`, `charts`, `engines`, `console`. The mouse is ignored in this mode;
- `--select-game N` opens game N (in finishing order) in the *Games* window as soon as it exists;
- `--quit-after SECONDS` stops the match and quits.

These options exist to check the rendering without an interactive session, for instance
`fastchess-gui --autostart --shot live.png@live@30`.

## Development notes

- `gui/src` holds the application, `gui/tests` its tests. The code is written in English; every user visible string
  goes through `TR()` or `trf()`, and `gui/src/i18n_table.cpp` holds the French text. A test fails when a `TR()`
  string has no French translation.
- `fastchess-gui-tests` covers the topology detection, UCI option parsing, both PGN comment styles, reading games
  back from their offsets, the pause gate, the statistics, the setup to fastchess configuration conversion,
  the translations, and real tournaments with fastchess' random mover engine (pause, resume, stop, a second run in
  the same process).
- The fastchess hooks are listed in the README. `make` in `app/` still builds and tests the upstream command line
  tool.
