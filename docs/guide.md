# fastchess-gui user guide

The [README](../README.md) explains what fastchess-gui is and how to build it. This page covers the details.

## The window

A navigation rail on the left leads to six pages:

| Page | Contents |
|---|---|
| Tournaments | The home page: the running tournament and when it ends, the queue and when it all ends, the *Health* card, every tournament of the profile (resume, open, relaunch), *New tournament* |
| Engines | The engine library: options, health check, groups, reference Elo, import of other programs' lists |
| Books | The opening book library, and the openings of each book with what is wrong with them |
| Suites | Test suites (STS, WAC, ERET…): engines against the positions of an EPD file |
| Ratings | One rating list over every game of every tournament |
| Log | fastchess' own output and the messages of the GUI |
| Settings | Language, theme (automatic, dark, light), interface size, end of tournament notification, resuming at launch the tournament that was running, the tournaments folder |

The interface is in English, French, German, Spanish, Italian, Brazilian Portuguese, Russian or simplified Chinese.
At startup it follows the first preferred language of the system (English when there is no translation for it); it
can be forced in **Settings**. Chinese uses a font of the system: Microsoft YaHei on Windows, Noto Sans CJK or
WenQuanYi on Linux. The theme is light unless dark, or the system's choice, is picked in **Settings**.

The Spanish, Italian, Portuguese, Russian and Chinese translations are new: corrections from players of those
languages are welcome. They are plain `.po` files in `gui/i18n`, which Poedit and the like open.

The pages use the whole window, whatever its size: the Live tab's boards grow with it, the new tournament form
splits into two columns on a wide window, and the list of the Engines, Books and Ratings pages can be widened by
dragging its right edge. Everything (text, buttons, boards) can also be made larger or smaller, from 80 to 250 %:
**Settings** > Interface size, or Ctrl + mouse wheel, Ctrl + and Ctrl -, and Ctrl 0 to go back to 100 %.

**Ctrl+K** opens the command palette: the pages and tabs, the engines of the library (it opens the card), the books,
the tournaments of the home page, and the actions possible now (*Pause*, *Resume*, *Stop*, *Start*, *Add to the
queue*, the entries of *Import…*, *Check those not checked*, *Export for a rating list…*, *Copy* the standings…).
Letter case does not matter and the letters typed need not be adjacent ("rl" finds *Rating list*); names that start
with them, or with a word that does, come first. Up and Down choose, Enter runs, Escape closes; Ctrl+K is left alone
while a text field is being typed in. The *?* beside each page's title, or F1, opens this guide at the page's section.

A badge on the *Live* tab and on the *Tournaments* button gives the number of games in progress. A number field
takes arithmetic: `40*60`, `15+10`, with `+ - * /` and parentheses, worked out on Enter or when the field is left;
Escape gives the field its value back.

## Engines

**Add…** registers a UCI executable. The engine is started once to read its name, author and options. Options
are edited in a table: modified values are highlighted, and only values that differ from the engine default are
sent with `setoption` at the start of every game. `Threads` and `Hash` come first.

- **Duplicate** makes a second entry for the same binary, with other options (a different `Hash`, a skill level,
  a different network file…). Entries are named after the UCI `id name` when first added and can be renamed.
- **Check** starts the engine, measures how fast it answers `uci` and `isready`, and has it play one move; then it
  runs fastchess' UCI compliance steps (`id name` and `id author`, positions from a FEN and with moves, `go` with
  each clock, info lines with an integer score…), up to the first that fails. The result (✓ or the error) stays
  next to the engine with its date, and *UCI compliance report* lists the steps, with what the engine wrote after
  a failed one. An engine spoken to in xboard only gets the first part.
- **Quick test** plays a short match against the engine itself or another one: 10 games at 10+0.1 from 5 positions
  (one with an en passant capture), through the same code as a tournament, shown in its workspace while it plays.
  The result stays next to the engine: its score, its losses on time, crashes and illegal moves, and the share of
  its clock it used, which tells an engine that plays far too fast or too close to the flag. Its mate PVs are
  checked (see *Check the mate PVs*). Only the last quick test
  is kept, in `tests/` in the profile (with the engine logs of its games that ended badly).
- **Groups**: free labels separated by commas (`pool A`, `list 40/15`…). The list can be filtered by group or
  searched, and a group can be added to a tournament in one click. *Notes*: anything to remember about the engine.
- **A large library**: every engine search (the library, and the engine lists of the other pages) finds a name, the
  name the engine reports, its author, its program path or its notes; the state filter shows the engines that work,
  failed or changed, were never checked, have lost their program, speak WinBoard, or are inactive. Ctrl or Shift
  click selects several engines: *Check* (one after the other, never while a tournament plays), *Add to a group…*,
  *Remove…*. *More…* checks those never checked (*Check those not checked*) and removes the missing programs. A
  string option holding a file or a folder (`EvalFile`, `SyzygyPath`…) has a *…* button.
- **Inactive** (on the card, or from the right click for all the engines selected): the engine stays in the library,
  greyed, but is left out of the engine pickers (setup, suites, quick test, relaunch, adding an engine to a running
  tournament) and of *Check those not checked*. A tournament or a setup that has it already keeps it.
- **Reading the options** never blocks a start. *Resume* starts at once: a tournament plays the engines copied in
  its folder. *Start*, the queue (one tournament at a time) and the quick test wait only for the read of an engine
  they play, read first; any other read or check is interrupted and done again later. A start that waits says so on
  its button (*Waiting for …*, with *Cancel*), and another start is refused meanwhile. A start also waits for a
  running test suite, and a test suite does not start while a tournament runs or waits. A queued engine can be read
  next from its menu.
- **Reference Elo**: the engine's rating on a list you trust. A tournament or the rating list can be anchored on it,
  and the games of a tournament carry it in their `WhiteElo` and `BlackElo` tags (copied when the engine joins).
  *Import…* > *A rating list, for the reference Elo…* sets it for many engines at once (see below).
- *Restart the engine for every game* starts a new process for each game instead of reusing it. Otherwise a
  tournament keeps one waiting process per engine that still has games to play, up to 32 (plus two per game being
  played), and closes an engine after its last game: in a knockout as soon as its match is lost, in a Swiss once it
  has no game left in the last round. *Start timeout (s)* (60 by default) is the time an engine is
  given to answer `uciok` when it starts and `isready` at the start of each game; a slow starter (a large network
  to load) needs more. An engine that fails to start is tried again three times, 10 s apart, with the same timeout;
  a pause holds the tries, without using them up. A tournament created afterwards copies the timeout into its
  participants.
- **Moved or rebuilt programs**: the library remembers each program (size, date, CRC32). A program that is no
  longer there is marked **!** with *Locate…*, and a tournament does not start with it. A program that changed
  (a new build) has its options read again automatically. Each tournament also records the programs it was
  started with: resuming it with a program rebuilt or missing since is reported in the *Log*.

**Import…** > *An engine list…* reads the engine list of another program: ChessGUI (`ChsGUI_Eng.TXT`, with the
options of `ChsGUI_Cfg.ini` next to it), cutechess and Banksia (`engines.json`), Arena (`ArenaENG.cfg`, in Arena's
folder: each engine's program, arguments, protocol and, for UCI engines, the option values set in Arena) and other
INI style lists. A list that gives no engine says so, with the formats read and *Every engine of a folder…* to
try instead;
*Every engine of a folder…* takes every program found in a folder, and remembers the folder: *Refresh: the new
engines of the folder* then offers only the engines added to it since. The engines found are listed first, to untick
those to leave out; engines already in the library (same program and arguments) are skipped; the options of the
others are read; WinBoard engines are imported too.

*Import…* > *A rating list, for the reference Elo…* reads a rating list, pasted or opened: a rating list's text
version or its web page (saved or copied), Ordo's output (text or CSV), or a CSV of `name,elo` lines. A name of the
list is a library engine with the same name, or the only one whose name is the same letter case and spaces aside
(as for the players of imported PGN files, see *Ratings*). The preview shows each engine's reference Elo, old and
new, ticked to be set, and the names of the list that are no library engine. The names of a list often carry their
build (`64-bit 4CPU`): rename the engines to match them if needed.

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
- *Scores from White's side*, among the engine's options: for the old engines that give their scores from White's
  point of view rather than the side to move's, as the protocol asks; their scores as Black are reversed.
- An engine that asks to be started again for every game (`feature reuse=0`) gets *Restart the engine for every
  game* ticked when it is added.
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

The lines of a PGN book that start with `%` (escape lines, which the PGN standard makes comments; the AVT books have
some) are ignored, by the tournaments too, and the Books page counts them (*lines starting with %, ignored*):
fastchess-gui 0.4.13 and earlier read them as moves, and could play some openings of such a book from a wrong
position, often the starting one.

Its openings are listed below, every one or only those with a problem, searched by part of a FEN or by moves (a
PGN book shows its moves in SAN); a click shows the position on a board. *Use it for the next tournament* puts it
in the setup (with Chess960 for a Chess960 book). *Save a cleaned copy…* writes an EPD book without the illegal
positions, the duplicates and the finished games (and, if ticked, the positions with colours reversed), and adds
it to the library. *Run as a test suite…* opens an EPD book in **Suites**.

## Test suites

The **Suites** page runs an EPD test suite: positions with their best move (`bm`), the moves to avoid (`am`), or,
for the Strategic Test Suite, the points of several moves (`c0 "f5=10, Be5+=2"`).

- **Suite**: an EPD file, or an EPD book of the library. The lines without a legal position or without one of
  these operations are left out (their number is shown).
- **Engines**: any number of engines of the library, with their options; *Hash* and *Threads* force a value on
  every one (0 = each engine's own).
- **Time**, **Nodes** or **Depth** per position. *Found for (plies)*: a search stops once a solution has stayed
  the engine's first move for that many more plies of depth (0 = every search takes its whole time).
- **Automatic concurrency**: with a time, physical cores − 1 divided by the engines' threads; with nodes or a
  depth, every hardware thread.

A position is solved when the engine's final move is a solution: one of `bm`, none of `am`, or, with points only,
the move with the most. Its time to solution is when the engine settled on that move. The grid shows the
positions against the engines, ✓ with the time to solution or ✗ with the move played, and the STS points; the
totals come first: positions solved, their average time, and the STS score. A finished run is kept in `suites/`
in the profile; *Compare with a saved run…* adds the columns of an earlier run of the same suite, to compare
versions of an engine. *Export CSV…* writes the grid.

## New tournament

**Tournaments → New tournament** opens the setup, with a live summary on the right (number of games, games per
engine, estimated duration, openings, common settings, time odds). The setup of the last tournament is kept.

The summary also gives the memory the hash tables can take: two engine processes per game played at the same time,
and the engines waiting for their next game, each with its `Hash`. Above the free memory it warns (the system will
swap); above the memory installed the tournament does not start. `--run` says the same.

- **Format**:
  - *Match*: two engines, A (the engine under test; every statistic is from its point of view) and B (the
    baseline). A fixed number of games, until you stop it, or an SPRT: elo0, elo1, alpha, beta, model
    (`normalized` uses nElo bounds like fishtest and OpenBench, `logistic` classic Elo, `bayesian` BayesElo) and an
    optional maximum number of games. The *Gainer [0, 3]* and *Non-regression [-5, 0]* presets fill in the bounds,
    and the line under them tells how many games the test is likely to last. *Calculator…* draws, as fishtest's
    SPRT calculator does, the probability that H1 is accepted and the expected number of games against the true
    Elo difference, from an expected draw ratio and the RMS bias of the openings; *Use these bounds* copies the
    bounds it was given back to the setup.
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
- **Participants**: engines from the library, in seed order (click an engine, then Ctrl+Up or Ctrl+Down sends it
  to the top or the bottom). Each one can have its own `Hash`, `Threads`, fixed nodes or depth, and time odds (a
  percentage of the time control). *Common settings*, for a match too, force `Hash` and `Threads` on every engine
  (0 = each engine's own); *Syzygy tablebases for the engines* gives the tablebase directories (the same as the
  adjudication's) to every engine with a `SyzygyPath` option, and *Engines' own books off* sets `OwnBook` to false
  where it exists, so that every game is played from the tournament's openings. Both are off by default, and kept
  in templates and the queue.

  *Add an engine…* lists the library with each engine's reference Elo and whether it works, sorted by name or Elo,
  filtered by a search, an Elo range or *Only those that work*. A click adds one engine; several can be ticked and
  added with *Add the N ticked*. *Suggest N opponents within ± … Elo of* the first participant (the engine under
  test of a gauntlet) ticks the N active engines that work and whose reference Elo is closest to its own, 150 at
  most away by default.

  *Book*: an opening book of the participant's own, like Cute Chess' `book=`/`bookdepth=`: a Polyglot (`.bin`) or
  PGN book of the Books page and how deep it is followed, in plies from the start of the game, the tournament's
  opening included (*all*: as long as the book has a move; for a PGN book, its games' first 40 plies). A PGN book is
  first turned into a Polyglot book: each game's moves up to that depth, each move weighted by the number of games
  that play it; games with an illegal move are left out and counted. This runs in the background as soon as the
  book is chosen (progress in the *Book* cell, its popup and the summary) and is kept in the profile's `book-cache`
  folder until the file or the depth changes; a start that comes before the end waits for it. A book that cannot be
  prepared shows why in its popup, with *Try again*; each start also tries again. A tournament plays from the book
  prepared when it was started or resumed, even if the PGN file changes meanwhile. While the position
  is in that book, the participant plays the book's move at once, chosen by the book's weights; then its engine
  takes over. The tournament's openings, if any, are played first, by every engine; EPD books only give those and
  are greyed in the list. To compare books with one engine, click its *Book* cell, then *This engine once more,
  with another book*: each copy plays as `<engine> (<book>)`, so its results and rating stay apart. In a match the *Book* buttons sit next to A and B, and
  an engine can play itself with two books. Book moves are marked `{book}` in the PGN; hovering a name in the
  standings shows its book. WinBoard engines can have one too.
- **Time control**: base time and increment in seconds, optional moves per period, and a margin before a game is
  lost on time; a fixed time per move (`go movetime`, with the same margin); a fixed number of nodes per move; or a
  fixed depth in plies (`go depth`), which no engine can lose on time.
- **Concurrency**: the number of games played at the same time. In automatic mode it is measured at startup:
  - fixed nodes or depth: one game per hardware thread, since Hyper-Threading does not change the moves played;
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
  In a round robin, a gauntlet or a Swiss, *The same opening for every pair of a round* gives every game pair of a
  round the same opening, the next of the book for each round, so that no engine gets easier openings than the
  others of its round (an engine added later catches up in rounds of its own, each with its opening).
- **Chess960** (Fischer random): without a book, the 960 starting positions in random order (written once to
  `chess960.epd` in the tournaments folder); with a book, its positions. Every engine must play Chess960: UCI
  engines with the `UCI_Chess960` option, WinBoard engines with the `fischerandom` variant. The summary names those
  that do not, and such a tournament does not start.
- **Adjudication**: draw (from a move number, for N consecutive moves, under a score), resign (N consecutive moves
  above a score, optionally both engines agreeing), a maximum number of moves, and Syzygy tablebases.
- **PGN and misc**: event name, *Also append every game to*: a PGN file of yours that every tournament adds its
  games to, like cutechess' `-pgnout` (never overwritten, in the ChessBase / Lichess or cutechess comment style),
  in schedule order: the two games of an opening side by side, rounds in order, whatever order they end in. A game
  waits there until those before it are written; a resume carries on; when the file cannot be written (locked,
  moved), the games wait and the tournament goes on, with a warning in the overview. A crash or a power cut while
  games are appended never writes them twice: the resume checks the file and completes a write cut short, and
  never removes anything from it. Extra fields in the move comments (nodes, seldepth, nps, pv, tbhits, and the
  decision time `dt=`: when the engine settled on the move it played; `wdl=W/D/L` is always written for the
  engines that report it), on by default since the charts of the *Games* tab read them (pv makes the PGN noticeably
  larger), restarting engines
  that crash, keeping the engine log of every game (it is always kept for the games that end badly), and *Check the
  mate PVs*: a mate score's PV must have the right length and end in mate. Every PV is checked for illegal moves,
  moves after the end of the game and a best move that is not its first move; the moves with a wrong PV are marked
  in the Games tab and written in the log.

**Templates** save the current setup under a name and load it back later. **Start** creates the tournament and
plays it; **Add to the queue** keeps it for later (see *Queue*).

## Running a tournament

A running or opened tournament has a workspace with a header (name, progress, *Pause*, *Stop*, *Layout*) and
panels, shown by default as tabs:

| Tab | Contents |
|---|---|
| Overview | Match: Elo and nElo with their 95 % confidence interval, LOS, score, pentanomial counts, SPRT bounds and LLR; *Statistics* (folded) gives fishtest's numbers from the games and from the pairs: score, Elo, nElo and BayesElo with DrawElo, standard deviation per game, LLR of both, the variance ratio of pairs to single games, the RMS bias of the openings, the kinds of balanced pairs and the pairs won over lost, which *Copy* includes; and the charts (Elo and LLR over time, game lengths, how games ended, depth, NPS, time per move and decision time). Tournament: the standings (with *TF*, each engine's losses on time, in orange as soon as there is one; a *!* beside an engine that crashed or played an illegal move), progress by cycle or round, games per hour and time left, the latest results. A finished tournament adds a *Submission checklist*: all games played, every pair with both colours, no duplicate games, no crashes, time losses or illegal moves (with buttons that open those games), the hash of each engine (shown, not judged), ponder off, the opening book, the tablebases |
| Standings | Points, games, losses on time (*TF*), wins/draws/losses, ratings with their confidence interval, Sonneborn-Berger (Buchholz for a Swiss), byes. *Table* lists them one engine per row; *Crosstable* (round robins, up to 200 engines) shows every pairing. *Order:* by points, score % or seed order (which keeps the columns of a gauntlet's crosstable in place). A click on an engine lists its opponents below: games, with White, with Black, + = −, points and %, with the pairs of which only one game was played marked; a gauntlet opens with its seed selected. *Copy* as text, or as BBCode for a forum post (with a line giving the format, time control, games and book), and in a gauntlet *Gauntlet table as text* or *as BBCode, for a forum* (the seed, then its opponents by their own points, each with points, %, Elo ±, W-L-D, as TorsGUI lays it out); *Export HTML…*; all with *TF* like the headless mode's standings, in the order chosen, and ending with a line giving the games played of those planned, the draw rate, time control, book and event |
| Bracket | Knockout only: the bracket, round by round, with the score of every match |
| Schedule | Every game pair by cycle or round, with its state and the opening it plays (once started, or once its round started when the round shares one); editing (see below) |
| Broadcast | Live files, the TLCS server and the web page (see below) |
| Live | The games in progress, as a mosaic of boards or one game in focus with the clocks, the running search of each engine (depth, score, nodes, nps, PV in SAN), the move list and the evaluation curve (or, chosen above it, each engine's time per move with its decision time, depth with seldepth, nodes, speed, tablebase hits, or how many plies its PV repeats of what the other engine's PV predicted), with the win/draw/loss shares (`UCI_ShowWDL`, switched on for the engines that have it when they are added) after each search line and as bands under the curve, White's wins at the bottom. Under the board: the 50-move counter (plies since the last capture or pawn move, out of 100), the material difference with the pieces one side has more of, and the pieces left, marked *TB* when the tablebase adjudication can end the game; *Details under the boards* adds them to the mosaic. *Arrows* (beside *Flip*, on by default) draws the move the side to move is thinking of in its engine's colour and the reply it expects, lighter, in the opponent's colour, on the board in focus and the detached ones (not the mosaic). In a gauntlet, *Seed at the bottom* (on by default) shows each new game from the seed's side; *Flip* still turns the board over until the next game. *Detach* opens a board in its own window |
| Games | The finished games with their opening (ECO code and name), filterable by result (wins, draws, losses, losses on time, crashes and illegal moves, adjudications, wrong PVs), by what the evaluations tell (the engines disagree by N pawns for 3 moves each in a row, an engine's evaluation swings by N pawns from one of its moves to the next, the loser thought it was N pawns better; the game opens at that moment) and by opening (an ECO code or its beginning, a range such as `B20-B99`, or words of the name), with a replay board (← → Home End, or click a move or the curve; the same details under it and the same charts as in *Live*, from the fields the PGN was written with; a chart whose field the tournament did not write says so, with a button to the PGN options of the setup; with the win, draw and loss shares of the engines that report them under the evaluation curve) and the export of one, all or the filtered games (*Export all* proposes `<name>.pgn` next to the tournament's folder; the tournament's own PGN files are refused as a target). *Export for a rating list…* writes the games as rating list testers (e.g. CCRL) send them: `[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, a zip too if asked; the players named as the lists write them, `<engine> 64-bit 4CPU` (editable for each participant), the rounds numbered again, the `Event` and `Site` tags set, the voided games and the duplicates left out, and those lost by a crash or a stall too unless unticked; the `PlyCount` of a game lost by an illegal move counts only the moves written. The tester and the site are remembered. *Engine log* opens the log of a game that has one (see *Files*) |
| Openings | Results by starting position: its game pairs, White's score, the draws, and how many pairs White won twice, Black won twice (the opening favours a colour) or one engine won twice (it tells the engines apart), with the average length. Sorted by any column, filtered to the unbalanced, the drawn or the decisive ones; *Export as EPD…* writes the positions shown, to keep them or to clean a book. A click shows the position and its games |

**Panels and layouts**: every tab is a panel. Drag one by its tab to dock it on a side of another panel or as a tab
next to it, or drop it elsewhere to float it (also onto a second screen on Windows and Linux/X11, not on Wayland).
The game followed in *Live* comes apart in *Board*, *Players*, *Searches*, *Moves* and *Chart* panels; the parts
that have no panel open stay beside the mosaic in *Live*, and detached boards dock like the other panels. Close a
panel with the x on its tab; bring it back from *Layout* > *Panels*, or with *Show …* in the command palette, and it
comes back docked. *Layout* also offers ready-made layouts (*Tabs*, as by default; *Follow a game*: the board on the
left, the players, searches and chart on the right, the standings under them; *Large tournament*: the mosaic beside
the standings; *Broadcast*: a large board with the players, searches and standings), *Save the layout as…* (your
own, by name, kept in `layouts.json` in the profile), *Delete* and *Default layout*. A panel taken out of the
workspace stays on screen on the other pages; the arrangement is kept between sessions. F1 and the `?` open this
guide at the panel last clicked.

**Processor use**: while games are played, the processor time of every engine's processes (the engine behind a
WinBoard adapter included) is measured at each `go` and each move. The Overview shows, for each engine, the cores it
keeps busy while it thinks and the share of a core it uses while its opponent thinks, and warns when an engine uses
clearly more cores than its `Threads`, or thinks on its opponent's time (pondering it was not asked for, a background
thread): what makes a game unfair to the opponent and to the other games running. It needs 20 s of each to tell, and
counts every game played, in the window or headless (`--run`): what each game's engines used is written to
`processor.tsv` in the tournament folder, so a resume keeps it (games played before 0.4.12, or headless before
0.5.6, have no measure). It only warns;
it never decides a game.

**Pause** starts no new game pair. Games in progress play to the end, and so does the second game of a pair whose
first game has started, so a pause never throws a result away. **Resume** continues. **Stop** either finishes
the running games first, or stops now (the interrupted games are not counted and will be played again).

**Every tournament resumes exactly where it stopped**, after a stop, a closed window, a crash or a power cut: no
game is lost and none is played twice, and a half-played game pair is completed with the same opening and
colours. *Resume* on the home page continues it, with the statistics of the games already played: results,
and the engines' depth, time per move and NPS read back from the move comments (NPS only when the PGN records
it, *nps* in *PGN and misc*). When the program ended while a tournament was running (a crash, a power cut, a
reboot, or quitting while it ran), the next launch resumes it by itself, and the queue goes on after it: *Resume at
launch the tournament that was running*, in **Settings**, on by default. A tournament paused or stopped waits for
you, and `--autostart` starts the saved setup instead.

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
can be reordered or removed. The home page tells when the running tournament ends (*Ends about HH:MM (in …)*) and
when the whole queue does; the tournaments whose length cannot be known (an SPRT without a maximum, nodes or depth
per move) are counted apart. Its *Health* card gives the crashes or hangs, losses on time and illegal moves of the
last 24 hours, the memory free and installed, and the engine processes running.

**Relaunch with…** on a finished tournament opens its setup again, to play it once more or with an engine replaced
by another (typically the next version).

## Ratings

The **Ratings** page computes maximum likelihood ratings (the logistic model, draws as half a point, as Ordo does
by default) over every game of every tournament in the profile, and of PGN files imported. Engines are identified
by their name. The games counted can be narrowed at the top of the page, for every tab:

- a time control, a group of the engine library (the games between its engines), the last 30 or 365 days;
- *Imported games*: the games of the PGN files listed under *PGN files…* (a rating list's download, the games of
  another program), read from their headers; their engines are known by the names the PGN gives them, and a name
  that is a library engine's, letter case and spaces aside ("stockfish  17" for "Stockfish 17"), is that engine:
  its games count as the engine's and its reference Elo can anchor them. Nothing looser is applied by itself:
  "Stockfish 17 64-bit" stays a separate engine until you say which engine it is in *PGN players…* (beside the
  count of library engines, or in *PGN files…*). That window lists the players that are no library engine by their
  name, with a search: for each, *A separate engine* (the default) or a library engine. A name two library engines
  share, letter case aside, is shown in the warning colour for you to choose; a likely engine (its name begins the
  player's) is suggested as *Perhaps …?*, never applied. The window opens by itself after a PGN file is added when
  there is something to decide. The choices, a separate engine included, are kept with the engine (they follow a
  rename) and apply to every PGN file and to the rating lists imported for the reference Elo; the games are read
  again with them when the window is closed. The rating list says how many library engines the PGN files hold, its tooltip which;
- *Anchor*: the average at 0, or one engine at a fixed rating (by default the first engine with a reference Elo).

Four tabs:

| Tab | Contents |
|---|---|
| Rating list | The list, with a minimum number of games. Besides Elo, ± and the games, each engine shows its score, its wins (+), draws (=) and losses (−), its draw rate, *Opp.*: the average rating of the opponents it met, game by game, *LOS*: the likelihood that it is stronger than the next engine of the list, from the two ratings and the covariance of their errors (approximate beyond 1000 engines, as its tooltip says), and *Gap*: its Elo above the next engine. A click on a column's header sorts by it (# keeps the place by rating); a right-click shows or hides columns (*Gap* is hidden at first). *Copy* as text or BBCode, with *With a header* to put the header below above the list. *Export…*: CSV with every column; an HTML page (light or dark) and Markdown (a table GitHub and forums show), both opening with a header that tells how the list was made: the tournaments and PGN files counted, the games with White's and Black's wins, the draws and White's score, the anchor, the filters, the minimum number of games, the date and the version; and *Games for Ordo…*: the games counted as a PGN of headers, which Ordo and BayesElo read (`ordo -p ratings.pgn -o ratings.txt`). A click on an engine opens its card |
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
  viewers can follow the tournament (board, clocks, PV, crosstable and the games played). It is checked against
  node-tlcv. *One port per game* puts every concurrent game on a port of its own: game
  #1 of the *Live* tab on the UDP port, #2 on the next one, and so on. Each port opens when its game starts, with
  its own limits on clients and logons, and gets the crosstable after every game; *Game shown* then does not
  apply.
- **Web page** (TCP port): a page served by fastchess-gui itself, for any browser on the network, read-only and
  refreshed every second. The followed game (the first in progress, or the one clicked) is in the middle: the board
  with the last move, a king in check, the coordinates and the arrows; each player's clock in large type; each
  engine's evaluation in large type with who is thinking, its search as tiles (depth/selective depth, nodes, speed,
  time, tablebase hits, hash) and its line of play; and the moves, with the book moves in italics. Step through the
  moves with the arrows under the board or the ← → keys (▶ past the last move goes back to the game as it is
  played), turn the board with ⇅ or F, and copy the game's PGN or the position's FEN. Beside the game, tabs show the
  standings and crosstable, the charts of the followed game, each engine's search on average per move with its
  anomalies, the next games of the schedule (a Swiss or a knockout pairs its later rounds as it goes), the
  statistics and anomalies of the games played, and the event. Every game in progress is shown below, with a small
  evaluation bar beside each player. *Moves* switches the moves between letters (Nf3) and figurines (♘f3), and the
  theme follows the system or is set to light or dark; the browser remembers these choices. The page loads nothing
  from elsewhere and works on phones. *Download the games (PGN)* gives the tournament's PGN file, up to the last
  game recorded. `state.json`, which the page reads, holds all of it for scripts.
- **lichess**: a lichess broadcast, with lichess' board, evaluation bar and chat, for anyone on the internet. It
  needs a personal API token with the `study:write` scope (*Create a token on lichess…*), kept in `settings.json`
  in the profile only, saved when its field is left. With no round given, a broadcast named after the tournament is made, unlisted unless
  *Listed among lichess' broadcasts* is ticked; its link is shown once it is on air. Otherwise the games go to the
  round whose URL or id is given (a broadcast fed by pushes, not by a source URL). The games in progress are
  pushed every 8 seconds with their clocks, and each finished game once; a round holds 64 games,
  after which the next round of the same broadcast is made. Only the games finished after it was switched on are
  pushed; `lichess.json` in the tournament folder remembers where they went, so a resumed tournament continues
  the same broadcast. lichess keeps only the clocks of what it is sent (it drops the `[%eval]` and the text of the
  move comments): its evaluation bar is its own, and the engines' evaluations do not show there.

  The connection uses Mbed TLS, built into the program, and trusts only the root certificates that sign lichess.org
  (Google Trust Services) and those of Let's Encrypt. It goes through the HTTP proxy that `HTTPS_PROXY` (or
  `ALL_PROXY`) names, as `http://proxy.example:3128` or `http://user:password@proxy.example:3128`, except for the
  hosts of `NO_PROXY`; on Windows too the proxy comes from these variables, not from the system settings. SOCKS
  proxies are not supported.

### Putting the web page on the internet

The web page can be followed from anywhere without opening a port in the router: tick *Publish on the internet*
under *Web page* in the *Broadcast* tab. While a tournament runs, fastchess-gui opens an SSH tunnel (libssh2, built into the
program) to a tunnel service, which gives the page a public https address; nothing needs to be installed.

- *localhost.run* (the default): free, without an account. Its addresses change now and then, and its speed is
  limited; the page and its state are sent compressed, so this does not matter much. Its host key is known to
  fastchess-gui: a connection to anything else is refused.
- *Another SSH server*: its host, port and user, and the port it opens for the page (as
  `ssh -R REMOTE_PORT:localhost:PORT user@host` would ask; 0 lets it choose). Only servers that let the user in
  without a key or a password can be used (authentication "none"), and they must offer curve25519-sha256, an RSA
host key of 2048 to 4096 bits, and AES (GCM or CTR). The server's key is
  trusted the first time and kept in the profile (`known_hosts.json`); if it changes, the tunnel stops and says so,
  and *Trust the new key* is for when the server's owner confirms it changed.

The tab shows the state (connecting, on air with the number of connections, reconnecting, or an error) and, once
known, the public address with *Copy*, *Open* (to see the page as viewers do) and a QR code for a phone (click it to
enlarge). The tunnel reconnects by itself after a failure, waiting a little longer each time, and shows the new
address if it changed. It stops with the web page or with the tournament.

Other tunnels work too, started separately: with Cloudflare's, for example, switch on the web page (port 8080 here)
and run `cloudflared tunnel --url http://localhost:8080`; it prints an address such as
`https://some-words.trycloudflare.com`. ngrok, tailscale funnel or a reverse proxy such as nginx, under a path of its
own as well (`location /live/ { proxy_pass http://localhost:8080/; }`), work the same way.

The page is read-only: it answers only GET requests for the page, its state and the PGN file, never a file a request
names, and nothing in it changes the tournament. Each request is answered on its own, 32 at a time at most, and must
arrive within a second and be answered within ten, so a slow or silent visitor holds no one else; behind the tunnel
each viewer's connection counts as one of those: 40 are relayed at once, as many more wait their turn, and the
next are refused; a viewer who sends more than the page takes is let go alone. Two downloads of the PGN file
run at once, and the next is asked to come back a moment later. The state is never cached, by the tunnel either, so
viewers see the games as they go; the page is kept by the browser and checked again at each load. The viewers shown
in the *Broadcast* tab are counted from the requests the pages make, so the count is right behind a tunnel too.

Viewers see the tournament's name, the engines' names, their ratings, searches and anomalies, the openings and the
PGN file of the games played. The folders and the engines' files stay private. Headless:

```
fastchess-gui --run FOLDER --tunnel                      # localhost.run, the address printed on the console
fastchess-gui --run FOLDER --web 8080 --tunnel ssh://user@host[:port][/remote_port]
```

### Where the viewers talk

The page has no chat of its own; it points to where the talk happens. A *Chat link* under *Web page* (a Discord
invite, a Twitch channel, a forum thread; https only, anything else is refused with a message under the field) puts a
*Chat* button in the page's header that opens it in a new tab. While the games are pushed to lichess, the page also
links to the lichess round, which has its own chat.

A *Discord server ID* adds a *Discord* tab beside *Event*, with Discord's own server widget in the page's light or
dark theme: the server's name, the members online, its voice channels and a *Join* button, not the messages. The
server's owner switches it on in Server Settings, Widget ("Enable Server Widget"), which also shows the server ID to
copy (17 to 20 digits); with Developer Mode on, right-clicking the server's icon gives *Copy Server ID* as well. The
widget is loaded from discord.com only once a viewer opens the tab, and Discord then sees that viewer's visit (their
address, their browser, their Discord login if they have one); without an ID, the page contacts nobody but
fastchess-gui. The chat and lichess links send nothing until a viewer clicks them, and then no referrer. A setup file
run headless (`--run SETUP.json`) takes both from `web_chat_link` and `web_discord_id`.

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
fastchess-gui --run FOLDER --tlcs 16001 --tlcs-lanes  # one port per game: 16001, 16002…
fastchess-gui --run FOLDER --web 8080               # the web page on http://<this machine>:8080/
fastchess-gui --run FOLDER --tunnel                # the web page on the internet (serves it too, --web's port or 8080)
fastchess-gui --run FOLDER --live-files [DIR]       # live.pgn, standings.html, tournament.json ("live" by default)
fastchess-gui --run FOLDER --lichess new            # a new lichess broadcast; or --lichess ROUND_URL|ROUND_ID
```

`--lichess` takes the token from the `FCG_LICHESS_TOKEN` environment variable, or the profile's.

The options can be combined. A setup file saved with broadcasting switched on broadcasts the same way without
them. For viewers outside the local network, open the ports in the router: TLCV and node-tlcv connect to
the TLCS port. For the web page, a tunnel avoids opening a port (see *Putting the web page on the internet*).

## Files

Everything lives in the profile folder: `~/.local/share/fastchess/fastchess-gui/` on Linux,
`%APPDATA%\fastchess\fastchess-gui\` on Windows.

- `engines.json`: the engine library, and `engines.json.bak` its previous version, read if the first is lost;
- `setup.json`: the last setup; `templates/*.json`: the saved templates; `queue.json`: the queue;
- `book-cache/`: the PGN books turned into Polyglot books for the participants' own books (removed after 30 days unused);
- `layout.ini`: the window layout and the panels of the workspace; `layouts.json`: the layouts saved by name; `fastchess-gui.log`: the log of the last session;
- `suite.json`: the last setup of the Suites page; `suites/<suite>-<date>.json`: the finished test suite runs;
- `settings.json`: the tournaments folder, when another one was chosen in **Settings**, the folder of *Every engine
  of a folder…*, the tester and site of *Export for a rating list…*, and the lichess token;
- `running.json`: the tournament running now, removed when it stops; found at launch, it tells what to resume;
- `runs/<date>_<A>_vs_<B>/` for a match, `runs/<date>_<name>/` for a tournament (in `runs/` or the chosen
  folder), one folder each, which can be moved, copied, zipped and sent:
  - `tournament.json`: the format, the participants (engines copied with their options) and the conditions;
  - `journal.jsonl`: everything that happened since, one line per event (game pairs started, games finished,
    changes, Swiss and knockout pairings); with the PGN files, it is what a resume reads. A game is on disk before
    its line, so a power cut loses at most the games being played. Game text no line points to (a game dropped by
    a change while it was saved, or left by a crash) is replaced with blank lines, so other PGN viewers only show
    the tournament's games;
  - `tournament.lock`: held by the program playing the tournament; another program opening the folder only
    shows it;
  - `<name>.pgn`: move comments in the cutechess / fastchess style, `{+0.35/18 1.234s}`, with the score from the
    point of view of the side that moved; games in the order they ended (the journal finds each one by its place
    in the file; *Export all* in the *Games* tab, and the file of *Also append every game to*, follow the schedule);
  - `<name>-chessbase.pgn`: `{[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}`, with the evaluation from
    White's point of view, as read by ChessBase / Fritz and Lichess;
  - `<name>` is the tournament's name when it was created (renaming it later keeps the files), with the characters
    Windows does not allow replaced by `_` and cut to 64 bytes; `tournament.json` records both names. A tournament
    without a name, one created before 0.4.13, or a match imported from 0.2.x has `games.pgn` and
    `games-chessbase.pgn`;
  - `lichess.json`: the lichess broadcast and round its games went to (no token);
  - `processor.tsv`: the processor time each game's engines used, for the *Processor use* of a resumed tournament;
  - `logs/game-N.log`: every line exchanged with both engines during game N (the number shown in the *Games*
    tab), with the time it was sent or read (`W>` sent to White's engine, `W<` read from its output, `W!` read
    from its error output: assertions, crash messages), for the games lost on time, by a crash, a stall or an illegal move
    (or every game, if asked). The first 300 lines of each engine and the last 5000 lines of the game are kept, in
    the order they came. It is what an engine author needs to reproduce a bug. For a WinBoard engine it shows the UCI side; `FCG_WB2UCI_LOG` gives the
    WinBoard side.

Matches played with an earlier version (0.2.x) appear on the home page: *Import them* turns them into finished
tournaments.

## Command line

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
              [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
```

- `--profile DIR` uses another profile folder;
- `--autostart` starts the saved setup at launch (rather than resuming the tournament that was running, see
  *Running a tournament*);
- `--shot` saves a screenshot of the window after the given number of seconds (default 5), with WINDOW brought to
  the front, and quits once every screenshot is taken. It can be repeated. WINDOW is a page (`home`, `setup`,
  `engines`, `books`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`, `settings`) or a
  tab (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). The mouse is ignored in this
  mode and, on Linux, the window is rendered off screen (no window appears; set `SDL_VIDEODRIVER` to override);
- `--focus WINDOW` opens that page or tab at launch, or: `engines-card` (the first engine's card),
  `engines-refresh` (*Refresh: the new engines of the folder*), `engines-ratings=FILE` (the preview of that rating
  list), `setup-picker` and `setup-suggest` (*Add an engine…*, with the suggestion made), `submission-export`
  (*Export for a rating list…*), `ratings-players` (the *PGN players* window), `palette[=TEXT]` (the command palette, with TEXT typed in it);
- `--select-game N` opens game N (in finishing order) in the *Games* tab as soon as it exists;
- `--quit-after SECONDS` stops the tournament and quits;
- `--run`: see *Headless mode*.

## Development notes

- `tournament/src/tour` is the tournament library (`fcg_tournament`): model, schedule, Swiss and knockout
  pairings, journal, state, the runner that drives fastchess, standings and ratings. It has no GUI dependency and
  its own tests, `fcg-tournament-tests`. [docs/design/tournament.md](design/tournament.md) describes it.
- `gui/src` holds the application, `gui/tests` its tests (`fastchess-gui-tests`, including real tournaments with
  fastchess' random mover engine). The code is written in English; every user visible string goes through `TR()`
  or `trf()`, `trn()` when it holds a count (`trn("{} game", "{} games", n, n)`: the right plural in every
  language), and `N_()` marks the strings translated later through a variable. The translations are the `.po`
  files of `gui/i18n`, embedded in the program and read with tinygettext. `python3 gui/i18n/update.py` writes
  `messages.pot` from the sources and adds new messages to every `.po` file (removing those gone). A test fails
  when a message has no translation in a language, when a translation does not keep the `{}` and `%d`
  placeholders of the English text, or when a plural message lacks one of its language's forms.
- The fastchess hooks are listed in the README. `make` in `app/` still builds and tests the upstream command line
  tool.
