# fastchess-gui user guide

The [README](../README.md) explains what fastchess-gui is and how to build it. This page covers the details.

## The window

A navigation rail on the left leads to six pages:

| Page | Contents |
|---|---|
| Tournaments | The home page: the running tournament, the queue, every tournament of the profile (resume, open, relaunch), *New tournament* |
| Engines | The engine library: options, health check, groups, reference Elo, import of other programs' lists |
| Books | The opening book library, and the openings of each book with what is wrong with them |
| Ratings | One rating list over every game of every tournament |
| Log | fastchess' own output and the messages of the GUI |
| Settings | Language, theme (automatic, dark, light), end of tournament notification, the tournaments folder |

The interface is in English, French or German. At startup it follows the first preferred language of the system
(French or German if it is one of them, English otherwise); it can be forced in **Settings**. The theme is light
unless dark, or the system's choice, is picked in **Settings**.

## Engines

**Add…** registers a UCI executable. The engine is started once to read its name, author and options. Options
are edited in a table: modified values are highlighted, and only values that differ from the engine default are
sent with `setoption` at the start of every game. `Threads` and `Hash` come first.

- **Duplicate** makes a second entry for the same binary, with other options (a different `Hash`, a skill level,
  a different network file…). Entries are named after the UCI `id name` when first added and can be renamed.
- **Check** starts the engine, measures how fast it answers `uci` and `isready`, and has it play one move. The
  result (✓ or the error) stays next to the engine with its date.
- **Quick test** plays a short match against the engine itself or another one: 10 games at 10+0.1 from 5 positions
  (one with an en passant capture), through the same code as a tournament, shown in its workspace while it plays.
  The result stays next to the engine: its score, its losses on time, crashes and illegal moves, and the share of
  its clock it used, which tells an engine that plays far too fast or too close to the flag. Only the last quick test
  is kept, in `tests/` in the profile (with the engine logs of its games that ended badly).
- **Groups**: free labels separated by commas (`pool A`, `CCRL 40/15`…). The list can be filtered by group or
  searched, and a group can be added to a tournament in one click.
- **Reference Elo**: the engine's rating on a list you trust. A tournament or the rating list can be anchored on it.
- *Restart the engine for every game* starts a new process for each game instead of reusing it.
- **Moved or rebuilt programs**: the library remembers each program (size, date, CRC32). A program that is no
  longer there is marked **!** with *Locate…*, and a tournament does not start with it. A program that changed
  (a new build) has its options read again automatically. Each tournament also records the programs it was
  started with: resuming it with a program rebuilt or missing since is reported in the *Log*.

**Import…** reads the engine list of another program: ChessGUI (`ChsGUI_Eng.TXT`, with the options of
`ChsGUI_Cfg.ini` next to it), cutechess and Banksia (`engines.json`), Arena and other INI style lists, or every
program found in a folder. Engines already in the library (same program, arguments and options) are skipped;
the options of the others are read; WinBoard engines are imported too.

**WinBoard engines** (xboard protocol, CECP) are played like the others. The *Protocol* of an engine is found
out when it is added (*Automatic*), or chosen: UCI or WinBoard. fastchess only speaks UCI: a WinBoard engine is
started through a translation built into fastchess-gui (`fastchess-gui --wb2uci ENGINE [ARGS]`, which fastchess
starts in place of the engine), and its options, `memory`, `cores` and Syzygy path become UCI options. It drives
the engine the way WinBoard does: protocol 2 features, or protocol 1 for engines that send none; positions with
`setboard`, or edit mode; moves in coordinates or SAN; `level`, `time`/`otim`, `st`, `sd`, `nps`; thinking
output, resignation. Engines marked ↔ in the list are WinBoard engines.

- An engine of protocol 1 without `setboard` nor edit mode (TSCP, for instance) can only start from the initial
  position: give it openings as moves (a PGN book), not as positions (EPD).
- Chess960 for an engine with the `fischerandom` variant: `variant fischerandom`, positions in X-FEN, castling as
  `O-O`/`O-O-O`, as WinBoard sends them.
- `FCG_WB2UCI_LOG=FILE` in the environment writes every line exchanged with WinBoard engines to `FILE`.

## Books

**Add…** registers an opening book (EPD, PGN or Polyglot `.bin`) under a name, with a description; for a PGN
book the number of plies kept, for a Polyglot book the depth its lines are followed to. The setup's *Openings*
section then offers the books of the library by name, with their number of openings (any other file can still
be chosen). *Remove* takes a book out of the library and leaves the file alone.

The book chosen is read in the background, with fastchess' own readers (so its openings are exactly those a
tournament plays), and checked:

- **illegal positions**: not one king of each colour, pawns on the first or last rank, the side not to move in
  check;
- **duplicates**: the same position as an earlier opening, by transposition included;
- **colours reversed**: an earlier opening with the colours swapped (a book meant to be played twice, colours
  reversed, does not need both);
- **games already over**: no legal move, or not enough material to mate.

Its openings are listed below, every one or only those with a problem, searched by part of a FEN or by moves (a
PGN book shows its moves in SAN); a click shows the position on a board. *Use it for the next tournament* puts it
in the setup (with Chess960 for a Chess960 book). *Save a cleaned copy…* writes an EPD book without the illegal
positions, the duplicates and the finished games (and, if ticked, the positions with colours reversed), and adds
it to the library.

## New tournament

**Tournaments → New tournament** opens the setup, with a live summary on the right (number of games, games per
engine, estimated duration, openings, common settings, time odds). The setup of the last tournament is kept.

- **Format**:
  - *Match*: two engines, A (the engine under test; every statistic is from its point of view) and B (the
    baseline). A fixed number of games, until you stop it, or an SPRT: elo0, elo1, alpha, beta, model
    (`normalized` uses nElo bounds like fishtest and OpenBench, `logistic` classic Elo, `bayesian` BayesElo) and an
    optional maximum number of games. The *Gainer [0, 3]* and *Non-regression [-5, 0]* presets fill in the bounds.
    This is the quickest way to compare two versions of an engine, and nothing else is needed.
  - *Round robin*: every engine plays every other one, *Cycles* times, each time two games with colours reversed.
    The rounds follow Berger tables, so every engine plays once per round.
  - *Gauntlet*: the first *Seed* engines play every other one, the others do not play each other.
  - *Swiss*: a number of *Rounds*. Each round is two games, colours reversed, against an engine with a similar
    score; nobody meets the same opponent twice when it can be avoided. With an odd number of engines, the lowest
    ranked engine without a bye yet gets one, worth a point.
  - *Knockout*: engines are seeded in their list order, the top seeds get the byes when the field is not a power of
    two, and the first two seeds can only meet in the final. A match is *Game pairs per match* pairs of games; a
    tied match gets one more pair at a time, up to ten, after which the higher seed goes through.
- **Participants**: engines from the library, in seed order. Each one can have its own `Hash`, `Threads`, fixed
  nodes and time odds (a percentage of the time control). *Common settings* force `Hash` and `Threads` on every
  engine (0 = each engine's own).
- **Time control**: base time and increment in seconds, optional moves per period, and a margin before a game is
  lost on time; or a fixed number of nodes per move.
- **Concurrency**: the number of games played at the same time. In automatic mode it is measured at startup:
  - fixed nodes: one game per hardware thread, since Hyper-Threading does not change a node count;
  - with a clock: physical cores − 1, so that engines do not slow each other down.

  *CPU affinity* (off by default) pins every game to one processor, shared by its two engines since they never
  think at the same time. fastchess only supports it for single-threaded engines, so it is turned off
  automatically when an engine has `Threads` > 1. A warning appears when concurrency × threads exceeds the hardware
  threads (or, with a clock, the physical cores).
- **Openings**: an EPD, PGN or Polyglot (`.bin`) book, random or sequential order, the number of plies kept from a
  PGN book, the starting index and the random seed (0 = a new order for every tournament). Every opening is played
  twice, with colours reversed. A Polyglot book is followed from the initial position to a depth (8 plies unless
  set), leaving out the moves that weigh less than a tenth of the best one of their position; the positions
  reached are written once to `<book>-<N>plies.epd` in the tournament folder and played like an EPD book.
- **Chess960** (Fischer random): without a book, the 960 starting positions in random order (written once to
  `chess960.epd` in the tournaments folder); with a book, its positions. Every engine must play Chess960: UCI
  engines with the `UCI_Chess960` option, WinBoard engines with the `fischerandom` variant. The summary names those
  that do not, and such a tournament does not start.
- **Adjudication**: draw (from a move number, for N consecutive moves, under a score), resign (N consecutive moves
  above a score, optionally both engines agreeing), a maximum number of moves, and Syzygy tablebases.
- **PGN and misc**: event name, extra fields in the move comments (nodes, seldepth, nps, pv), restarting engines
  that crash, keeping the engine log of every game (it is always kept for the games that end badly).

**Templates** save the current setup under a name and load it back later. **Start** creates the tournament and
plays it; **Add to the queue** keeps it for later (see *Queue*).

## Running a tournament

A running or opened tournament has a workspace with a header (name, progress, *Pause*, *Stop*) and tabs:

| Tab | Contents |
|---|---|
| Overview | Match: Elo and nElo with their 95 % confidence interval, LOS, score, pentanomial counts, SPRT bounds and LLR, and the charts (Elo and LLR over time, game lengths, how games ended, depth, NPS and time per move). Tournament: the standings, progress by cycle or round, games per hour and time left, the latest results |
| Standings | Points, games, wins/draws/losses, ratings with their confidence interval, Sonneborn-Berger (Buchholz for a Swiss), byes, and the crosstable. *Copy* as text, or as BBCode for a forum post (with a line giving the format, time control, games and book), *Export HTML…* |
| Bracket | Knockout only: the bracket, round by round, with the score of every match |
| Schedule | Every game pair by cycle or round, with its state; editing (see below) |
| Broadcast | Live files, the TLCS server and the web page (see below) |
| Live | The games in progress, as a mosaic of boards or one game in focus with the clocks, the running search of each engine (depth, score, nodes, nps, PV in SAN), the move list and the evaluation curve. *Detach* opens a board in its own window |
| Games | The finished games with their opening (ECO code and name), filterable by result (wins, draws, losses, losses on time, crashes and illegal moves, adjudications), by what the evaluations tell (the engines disagree by N pawns for 3 moves each in a row, an engine's evaluation swings by N pawns from one of its moves to the next, the loser thought it was N pawns better; the game opens at that moment) and by opening (an ECO code or its beginning, a range such as `B20-B99`, or words of the name), with a replay board (← → Home End, or click a move or the curve) and the export of one, all or the filtered games. *Engine log* opens the log of a game that has one (see *Files*) |
| Openings | Results by starting position: its game pairs, White's score, the draws, and how many pairs White won twice, Black won twice (the opening favours a colour) or one engine won twice (it tells the engines apart), with the average length. Sorted by any column, filtered to the unbalanced, the drawn or the decisive ones; *Export as EPD…* writes the positions shown, to keep them or to clean a book. A click shows the position and its games |

**Processor use**: while games are played, the processor time of every engine's processes (the engine behind a
WinBoard adapter included) is measured at each `go` and each move. The Overview shows, for each engine, the cores it
keeps busy while it thinks and the share of a core it uses while its opponent thinks, and warns when an engine uses
clearly more cores than its `Threads`, or thinks on its opponent's time (pondering it was not asked for, a background
thread): what makes a game unfair to the opponent and to the other games running. It needs 20 s of each to tell, and
counts from the start or the resumption of the tournament. It only warns; it never decides a game.

**Pause** starts no new game pair. Games in progress play to the end, and so does the second game of a pair whose
first game has started, so a pause never throws a result away. **Resume** continues. **Stop** either finishes
the running games first, or stops now (the interrupted games are not counted and will be played again).

**Every tournament resumes exactly where it stopped**, after a stop, a closed window, a crash or a power cut: no
game is lost and none is played twice, and a half-played game pair is completed with the same opening and
colours. *Resume* on the home page continues it, with the statistics of the games already played: results,
and the engines' depth, time per move and NPS read back from the move comments (NPS only when the PGN records
it, *nps* in *PGN and misc*).

### Changing a running tournament

The *Schedule* tab's **Change the tournament** panel works without stopping anything: running games go on and no
played game is lost. Each change shows its impact before it is applied.

- **Add an engine**: it plays the cycles already started (*Catch up*) or joins from the next cycle.
- **Withdraw an engine**: its remaining games are dropped; its games played so far count or not.
- **Replace an engine**: its games stop counting and are played again by the new engine.
- **Number of cycles**: more or fewer.
- **Play games again**: every game of an engine, or the losses on time and crashes.
- Right click on a game: play it again, or void it (no replay).

Voided games stay in the PGN files and the journal; they only stop counting. **Undo the last change** is offered
until the next game starts: nothing has been played under the change yet, so it can be taken back exactly (games
that were already running when it was made are kept).

### Queue and relaunch

The **Queue** on the home page holds tournaments to play one after the other: when one ends, the next starts. They
can be reordered or removed. **Relaunch with…** on a finished tournament opens its setup again, to play it once
more or with an engine replaced by another (typically the next version).

## Ratings

The **Ratings** page computes maximum likelihood ratings (the logistic model, draws as half a point, as Ordo does
by default) over every game of every tournament in the profile, and of PGN files imported. Engines are identified
by their name. The games counted can be narrowed at the top of the page, for every tab:

- a time control, a group of the engine library (the games between its engines), the last 30 or 365 days;
- *Imported games*: the games of the PGN files listed under *PGN files…* (a CCRL or CEGT download, the games of
  another program), read from their headers; their engines are known by the names the PGN gives them;
- *Anchor*: the average at 0, or one engine at a fixed rating (by default the first engine with a reference Elo).

Four tabs:

| Tab | Contents |
|---|---|
| Rating list | The list, with a minimum number of games; *Copy* (as text or BBCode), *Export CSV…*, and *Export for Ordo…*: the games counted as a PGN of headers, which Ordo and BayesElo read (`ordo -p ratings.pgn -o ratings.txt`). A click on an engine opens its card |
| Engine | An engine's card: its rating, its results overall and by time control, its losses on time, crashes and illegal moves (each game opens in its tournament), its tournaments (each opens), its average depth, time per move and NPS read from the move comments, and its rating after each tournament |
| Head to head | Two engines against each other: the score, Elo and LOS, the pentanomial counts of the pairs they played both games of, by time control and by tournament, their games (each opens in its tournament) and *Export the games…* |
| Progress | The rating of the engines ticked after each tournament they played, on a time axis: the versions of an engine side by side |

The *Engines* page leads to an engine's card with *Its results…*.

## Broadcast

The *Broadcast* tab of a running tournament publishes it while it plays; the snapshot is refreshed twice a second.
*Game shown* picks the game sent to TLCS and the web page: automatic, or the one chosen in the *Live* tab.

- **Live files**, written atomically in a folder of your choice, to publish anywhere (FTP, rsync, a website):
  `live.pgn` (the games in progress), `standings.html` (standings and crosstable), `tournament.json` (everything,
  for scripts).
- **TLCS server** (UDP port, 16001 by default): the protocol of Tom's Live Chess Server, so TLCV and other TLCS
  viewers can follow the tournament (board, clocks, PV, crosstable).
- **Web page** (TCP port): a page with the board, clocks, PV and standings, served by fastchess-gui itself, for any
  browser on the network.

## Headless mode

```
fastchess-gui --run TOURNAMENT_FOLDER     # resume a tournament
fastchess-gui --run SETUP.json            # create one from a setup or template file, with the profile's engines
```

No window is opened; progress is printed on the console. **Ctrl+C** stops after the running games and the
tournament can be resumed later, from the command line or the GUI. Exit codes: 0 finished, 1 error while
playing, 2 nothing to run, 3 stopped before the end.

The tournament can be broadcast at the same time, as from the *Broadcast* tab:

```
fastchess-gui --run FOLDER --tlcs 16001             # a TLCS server on UDP port 16001
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
```

The options can be combined. A setup file saved with broadcasting switched on broadcasts the same way without
them. For viewers outside the local network, open the ports in the router: TLCV and node-tlcv (ccrl.live)
connect to the TLCS port.

## Files

Everything lives in the profile folder: `~/.local/share/fastchess/fastchess-gui/` on Linux,
`%APPDATA%\fastchess\fastchess-gui\` on Windows.

- `engines.json`: the engine library;
- `setup.json`: the last setup; `templates/*.json`: the saved templates; `queue.json`: the queue;
- `layout.ini`: the window layout; `fastchess-gui.log`: the log of the last session;
- `settings.json`: the tournaments folder, when another one was chosen in **Settings**;
- `runs/<date>_<A>_vs_<B>/` for a match, `runs/<date>_<name>/` for a tournament (in `runs/` or the chosen
  folder), one folder each, which can be moved, copied, zipped and sent:
  - `tournament.json`: the format, the participants (engines copied with their options) and the conditions;
  - `journal.jsonl`: everything that happened since, one line per event (game pairs started, games finished,
    changes, Swiss and knockout pairings); with the PGN files, it is what a resume reads;
  - `games.pgn`: move comments in the cutechess / fastchess style, `{+0.35/18 1.234s}`, with the score from the
    point of view of the side that moved;
  - `games-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, with the evaluation from
    White's point of view, as read by ChessBase / Fritz and Lichess;
  - `logs/game-N.log`: every line exchanged with both engines during game N (the number shown in the *Games*
    tab), with the time it was sent or read, for the games lost on time, by a crash, a stall or an illegal move
    (or every game, if asked). The first 300 and the last 5000 lines of a game are kept. It is what an engine
    author needs to reproduce a bug. For a WinBoard engine it shows the UCI side; `FCG_WB2UCI_LOG` gives the
    WinBoard side.

Matches played with an earlier version (0.2.x) appear on the home page: *Import them* turns them into finished
tournaments.

## Command line

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT] [--web TCP_PORT]
              [--live-files [DIR]]
```

- `--profile DIR` uses another profile folder;
- `--autostart` starts the saved setup at launch;
- `--shot` saves a screenshot of the window after the given number of seconds (default 5), with WINDOW brought to
  the front, and quits once every screenshot is taken. It can be repeated. WINDOW is a page (`home`, `setup`,
  `engines`, `books`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`,
  `settings`) or a tab (`live`, `games`, `openings`, `standings`, `schedule`,
  `bracket`, `broadcast`). The mouse is ignored in this mode and, on Linux, the window is rendered off screen (no window
  appears; set `SDL_VIDEODRIVER` to override);
- `--focus WINDOW` opens that page or tab at launch;
- `--select-game N` opens game N (in finishing order) in the *Games* tab as soon as it exists;
- `--quit-after SECONDS` stops the tournament and quits;
- `--run`: see *Headless mode*.

## Development notes

- `tournament/src/tour` is the tournament library (`fcg_tournament`): model, schedule, Swiss and knockout
  pairings, journal, state, the runner that drives fastchess, standings and ratings. It has no GUI dependency and
  its own tests, `fcg-tournament-tests`. [docs/design/tournament.md](design/tournament.md) describes it.
- `gui/src` holds the application, `gui/tests` its tests (`fastchess-gui-tests`, including real tournaments with
  fastchess' random mover engine). The code is written in English; every user visible string goes through `TR()`
  or `trf()`; `gui/src/i18n_table.cpp` holds the French text and `gui/src/i18n_table_de.cpp` the German. A test
  fails when a `TR()` string has no French or German translation, or when a translation does not keep the `{}` and
  `%d` placeholders of the English text.
- The fastchess hooks are listed in the README. `make` in `app/` still builds and tests the upstream command line
  tool.
