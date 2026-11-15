# fastchess-gui user guide

The [README](../README.md) explains what fastchess-gui is and how to build it. This page covers the details.

## The window

A navigation rail on the left leads to eight pages:

| Page | Contents |
|---|---|
| Tournaments | The home page: what runs and the cores it takes, what needs action, the queue and when it all ends, the last tournaments finished with their result, the *Health* card, every tournament of the profile (resume, open, relaunch), *Pause all*, *New tournament* |
| Engines | The engine library: options, health check, groups, reference Elo, import of other programs' lists |
| Books | The opening book library, and the openings of each book with what is wrong with them |
| Board | A game or a position from anywhere, analysed by an engine of the library |
| Suites | Test suites (STS, WAC, ERET…): engines against the positions of an EPD file |
| Ratings | One rating list over every game of every tournament |
| Log | fastchess' own output and the messages of the GUI, and *Report a problem…* |
| Settings | Language, theme (automatic, dark, light), interface size, end of tournament notification, resuming at launch the tournament that was running, the appearance of the boards (colours, pieces, move sounds, see *Appearance*), the profile folder and the portable mode, the tournaments folder, the remote machines (see *Machines*), the lichess token, the version and new versions, what draws the window and software rendering, *Report a problem…* |

The interface is in English, French, German, Spanish, Italian, Brazilian Portuguese, Russian or simplified Chinese.
At startup it follows the first preferred language of the system (English when there is no translation for it); it
can be forced in **Settings**. Chinese uses a font of the system: Microsoft YaHei on Windows, Noto Sans CJK or
WenQuanYi on Linux. The theme is light unless dark, or the system's choice, is picked in **Settings**.

The Spanish, Italian, Portuguese, Russian and Chinese translations are new: corrections from players of those
languages are welcome. They are plain `.po` files in `gui/i18n`, which Poedit and the like open.

The version is in the window title and in **Settings**, whose *Copy* puts it on the clipboard with the system, for
a bug report. *Tell me when a new version is out* (off by default) asks GitHub for the latest release at launch, at
most once a day and in the background; when it is newer, the home page says so, with *Download page* (the release's
page on GitHub, in the browser), *What's new* (its notes) and *Hide* (until the next version). Nothing is downloaded
or installed by the program, and a failure (no network, GitHub unreachable) is only written in the log.

*Report a problem…* (in **Settings**, on the **Log** page, in the command palette, and in the message of a failure
at startup) opens a new issue on GitHub in the browser, from the bug report form, already filled with the version,
the system (operating system, processor, memory, graphics card and driver) and the last lines of the log. Read it
before sending it, remove what you do not want to show (paths, engine names) and say what you did, what you
expected and what happened. The program sends nothing itself. Ideas go to the same place: *New issue*, *Idea*.

The window opens as it was left: its size, its place on the screens, maximized or not. When the screen it was on is
gone (a monitor unplugged), it opens in the middle of the main one; when its size no longer fits the screen (a lower
resolution), at the default size. The panels dragged out of it, such as a detached board, come back where they were
too (`layout.ini`), and into the window when their screen is gone. Wayland does not let a program choose its
window's place: there, only the size and the maximized state come back.

The pages use the whole window, whatever its size: the Live tab's boards grow with it, the new tournament form
splits into two columns on a wide window, and the list of the Engines, Books and Ratings pages can be widened by
dragging its right edge. Everything (text, buttons, boards) can also be made larger or smaller, from 80 to 250 %:
**Settings** > Interface size, or Ctrl + mouse wheel, Ctrl + and Ctrl -, and Ctrl 0 to go back to 100 %. F11 makes
the window full screen, and back.

Under every board that shows a game (the replay of the *Games* tab, the game in focus in *Live*, a detached board,
the Board page), a row of small buttons goes to the start position, the previous move, the next move and the last
one, and turns the board over; their tooltips give the keys, which do the same: ← → Home End, and F to flip. Those
who prefer the keys hide the buttons with **Settings** > Boards > *Navigation buttons under the boards*, a right
click on them, or the command palette (*Hide the navigation buttons under the boards*, and *Show…* to bring them
back). The mouse wheel over a move list scrolls the list, never the moves.

**Ctrl+K** opens the command palette: the pages and tabs, the engines of the library (it opens the card), the books,
the tournaments of the home page, and the actions possible now (*Pause*, *Resume*, *Pause all*, *Stop*, *Start*, *Add to
the queue*, *Relaunch … with…*, the entries of *Import…*, *Check those not checked*, *Export for a rating list…*, *Copy* the standings,
*Copy the version*, *Report a problem…*, the board colours and piece sets, the move sounds on or off…).
Letter case does not matter and the letters typed need not be adjacent ("rl" finds *Rating list*); names that start
with them, or with a word that does, come first. Up and Down choose, Enter runs, Escape closes; Ctrl+K is left alone
while a text field is being typed in. The *?* beside each page's title, or F1, opens this guide at the page's section.

A badge on the *Live* tab and on the *Tournaments* button gives the number of games in progress. A number field
takes arithmetic: `40*60`, `15+10`, with `+ - * /` and parentheses, worked out on Enter or when the field is left;
Escape gives the field its value back.

The window is drawn with OpenGL 3.2. Where there is none (a virtual machine without 3D acceleration, a remote
desktop, an old server), the program draws it through SDL's own renderer instead: Direct3D 11 on Windows, OpenGL
2.1, or the processor alone. The log says which, and why OpenGL was not used; **Settings** > Rendering shows it too,
with the graphics card's name when OpenGL draws. *Software rendering* there (or `--software` on the command line, for
one launch) has the processor draw the window from the next launch, for a graphics driver that fails or crashes. It
uses more processor time, redraws only what changes (and what moves, such as clocks, 15 times a second rather than
30 while the mouse and keys rest), draws square corners, and the panels of the workspace float inside the main window
rather than onto another screen. Under Wayland (and on macOS) the processor's picture is still shown through the
graphics driver; if there is none, start the program with `SDL_VIDEODRIVER=x11`. When no way of drawing works, a
message says so before the program ends.

### Appearance

**Settings** > *Appearance* chooses how every board of the program looks (the live mosaic and the game followed,
the detached boards, the big screen, a game replayed in *Games*, the *Openings* tab, the openings of a book), with
a small board beside the choices showing them:

![Settings > Appearance: the brown board and the Celtic pieces](screenshots/appearance.png)

- **Board**: the colours of the squares, for the light theme and for the dark theme each (the board of the theme in
  use is drawn). A preset (*Blue grey*, the colours of the earlier versions; *Brown*; *Blue*; *Green*; *Grey*), or
  *Custom*: a click on *Light squares*, *Dark squares* or *Last move* opens a colour picker, and changing a colour
  makes the board a custom one from the colours it had. *Last move* is laid over the two squares of the last move,
  its transparency letting the square's colour through.
- **Pieces**: a set built into the program (*Cburnett*, the default; *Chessnut*; *Celtic*; *Fantasy*; their authors
  and licenses in the tooltip and in `ATTRIBUTION.md`), or *Your own folder…*: a folder of 12 SVG pictures named
  `wK.svg`, `wQ.svg`, `wR.svg`, `wB.svg`, `wN.svg`, `wP.svg` and `bK.svg` … `bP.svg` (white and black king, queen,
  rook, bishop, knight, pawn). The pictures are drawn with nanosvg, as the built-in ones: plain shapes, fills,
  strokes and gradients; no text, filters or embedded images. When a file is missing, larger than 1 MB or not a
  picture that can be drawn, the page says which and why, the default pieces are drawn meanwhile, and *Read again*
  reads the folder once the files are fixed. The web page of the broadcast keeps the default pieces.
- **Move sounds** (off by default): *A sound for each move* plays a short knock for each move of the game followed,
  in *Live* and on the big screen, and a heavier one for a capture; not for every game of the mosaic. *Volume*, and
  *Try a move* / *Try a capture* to hear them. The sounds are made by the program (no sound file); the sound device
  is opened only while they are on, and when there is none the log says why and the program stays silent.

The command palette has these choices too (*Board colours: Brown*, *Pieces: Celtic*, *Turn the move sounds on*…).
They are kept in `settings.json` in the profile; the folder of pieces is kept relative to the program's folder in
the portable mode.

**Engine logos**: the card of an engine in the **Engines** page has *Logo* > *Choose…*, a PNG, JPEG or SVG picture
of 2 MB at most (4096 pixels each way at most), shown beside the engine's name in *Live*, on the big screen and on
the broadcast web page. The picture is copied into the folder `logos` of the profile, so the original can be moved
or deleted and the profile moved whole; *Remove* takes it away (and its copy, unless another engine has it). The
logo can be changed while the engine plays.

## The home page

The **Tournaments** page is a control room: what runs and what needs you come first, then the rest.

- **Pause all**, beside *Open a folder…* and *New tournament*, pauses the running tournament (no new game pair
  starts, the games in progress play to the end) and holds the queue: no queued tournament starts until **Resume
  all**, which resumes the tournament and lets the queue go on, at once if a tournament ended meanwhile.
- **Cores**: the machine's hardware threads, and those the games being played take, the rest free. A game takes
  the `Threads` of its engine that has the most, since the two engines think in turn; the bar shows the tournament's
  name and its count, the cursor the details.
- **Needs action**, when something does, each with its fix in place: an engine that cannot start (see *An engine
  that cannot start*: *Try again*, *Withdraw…*, *Replace…*, *Engine log*); the tournament an error or an engine
  stopped, with what stopped it, *Open the engine*, *Open the log*, *Resume*, *Open* and *Hide*; a drive too full
  for a night of games (under 1 GB free, or under 2 % of a drive under 500 GB), the tournaments folder's or the
  running tournament's, looked at every minute, with *Open the folder* and *Change the tournaments folder…* (where
  new tournaments go, as in **Settings**).
- **Running**: the card of the running tournament: its name, format, time control and state, a progress bar with
  the games played of those planned and the games being played, when it ends, and who leads: the first of the
  standings with its rating against the field and its points (a knockout: the players left; a team match: the
  teams' score and its Elo), the engine ahead in a match with its Elo and the score, or the LLR of an SPRT with its
  bounds, then the Elo and the score. *Open* goes to its workspace, *Pause* pauses it. A tournament that can be
  resumed shows instead under *Interrupted*, with *Resume*.
- **Queue**: the tournaments waiting, each with its length and when it starts, and when everything ends (see
  *Queue and relaunch*).
- **Last finished**: the last five tournaments finished, with their result: a match's score, the SPRT's decision,
  a knockout's winner (of its final), a team match's score, or the first of the standings with its points of its
  games. *Relaunch with…* for a match, and *Open*.
- **Health**: the crashes or hangs, losses on time and illegal moves of the last 24 hours, the memory free and
  installed, and the engine processes running; then the engines that lost those games, the three with the most,
  with *Open the engine* and *Open the folder* (the tournament's `logs` folder keeps the engine log of every game
  that ended badly). A loss on time before move 20 is told apart: it is more often an engine set up wrong (too many
  threads, a hash too large to allocate in time, a network loaded at every game, a slow start) than its time
  management.
- **All tournaments**: every tournament of the profile, newest first, with *Open* and *Folder*; its title folds
  it. The result of a finished one shows at the cursor, over its state.

The command palette has these actions too: *Pause all*, *Resume all*, *Relaunch … with…* for the last matches
finished, *Open the tournaments folder* and *Change the tournaments folder…*.

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
- **Console…** talks to the engine, outside any tournament: it is started with the library's program, arguments
  and working directory, and a UCI engine gets `uci` and then the library's option values. Lines typed are sent as
  they are (Enter; Up and Down bring the earlier ones back), and the log shows every line with its time, those sent
  marked `>` and the error output `!`. Buttons send the usual commands (`isready`, `position startpos`,
  `go depth 10`, `go infinite`, `stop`…; WinBoard's for a WinBoard engine, spoken to in its own protocol unless
  *Through the adapter* is ticked) and the position of the Board page. *Copy* and *Save…* keep the log. The
  console shows in place of the engine's settings (*Settings* goes back to them, the console staying open); *Close*
  ends the engine. While a tournament runs, what the engine searches takes cores from its games: the console says
  so. Its engine is not one of the tournament's processes: the end of a tournament does not end it.
- **Quick test** plays a short match against the engine itself or another one: 10 games at 10+0.1 from 5 positions
  (one with an en passant capture), through the same code as a tournament, shown in its workspace while it plays.
  The result stays next to the engine: its score, its losses on time, crashes and illegal moves, and the share of
  its clock it used, which tells an engine that plays far too fast or too close to the flag. Its mate PVs are
  checked (see *Check the mate PVs*). Only the last quick test
  is kept, in `tests/` in the profile (with the engine logs of its games that ended badly).
- **Groups**: free labels separated by commas (`pool A`, `list 40/15`…). The list can be filtered by group or
  searched, and a group can be added to a tournament in one click. *Notes*: anything to remember about the engine.
  *Logo*: a picture shown beside its name in *Live*, on the big screen and on the broadcast web page (see
  *Appearance*).
- **A large library**: every engine search (the library, and the engine lists of the other pages) finds a name, the
  name the engine reports, its author, its program path or its notes; the state filter shows the engines that work,
  failed or changed, were never checked, have lost their program, speak WinBoard, or are inactive. Ctrl or Shift
  click selects several engines: *Check* (one after the other, never while a tournament plays), *Add to a group…*,
  *Remove…*. *More…* checks those never checked (*Check those not checked*) and removes the missing programs. A
  string option holding a file or a folder (`EvalFile`, `SyzygyPath`…) has a *…* button.
- **Inactive** (on the card, or from the right click for all the engines selected): the engine stays in the library,
  greyed, but is left out of the engine pickers (setup, suites, quick test, relaunch, adding an engine to a running
  tournament) and of *Check those not checked*. A tournament or a setup that has it already keeps it.
- **Uses a GPU** (on the card): a network engine that plays on a graphics card, such as Lc0. No more such engines
  play at once than the *GPUs* of **Settings**, in a tournament and across tournaments (see *Several tournaments at
  once*); from the next tournament it joins.
- **Reading the options** never blocks a start. *Resume* starts at once: a tournament plays the engines copied in
  its folder. *Start*, the queue and the quick test wait only for the read of an engine they play, read first; any
  other read or check is interrupted and done again later. A start that waits says so on its button (*Waiting for
  …*, with *Cancel*), and another start is refused meanwhile. A start also waits for a running test suite, and a test
  suite does not start while the quick test runs or a start waits. Reads, checks and test suites run beside the
  tournaments, which play in processes of their own. A queued engine can be read
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
  a pause holds the tries, without using them up. Then its games are set aside and the others go on (see *An
  engine that cannot start*). A tournament created afterwards copies the timeout into its participants.
- **Moved or rebuilt programs**: the library remembers each program (size, date, CRC32). A program that is no
  longer there is marked **!** with *Locate…*, and a tournament does not start with it. A program that changed
  (a new build, or another file under the same path) has its options read again automatically.
- **What a new build changed**: when the new read differs from the one before, the engine is marked **Δ** and its
  card lists the differences until *Dismiss*: its new `id name` (with *Rename it* when the engine was named after
  the old one) and author, each option whose default, bounds or choices changed (before and now), the options new
  and gone, and your value of each (the value of a gone option is dropped; one the new build no longer accepts is in
  orange). For an option whose default changed, or whose value no longer fits, choose *Keep my value* (or *Keep the
  old default* where you had none: it plays as before) or *Use the new default*; *Keep my values* and *Use the new
  defaults* choose for all. The notice goes once every choice is made, unless it has more to tell. Several builds
  before it is dismissed make one notice, against the read before the first. *Failed or changed* in the state
  filter shows these engines too, and the command palette has *What changed*, *Keep my values*, *Use the new
  defaults* and *Dismiss what changed* for each.
- **The programs of the tournaments**: each tournament records the programs it was started with. The setup's
  summary warns of an engine whose program changed since its last tournament (the newest one of the tournaments
  folder that played this program), with *What changed* when its card has a notice. Resuming a tournament with a
  program rebuilt or missing since says so in its header, with *Continue* (the notice closes) and *Pause*; the
  tournament does not stop by itself.

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

fastchess leaves the openings whose position cannot be played out of a tournament (a FEN or EPD line without a
king or otherwise malformed, a king that could be taken, a PGN game whose FEN tag is invalid), with one warning per
book; they are counted here as illegal positions. A PGN game whose moves stop parsing (a malformed or ambiguous
move) keeps the moves before it.

The lines of a PGN book that start with `%` (escape lines, which the PGN standard makes comments; the AVT books have
some) are ignored, by the tournaments too, and the Books page counts them (*lines starting with %, ignored*):
fastchess-gui 0.4.13 and earlier read them as moves, and could play some openings of such a book from a wrong
position, often the starting one.

Its openings are listed below, every one or only those with a problem, searched by part of a FEN or by moves (a
PGN book shows its moves in SAN); a click shows the position on a board. *Use it for the next tournament* puts it
in the setup (with Chess960 for a Chess960 book). *Save a cleaned copy…* writes an EPD book without the illegal
positions, the duplicates and the finished games (and, if ticked, the positions with colours reversed), and adds
it to the library. *Run as a test suite…* opens an EPD book in **Suites**.

**Create…** writes books for the library (also in the command palette):

- *Generate Chess324 book*: Larry Kaufman's Chess324. The kings and the rooks stay on their usual squares, so
  castling is the usual one; the queen, the bishops and the knights of each side are shuffled on the other squares
  of its back rank (the b, c, d, f and g files), the bishops on squares of opposite colours. That makes 18
  arrangements a side, White's and Black's chosen independently: *All 324 positions*, or *Only the 18 symmetrical
  ones*, where Black's pieces stand on the same files as White's. They are ordinary positions, written to an EPD
  file (offered next to the books of the library, or in `books/` in the profile) and played by any engine, without
  Chess960.
- *Build a Polyglot book…*: a Polyglot book (`.bin`) from the games of a PGN file, as Polyglot's `make-book` does,
  with the conversion that turns the participants' PGN books into Polyglot books. Its form, in place of the book's
  details, takes the PGN file (the PGN book chosen in the list, if any), the **depth** (the plies of each game that
  go into the book), the **minimum games** a move needs in its position to be kept (Polyglot's `-min-game`; the
  others are left out as played too rarely), the **weights**, *by score* (the points of the side that played the
  move: a win 2, a draw 1, a loss 0, as Polyglot counts; games without a result are left out, and so is a move whose
  games were all lost) or *by count* (the number of games that play it), and, if ticked, a **rating**: only the
  moves of a player rated at least that much count, by the game's `WhiteElo` or `BlackElo` tag (a player without
  one is left out). *Build…* asks where to write the book (next to the PGN file by default), then reads the games in
  the background, with its progress and *Cancel*, beside the form and at the end of the list; the book, sorted by
  key as the format wants, is then added to the library, its options in its description. The console says how many
  games went into it, how many the options left out and how many could not be read.

## Board

The Board page shows one game or one position, from anywhere: **A** on any board of the program (a game of the
*Live* tab's mosaic or the one followed, the board of the *Games* tab, of the *Openings* tab, of an opening of a
book) opens its position here and analyses it; on the Board page, A starts and stops the analysis. *Back to the
tournament* (or *Back*) returns to the page it came from.

- **Open a PGN…** reads any PGN file (a file can also be dropped on the window), in the background with a progress
  bar: a file of millions of games takes a few seconds, and its games are listed in a tab beside the moves, filtered
  by what is typed (every word found in the players, the event, the site, the date, the round, the opening, the
  ECO code or the result). A click shows a game. The evaluations of its moves draw the curve under them: the
  cutechess and fastchess comments (`{+0.35/18 1.2s}`) and the `[%eval 0.35]` of lichess and ChessBase (from
  White's side, `#-3` for a mate) are read. A game whose FEN tag cannot be played is refused, and the moves of a
  game stop at the first that cannot be read.
- **Paste** (Ctrl+V on the page) reads the clipboard: a FEN, or the four fields of an EPD line; a PGN of one game or
  more (listed like a file's); a list of moves from the initial position, in SAN with or without numbers
  (`1. e4 e5 2. Nf3`) or in UCI (`e2e4 e7e5`). A position that cannot be played is refused with the reason (a king
  missing, a pawn on the first rank, the side not to move in check). Castling rights whose king or rook is not on
  its usual square, or written as files (`HAha`), make it a Chess960 position. A `.fen` or `.epd` file can be dropped
  too.
- **Edit the position** sets up a position on the board: a click puts the piece chosen (a second click takes it
  away), a right click takes it away; the side to move, the move number, the castling rights (offered only when the
  king and the rook are where they need to be), the en passant square (only behind a pawn that just moved two
  squares) and Chess960. The FEN below follows the board, and a FEN typed there sets it. *Done* is offered once the
  position can be played, and puts it on the board.
- *Copy PGN* copies the game shown, with its tags and moves (the engines' scores as comments when it has them).
- **The board**: the buttons under it and ← → Home End go through the moves, and F (or the last button) turns it
  over; a click on a piece and on a square plays a move (a pawn that promotes asks for its piece). A move other than
  the game's next one takes the place of the moves after it, and *Back to the game* brings the game back as it came.
  *Copy FEN* copies the position.
- **The analysis**: an engine of the library searches the position shown and follows the board: depth, nodes,
  speed, the chances of a win, draw and loss when the engine gives them (`UCI_ShowWDL`), and several lines (MultiPV),
  their first moves drawn as arrows, the best one darker. Scores are White's. A click on a line plays its first
  move. Its engine, *Threads*, *Hash* (0: the engine's own) and *Lines* are the analysis's own settings: kept for
  the next time, they change neither the engine of the library nor any tournament. The library's other option
  values are sent as a game sends them. The analysis waits while another page is shown, and goes on when the Board
  page comes back; *Stop the analysis* (or A) stops it, and a dot on the rail's *Board* button tells when it runs.
- **Analyse the game** goes through every position of the game shown with the analysis engine (its threads and
  hash, one line), for the time per move or the depth chosen beside the button, in the background: the progress
  shows above the moves, *Cancel* stops it, and the analysis of the board waits meanwhile. Each move is judged by
  the winning chances its mover lost, from the engine's scores before and after it (lichess' measure: 50 % at 0.00,
  about 59 % at +1.00): 10 points lost is an inaccuracy (`?!`), 20 a mistake (`?`), 30 a blunder (`??`), marked
  after the move in the list; the move the engine would have played is never marked. The tooltip of a marked move
  gives what it lost and the engine's move. A line counts them for each player, and the analysis's curve (orange)
  is drawn over the engines' own (blue). The analysis is kept with the game (in `analyses/` in the profile): the same
  game, opened again from anywhere, shows it at once.
- **Play a game…** plays a game from the position on the board between two engines of the library (*White* and
  *Black*, `<->` swaps them with their clocks), with a clock for each (minutes and an increment in seconds; *The same
  for both*, or one each for time odds), and, if ticked, a second game with the colours reversed. The engines are
  started with the library's option values, a WinBoard engine through the adapter. The board follows the game as it
  is played (going back in it stops following until its end is reached again, or *Back to the current move* is
  clicked), with the clocks, the side to move's search and *Stop*, which ends the game without a result. A game ends
  by mate, stalemate, insufficient material, the 50-move rule, a threefold repetition, an illegal move, an engine
  that stops, or a loss on time (the clock given 0.1 s for the move to come through). Each game is appended, as it ends, to a PGN file in `games/` in the
  profile (named after the date and the engines, with a `Termination` tag), which *Open the games* lists on the
  board. The games are played by fastchess-gui's own engine client, apart from any tournament: they run while one
  plays, without touching it. Their threads (the larger of the two engines', as they take turns) count in the core
  budget; when the running tournament's games and the analysis already fill the machine, the card says so and offers
  *Play anyway* instead of *Play*. The analysis stops when a game starts, as it would compete with its engines.
- **Its cores**: the analysis's threads count in the core budget, shown under it with the machine's threads. While a
  tournament runs, the analysis competes with its games: the page says so, with the threads the games use. Its
  engine is not one of the tournament's processes: the end of a tournament does not end it.

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

**Tournaments → New tournament** opens the setup: a card of folded rows saying how the games are played, the
participants with the engine library beside them, and a live summary on the right (number of games, games per
engine, estimated duration, openings, common settings, time odds). The setup of the last tournament is kept, so
testing two versions of one engine again is: choose A and B, **Start**.

The summary also gives the memory the hash tables can take: two engine processes per game played at the same time,
and the engines waiting for their next game, each with its `Hash`. Above the free memory it warns (the system will
swap), naming the engine with the largest `Hash`; above the memory installed the tournament does not start. `--run`
says the same. When the memory budget of **Settings** lets fewer games play at once than the concurrency, the summary
says how many (each game takes the `Hash` of its two engines and what their processes take besides), and so does the
*Concurrency* row; the same for the games with an engine that uses a GPU, against the *GPUs*. It also names the engines whose program changed since their last tournament (a new build), with
*What changed* when the engine's card lists new defaults or options (see *Engines*).

What would go wrong is said with the engine, the resource and the fix, a button under the line (the command palette
lists the same fixes):

- a program or a working directory not found: *Open the engine* (its card on the Engines page), *Locate…*; an
  opening book not found: *Locate…*;
- an engine whose own `Threads` (its setting, else its default: some engines take every core) is above half the
  hardware threads, or above the threads each game has with the games played at once (the physical cores with a
  clock): *Lower Threads to N* sets the engine's option in the library, *Open the engine*. `Threads` given by the
  setup beyond the machine get *Lower Threads* where they are given (the participant's cell, the common settings).
  The participants list marks such an engine with a *!*, the match's engines are flagged under them;
- an option value an engine refuses (out of its range, not one of its choices, an option it no longer has):
  fastchess does not send it, and the engine plays with its default;
- the Syzygy tablebases the tournament uses: the adjudication's folders and what each engine's `SyzygyPath` points
  to (several folders separated by `;` on Windows, `:` elsewhere). For the pieces used (the adjudication's *Max
  pieces*, else the largest tables found), every WDL file (`.rtbw`) must be there, and no file cut short (a Syzygy
  file is 16 bytes more than a multiple of 64 long; engines leave out a file of another size, and some stop when they
  meet it): *Syzygy 6 pieces: 14 files missing in D:\TB (KRPPvKR.rtbw…)*, *Adjudication up to 6 pieces, but only
  5-piece files found*, *SyzygyPath of Alpha points to a folder with no tablebase files*, with *Open the folder* and
  *Lower to 5 pieces*; the tooltip lists the files. These are warnings: the adjudication and the engines do without
  the missing files. Only an adjudication without a folder, or whose folders hold no table, stops the tournament at
  its start (fastchess refuses it): *Locate…* chooses another folder. The folders are read in the background, and
  again every few seconds while the summary is shown.

A start refused names the same problems, with their fixes and *Open the log*.

**Profiles.** The first five rows, *Format*, *Time control*, *Openings*, *Adjudication* and *Engine settings*, are
each a named profile: its list chooses one, the line beside it says what it plays ("40 moves in 15 min, repeated",
"draw 40/8/10 cp · TB 6 pieces"; its tooltip gives the same as cutechess and fastchess write it, `tc=40/900`,
`-draw movenumber=40 movecount=8 score=10`), and ✎, or a click on that line, opens the details in place under it.
A change made for this tournament only puts a dot beside the profile, and its list and its details offer *Update*
(the profile takes the change, for every tournament and template that uses it), *Revert* and *Save as profile…*
(under a new name). The program comes with presets:

- time controls: 10+0.1 and 60+0.6 (fishtest's short and long tests), 2+1, 40/4, 40/15 and 40/20 (40 moves in 4,
  15 or 20 minutes, repeated, as rating lists play them), and 200k nodes per move;
- formats: SPRT gainer [0, 3] and non-regression [-5, 0], a match of 1000 games, a round robin, a gauntlet, a Swiss
  of 9 rounds, a knockout and a team match;
- adjudications: none, a standard one (a draw from move 40 after 8 moves within 10 cp, a loss after 3 moves at
  600 cp) and the same with the tablebases up to 6 pieces;
- engine settings: each engine's own, or Hash 16, 64 or 256 MB with 1 thread and the engines' own books off;
- openings: no book, or Chess960's 960 positions.

A profile of yours saved under a preset's name takes its place; deleting it (from its list, after a confirmation)
brings the preset back, and the setups and templates that used it keep its values. Profiles are kept in
`profiles.json` in the profile folder, and the command palette (Ctrl+K) lists them: choosing one applies it to the
setup. The palette also opens each row of the setup by its title, and loads or saves templates. The Syzygy directories are in no profile: they belong to this computer, and the adjudication and the engines
share them. The *Concurrency* and *PGN and misc* rows hold what is this tournament's only.

**Template** and **Name**, beside *Start*: a template is the whole setup, a profile for each section plus what no
profile holds (the name, the participants, the concurrency, the PGN files, the broadcast). The menu loads one or
saves the setup as one (*Save the setup as a template…*, which asks before replacing a template of the same
name), and the command palette loads them too. A section that played as its profile when the template was saved
follows that profile, so updating a profile updates every template that uses it; a section changed for that
tournament keeps its own values. Templates saved by earlier versions load as they were, and their sections get the
profiles they play like. An empty name is made from the engines.

- **Format**: the tooltip of each format says what it is.
  - *Match*: two engines, A (the engine under test; every statistic is from its point of view) and B (the
    baseline). A fixed number of games, until you stop it, or an SPRT: elo0, elo1, alpha, beta, model
    (`normalized` uses nElo bounds like fishtest and OpenBench, `logistic` classic Elo, `bayesian` BayesElo) and an
    optional maximum number of games. The SPRT gainer and non-regression profiles give the usual bounds, and the
    line under the bounds tells how many games the test is likely to last. *Calculator…* draws, as fishtest's
    SPRT calculator does, the probability that H1 is accepted and the expected number of games against the true
    Elo difference, from an expected draw ratio and the RMS bias of the openings; *Use these bounds* copies the
    bounds it was given back to the setup.
    *Elo bias* (0 by default) is added to both bounds, to test against a version known to be stronger or weaker by
    that much: [0, 3] with a bias of 5 gives the SPRT [5, 8], which the summary, the overview and the headless mode
    write `SPRT [0, 3] + 5`; the calculator and the line under the bounds take it into account. *Min games* (0, none,
    by default) holds the decision until that many games are played, the LLR still computed and shown, to guard a
    short test against an early lucky stop: an LLR beyond a bound before then is said under the LLR bar and the
    match goes on; once the games are there, the next pair that ends with the LLR beyond a bound ends the match. A
    maximum of games below the minimum ends the match without a decision.
    This is the quickest way to compare two versions of an engine, and nothing else is needed.
  - *Round robin*: every engine plays every other one, *Cycles* times, each time two games with colours reversed
    (what cutechess calls rounds, of 2 games per encounter). The rounds follow Berger tables, so every engine plays
    once per round.
  - *Gauntlet*: the first *Tested engines* (the seeds) play every other one, the others do not play each other.
  - *Swiss*: a number of *Rounds*. Each round is two games, colours reversed, against an engine with a similar
    score; nobody meets the same opponent twice when it can be avoided. With an odd number of engines, the lowest
    ranked engine without a bye yet gets one, worth a point.
  - *Knockout*: engines are seeded in their list order, the top seeds get the byes when the field is not a power of
    two, and the first two seeds can only meet in the final. A match is *Game pairs per match* pairs of games; a
    tied match gets one more pair at a time, up to ten, after which the higher seed goes through.
  - *Team match*: two teams of engines, to compare one family of engines, or a set of versions, with another as a
    whole. *Pairings*: *Everyone against everyone*, every engine of a team against every engine of the other team,
    one opponent per round (a Scheveningen system; the teams may differ in size), or *Board by board*, the first
    engine of each team against the first of the other, the second against the second and so on (teams of the
    same size). Each pairing is played *Cycles* times, each time two games on the same opening, colours reversed;
    the colours of the first game alternate from board to board and from one cycle to the next. *Team A* and
    *Team B* name the teams ("Team A" and "Team B" without a name); beside them, how many engines each has. The
    teams are made in the participants list: the upper half of the list is team A, the lower half team B (team A
    gets the odd one), until engines are put in a team.
- **Participants**, with the **Library** beside them (under them in a narrow window). Each participant is a line:
  its place (a star for the seeds of a gauntlet), its name, its reference Elo, what it plays differently from the
  common settings ("Hash 512 MB · 80 % of the time") and ⋯, highlighted when it has settings of its own. ⋯, a right
  click, or the Menu key opens them: its own `Hash`, `Threads`, fixed nodes or depth, time odds (a percentage of the
  time control, for a clock), its own *Book*, *This engine once more, with another book*, the moves and *Remove*.
  The list order is the seeding: drag participants to reorder them (seeds first), or press Ctrl+Up or Ctrl+Down to
  send them to the top or the bottom, Alt+Up or Alt+Down to move them by one. Ctrl or Shift click selects several;
  Delete (or *Remove*) removes them.

  In a team match the place is the team and the board, A1, A2… for team A, B1, B2… for team B, in the list's order
  (board by board, A1 plays B1). The ⋯ menu puts the participant in a team; *Team A* and *Team B* under the list
  put the participants selected in one, and *Halves* makes the teams again from the halves of the list (the command
  palette offers the same). An engine added once teams are chosen joins the smaller one. The summary says how many
  engines each team has, and a start is refused, with the reason, when a team is empty or, board by board, when the
  teams differ in size.

  The library lists the active engines with their reference Elo, sorted by name or by Elo (click the column), a `!`
  on those with a problem (program missing or rebuilt, last check failed: the tooltip says which) and a ✓ on those
  that play already. The filter searches the names, the names the engines report, their authors, programs, notes
  and groups (Ctrl+F gives it the keyboard; Enter in it adds the engines selected, or the first one shown); a chip
  per group shows only that group, and *Add all shown* adds the whole group. Double-click an engine, or select
  several with Ctrl or Shift (Ctrl+A: all those shown) and press Enter, to add them; or drag them to a place among
  the participants. *Suggest…* selects the N active engines that work and whose reference Elo is closest to the
  first participant's (the engine under test of a gauntlet), 150 at most away by default; Enter adds them. For a
  match, the library fills A, then B (B is replaced when both are chosen), and an engine dragged onto A or B takes
  its place.

  *Book*: an opening book of the participant's own, like Cute Chess' `book=`/`bookdepth=`: a Polyglot (`.bin`) or
  PGN book of the Books page and how deep it is followed, in plies from the start of the game, the tournament's
  opening included (*all*: as long as the book has a move; for a PGN book, its games' first 40 plies). A PGN book is
  first turned into a Polyglot book: each game's moves up to that depth, each move weighted by the number of games
  that play it; games with an illegal move are left out and counted. This runs in the background as soon as the
  book is chosen (progress on the *Book* button, in its popup and in the summary) and is kept in the profile's
  `book-cache` folder until the file or the depth changes; a start that comes before the end waits for it. A book
  that cannot be prepared shows why in its popup, with *Try again*; each start also tries again. A tournament plays
  from the book prepared when it was started or resumed, even if the PGN file changes meanwhile. While the position
  is in that book, the participant plays the book's move at once, chosen by the book's weights as the *Randomness*
  of the openings makes them (also in the *Book* popup, the same for every book of the tournament: see *Openings*);
  then its engine takes over. The tournament's openings, if any, are played first, by every engine; EPD books only
  give those and are greyed in the list. To compare books with one engine, choose *This engine once more, with
  another book* in its ⋯ menu: each copy plays as `<engine> (<book>)`, so its results and rating stay apart. In a
  match the *Book* buttons sit next to A and B, and an engine can play itself with two books. Book moves are marked
  `{book}` in the PGN; hovering a name in the standings shows its book. WinBoard engines can have one too.
- **Engine settings**: *Hash* and *Threads* forced on every engine, for a match too (0 = each engine's own; a
  participant's own, from its ⋯ menu, comes first); *Syzygy tablebases for the engines* gives the tablebase
  directories (the same as the adjudication's) to every engine with a `SyzygyPath` option, and *Engines' own books
  off* sets `OwnBook` to false where it exists, so that every game is played from the tournament's openings. Both
  are off by default.
- **Time control**: base time and increment in seconds, optional moves per period, and a margin before a game is
  lost on time; a fixed time per move (`go movetime`, with the same margin); a fixed number of nodes per move; or a
  fixed depth in plies (`go depth`), which no engine can lose on time.
- **Concurrency**: the number of games played at the same time. In automatic mode it is measured at startup:
  - fixed nodes or depth: one game per hardware thread, since Hyper-Threading does not change the moves played;
  - with a clock: physical cores − 1, so that engines do not slow each other down.

  The row and the line under the field say which rule chose the number. *CPU affinity* (off by default; not on
  macOS, which has none) pins every game to one processor, shared by its two engines since they never think at the
  same time. fastchess only supports it for single-threaded engines, so it is turned off automatically when an
  engine has `Threads` > 1. A warning appears when concurrency × threads exceeds the hardware threads (or, with a
  clock, the physical cores).
- **Openings**: an EPD, PGN or Polyglot (`.bin`) book (an opening suite), random or sequential order, the number of
  plies kept from a PGN book, the starting index and the random seed (0 = a new order for every tournament). Every
  opening is played twice, with colours reversed. A Polyglot book is followed from the initial position to a depth
  (8 plies unless set), leaving out the moves that weigh less than a tenth of the best one of their position; the
  positions reached are written once to `<book>-<N>plies.epd` in the tournament folder and played like an EPD book.
  In random order they are written in the order they are drawn, each once: a line comes the sooner the likelier it
  is, by the weights of its moves, so that a tournament that plays only part of the book plays its main lines
  first; the seed draws the same order again. In sequential order they keep the order of the book. *Randomness*
  (−100 to 100, 0 by default) says how the weights count, as Polyglot and Cute Chess choose a book move: 0 the
  weights as they are, −100 the best move of each position only (one line, unless several share the best weight),
  100 every move alike (the rare moves come back); in between, −50 squares the weights and 50 takes their square
  roots. It chooses the moves of the participants' own books too, and is shown for them even with an EPD or PGN
  book; the summary says it, and the line under the standings copies gives the book with its depth and, for a
  Polyglot book, this randomness.
  In a round robin, a gauntlet, a Swiss or a team match, *The same opening for every pair of a round* gives every
  game pair of a round the same opening (board by board, a cycle is one round: every board plays the same opening), the next of the book for each round, so that no engine gets easier openings than the
  others of its round (an engine added later catches up in rounds of its own, each with its opening).

  *Flag unbalanced openings* (off by default) shows, as the games come, the openings that decide the game by
  themselves: a game is flagged when it ends if both engines saw the same side ahead by at least the score (200 cp
  by default) for each of their first moves after the book (4 by default; a participant's own book moves count as
  book moves). It reads the engines' scores, so an engine that gives none flags nothing; the results are not
  changed. The *Games* tab filters such games (*Unbalanced opening*), the *Openings* tab marks their openings with a
  `!` and counts them, and the overview counts the games. *Play the pair again from another opening* voids such a
  pair once its two games are played (they stay in the PGN files and the journal, as any voided game) and plays it
  again from an opening at the end of the book (in the book's playing order), which the tournament would play last,
  so that the openings of the pairs to come do not move; at most three times for one pair, after which the pair
  counts, flagged. The console and the overview say how many openings were replaced.
- **Chess960** (Fischer random), in the openings: without a book, the 960 starting positions in random order
  (written once to `chess960.epd` in the tournaments folder); with a book, its positions. Every engine must play
  Chess960: UCI engines with the `UCI_Chess960` option, WinBoard engines with the `fischerandom` variant. The summary
  names those that do not, and such a tournament does not start.
- **Adjudication**: draw (from a move number, for N consecutive moves, under a score), resign (N consecutive moves
  above a score, optionally both engines agreeing), a maximum number of moves, and Syzygy tablebases.
- **PGN and misc**: event name (its tooltip names the two PGN files every game is written to), *Also append every
  game to*: a PGN file of yours that every tournament adds its games to, like cutechess' `-pgnout` (never
  overwritten, in the ChessBase / Lichess or cutechess comment style), in schedule order: the two games of an
  opening side by side, rounds in order, whatever order they end in. A game waits there until those before it are
  written; a resume carries on; when the file cannot be written (locked, moved), the games wait and the tournament
  goes on, with a warning in the overview. A crash or a power cut while games are appended never writes them twice:
  the resume checks the file and completes a write cut short, and never removes anything from it. Extra fields in
  the move comments (nodes, seldepth, nps, pv, tbhits, and the decision time `dt=`: when the engine settled on the
  move it played; `wdl=W/D/L` is always written for the engines that report it), on by default since the charts of
  the *Games* tab read them (pv makes the PGN noticeably larger), restarting engines that crash, *Stop the tournament when an engine cannot start* (off by default: see *An
  engine that cannot start*), keeping the engine log of every game (it is always kept for the games that end badly), and *Check the mate PVs*: a mate score's PV
  must have the right length and end in mate. Every PV is checked for illegal moves, moves after the end of the game
  and a best move that is not its first move; the moves with a wrong PV are marked in the Games tab and written in
  the log.

Everything on the page can be done from the keyboard: Tab and the arrows move between the rows, the lists and the
buttons, Enter or Space opens a row or a list, and the keys above work in the participants and the library.
**Start** creates the tournament and plays it; **Add to the queue** keeps it for later (see *Queue*).

## Running a tournament

A running or opened tournament has a workspace with a header (name, progress, *Pause*, *Stop*, *Layout*) and
panels, shown by default as tabs:

| Tab | Contents |
|---|---|
| Overview | Match: Elo and nElo with their 95 % confidence interval, LOS, score, pentanomial counts, SPRT bounds and LLR; *Statistics* (folded) gives fishtest's numbers from the games and from the pairs: score, Elo, nElo and BayesElo with DrawElo, standard deviation per game, LLR of both, the variance ratio of pairs to single games, the RMS bias of the openings, the kinds of balanced pairs and the pairs won over lost, which *Copy* includes; and the charts (Elo and LLR over time, game lengths, how games ended, depth, NPS, time per move and decision time). Tournament: the standings (with *TF*, each engine's losses on time, in orange as soon as there is one; a *!* beside an engine that crashed or played an illegal move), progress by cycle or round, games per hour and time left, the latest results. Both count the games from unbalanced openings, and the openings replaced, when the setup looks for them, with *Show* for those games. A finished tournament adds a *Submission checklist*: all games played, every pair with both colours, no duplicate games, no crashes, time losses or illegal moves (with buttons that open those games), the hash of each engine (shown, not judged), ponder off, the opening book, the tablebases |
| Standings | Points, games, losses on time (*TF*), wins/draws/losses, ratings with their confidence interval, Sonneborn-Berger (Buchholz for a Swiss), byes. *Table* lists them one engine per row; *Crosstable* (round robins, up to 200 engines) shows every pairing. *Order:* by points, score % or seed order (which keeps the columns of a gauntlet's crosstable in place). A click on an engine lists its opponents below: games, with White, with Black, + = −, points and %, with the pairs of which only one game was played marked; a gauntlet opens with its seed selected. *Copy* as text, or as BBCode for a forum post (with a line giving the format, time control, games and book), and in a gauntlet *Gauntlet table as text* or *as BBCode, for a forum* (the seed, then its opponents by their own points, each with points, %, Elo ±, W-L-D); *Export HTML…*; all with *TF* like the headless mode's standings, in the order chosen, and ending with a line giving the games played of those planned, the draw rate, time control, book (with the plies kept of a PGN book; for a Polyglot book its depth and how its moves were chosen, e.g. `book:Perfect2023.bin (8 plies, moves by weight)`) and event; the copies then sign with *fastchess-gui* and its version, and the gauntlet table is ruled with = rather than -, so that a table posted on a forum is told apart from those of other GUIs |
| Teams | Team match only, in the *Standings* tab (where it opens) and atop the *Overview*: the teams' score, "Lynx 17½ – 18½ Otter", with the games played, team A's score %, the Elo difference that score stands for with its 95 % interval and the LOS, counted on the pairs of games as the statistics of a match are (pentanomial); then each team's engines with their games, points, %, rating and the points each scored against each engine of the other team (B1, B2… as columns, the name at the cursor): the crosstable of the two teams. *Table* and *Crosstable* show the engines as in any tournament. *Copy* and *Export HTML…* give the teams' scores and both teams' engines against each other, with the line under them |
| Bracket | Knockout only: the bracket, round by round, with the score of every match |
| Schedule | Every game pair by cycle or round, with its state and the opening it plays (once started, or once its round started when the round shares one); editing (see below) |
| Broadcast | Live files, the TLCS server and the web page (see below) |
| Live | The games in progress, as a mosaic of boards or one game in focus with the clocks, the running search of each engine (depth, score, nodes, nps, PV in SAN), the move list and the evaluation curve (or, chosen above it, each engine's time per move with its decision time, depth with seldepth, nodes, speed, tablebase hits, or how many plies its PV repeats of what the other engine's PV predicted), with the win/draw/loss shares (`UCI_ShowWDL`, switched on for the engines that have it when they are added) after each search line and as bands under the curve, White's wins at the bottom. Under the board: the 50-move counter (plies since the last capture or pawn move, out of 100), the material difference with the pieces one side has more of, and the pieces left, marked *TB* when the tablebase adjudication can end the game; *Details under the boards* adds them to the mosaic. Under them, the buttons through the moves (see *The window*): the game in focus, or a detached board, can be stepped back while it goes on (also with ← → Home End, a click on a move of the list or on the curve), the earlier position shown with its move marked in the list and on the curve; *Back to the current move* (or End, or a click on the last move) makes it follow the game again, as it does at each new game. *Arrows* (on by default) draws the move the side to move is thinking of in its engine's colour and the reply it expects, lighter, in the opponent's colour, on the board in focus and the detached ones (not the mosaic), when it shows the current move. In a gauntlet, *Seed at the bottom* (on by default) shows each new game from the seed's side; flipping the board (F, or its button) still turns it over until the next game. *Detach* opens a board in its own window. A right click on a board ends its game with a result (see *Changing a running tournament*). *Big screen* shows the game followed filling the window (see below) |
| Games | The finished games with their opening (ECO code and name), filterable by result (wins, draws, losses, losses on time, crashes and illegal moves, adjudications, wrong PVs, and the games of an opening flagged unbalanced when the setup looks for them), by what the evaluations tell (the engines disagree by N pawns for 3 moves each in a row, an engine's evaluation swings by N pawns from one of its moves to the next, the loser thought it was N pawns better; the game opens at that moment) and by opening (an ECO code or its beginning, a range such as `B20-B99`, or words of the name), with a replay board (the buttons under it, ← → Home End and F, or click a move or the curve; the same details under it and the same charts as in *Live*, from the fields the PGN was written with; a chart whose field the tournament did not write says so, with a button to the PGN options of the setup; with the win, draw and loss shares of the engines that report them under the evaluation curve) and the export of one, all or the filtered games (*Export all* proposes `<name>.pgn` next to the tournament's folder; the tournament's own PGN files are refused as a target). *Export for a rating list…* writes the games as rating list testers (e.g. CCRL) send them: `[Tester YYYY-MM-DD] Event (hash NMB) (book X) (egtb N-man).pgn`, a zip too if asked; the players named as the lists write them, `<engine> 64-bit 4CPU` (editable for each participant), the rounds numbered again, the `Event` and `Site` tags set, the voided games and the duplicates left out, and those lost by a crash or a stall too unless unticked; the `PlyCount` of a game lost by an illegal move counts only the moves written. The tester and the site are remembered. *Engine log* opens the log of a game that has one (see *Files*). A right click on a game plays it again, voids it or sets its result (see *Changing a running tournament*) |
| Openings | Results by starting position: its game pairs, White's score, the draws, and how many pairs White won twice, Black won twice (the opening favours a colour) or one engine won twice (it tells the engines apart), with the average length. Sorted by any column, filtered to the unbalanced, the drawn or the decisive ones, or to those *Flagged unbalanced during the games* (see *Openings* in the setup: marked `!`, their number beside the count, with the openings replaced); *Export as EPD…* writes the positions shown, to keep them or to clean a book, *Without the unbalanced* (on by default, shown once an opening is flagged) leaving out the flagged ones. A click shows the position and its games |

**Panels and layouts**: every tab is a panel. Drag one by its tab to dock it on a side of another panel or as a tab
next to it, or drop it elsewhere to float it (also onto a second screen on Windows and Linux/X11, not on Wayland nor
with software rendering).
The game followed in *Live* comes apart in *Board*, *Players*, *Searches*, *Moves* and *Chart* panels; the parts
that have no panel open stay beside the mosaic in *Live*, and detached boards dock like the other panels. Close a
panel with the x on its tab; bring it back from *Layout* > *Panels*, or with *Show …* in the command palette, and it
comes back docked. *Layout* also offers ready-made layouts (*Tabs*, as by default; *Follow a game*: the board on the
left, the players, searches and chart on the right, the standings under them; *Large tournament*: the mosaic beside
the standings; *Broadcast*: a large board with the players, searches and standings), *Save the layout as…* (your
own, by name, kept in `layouts.json` in the profile), *Delete* and *Default layout*. A panel taken out of the
workspace stays on screen on the other pages; the arrangement is kept between sessions. F1 and the `?` open this
guide at the panel last clicked.

**Big screen**: for a television, a club or a stream, *Big screen* (in the *Live* tab, on a detached board, or in
the command palette) shows one game filling the window, light on black: the players' names and clocks in large
type (the side to move and its running clock in yellow, a clock under ten seconds in red), the evaluation bar, the
evaluation, depth and speed of the search, the last move framed on the board and written large, and the moves in
large type, the last one marked; the result over the board once the game is over. Its text follows the size of
the window and has a size of its own (*A-* / *A+*, or the + and - keys). It shows the game followed, or switches
between the games in progress every N seconds (*Every game in turn*), or follows *The most interesting* one: the
closest to level, the longest of those as close, kept while it stays so. The controls show for three seconds when
the mouse moves (the pointer hides with them); ← and → change the game, F11 makes the window full screen, Esc
leaves. A detached board moved to another screen (the television) shows it in its own window, F11 making that
window full screen while the main window goes on; docked in the main window, the main window shows it.

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

A tournament stopped by an engine says which one in its header and on the home page, under *Needs action*:
*Interrupted: Alpha could not start: …* (after its last try), or *Alpha crashed or stopped responding (game 12)*
(without *Restart an engine that crashes*), with *Open the engine* and *Open the log*.

**Every tournament resumes exactly where it stopped**, after a stop, a closed window, a crash or a power cut: no
game is lost and none is played twice, and a half-played game pair is completed with the same opening and
colours. *Resume* on the home page continues it, with the statistics of the games already played: results,
and the engines' depth, time per move and NPS read back from the move comments (NPS only when the PGN records
it, *nps* in *PGN and misc*). When the program ended while a tournament was running (a crash, a power cut, a
reboot, or quitting while it ran), the next launch resumes it by itself, and the queue goes on after it: *Resume at
launch the tournament that was running*, in **Settings**, on by default. A tournament paused or stopped waits for
you, and `--autostart` starts the saved setup instead. A participant whose program was rebuilt or went missing
since it joined is named in the header of the resumed tournament, with *Continue* and *Pause*: its next games are
played by the program as it is now, with the options the tournament gave it.

### Several tournaments at once

Several tournaments can play side by side: a gauntlet beside an SPRT, two gauntlets at a time. *Start* on the *New
tournament* page starts another one while some play; *Resume* resumes one beside them. Each tournament plays in a
process of its own (`fastchess-gui --run <folder>` driven by the window), so a crash takes down that tournament only,
and the window, its engine checks, quick tests and test suites no longer wait for a tournament to end.

The workspace has **a tab per open tournament**, above its header: a dot while it plays (a pause sign while paused),
its status at the cursor, a cross to close one that does not play. The panels show the tab chosen; the command
palette lists the others (*Show …*, *Pause*, *Resume*). A finished tournament opened from the list takes the tab of
one only looked at. The home page has a card for each tournament playing, with *Open*, *Pause*, *Stop* and its
**weight**, the cores its games use of those it may use, and what its next game waits for; under the cards, a bar
shares out the cores between them. The *Log* shows the lines of one tournament, of the window, or all (each line of a
tournament carries its name); a tournament's lines are also kept in `fastchess-gui.log` in its folder.

**Sharing the machine**: the tournaments playing share a budget of cores, set in **Settings** (*Cores for the
tournaments*: the physical cores less one by default). Each gets one game's worth first while the budget allows, the
heaviest weight first; the rest goes by weight (a tournament of weight 2 gets twice the cores of one of weight 1), none
above its own concurrency, what one cannot take going to the others. A game counts as the `Threads` of the larger of
its two engines: one thinks while the other waits. A paused tournament keeps only what its running games and the
second games of their pairs need. A smaller share never interrupts a game: the tournament plays fewer games from its
next one. A tournament alone plays as many games as its concurrency says, as before.

**Memory**: a game starts only when the `Hash` of its two engines, and what each engine process takes besides (64 MB,
and 8 MB per thread), fit in what the games playing leave of the memory budget (*Memory for the engines* in
**Settings**, 75 % of the installed memory by default), shared between the tournaments. The setup summary says how
many games the budget lets play at once, and the home page's *Health* card when games wait for memory.

**GPUs**: an engine marked *Uses a GPU* on its card (a network engine such as Lc0) counts against the *GPUs* of
**Settings**: no more such engines play at once than there are GPUs, in a tournament and across tournaments, while the
other games play beside them. The setup summary says how many games with a GPU engine that allows, and warns when no
GPU is counted, or when a game between two GPU engines needs more GPUs than the machine has (it then plays alone).

**Keeping them running**: closing the window while tournaments play asks what to do. *Keep them running and quit* (the
default) leaves each one playing in its process; the window shows them again, as they stand, at its next launch. *Stop
and quit* stops them, each to be resumed exactly where it stopped. The quick test stops either way. A tournament whose
process is gone by the next launch (a reboot, a crash) is resumed as any interrupted tournament. The broadcast of the
*Broadcast* tab runs in the window, for the tournament shown: for a broadcast that goes on without the window, start
the tournament from a terminal with the broadcast options (see *Headless mode*).

### An engine that cannot start

A night left alone does not stop on one engine. An engine that does not start (a program moved or rebuilt, a
network file missing, a drive gone) is tried again three times, 10 s apart; then its games are set aside and the
other games go on. It is tried again every 5 minutes, one game at a time, and its games come back as soon as it
starts. The tournament header and the home page (under *Needs action*) say which engine, the error, and offer:

- **Try again**: at once, rather than at the next try (after fixing the program, say).
- **Withdraw…** and **Replace…** (round robins, gauntlets and team matches): the Schedule tab's change panel, set
  for that engine,
  with the impact shown before anything changes.
- **Engine log**: `logs/start-<engine>.log` in the tournament folder, what was sent to the engines of its last try
  and what they answered, their error output included (often the reason: a library or a network file not found).

The log and the desktop notification say when an engine is set aside. The tournament stops only when nothing is
left to play without the engines set aside, after one last try of each; its status and the notification at the end
say so (*Stopped: no game is left to play without …*), and the queue goes on with the next tournament. *Resume*
tries every engine anew. A pause holds the tries. *Stop the tournament when an engine cannot start*, in the
setup's *PGN and misc*, stops it at once instead, once the tries are used up.

### Changing a running tournament

The *Schedule* tab's **Change the tournament** panel works without stopping anything: running games go on and no
played game is lost. Each change shows its impact before it is applied.

- **Add an engine**: it plays the cycles already started (*Catch up*) or joins from the next cycle.
- **Withdraw an engine**: its remaining games are dropped; its games played so far count or not.
- **Replace an engine**: its games stop counting and are played again by the new engine.
- **Number of cycles**: more or fewer.
- **Play games again**: every game of an engine, or the losses on time and crashes.
- Right click on a game (here, or in the *Games* tab): play it again, void it (no replay), or set its result.

In a team match an engine added joins the team chosen and plays every engine of the other team; board by board an
engine cannot be added (its board would have no opponent): replace one instead. A replacement takes the team and the
board of the engine it replaces; more cycles pair the engines still playing (board by board, on the boards of the
first cycle). The games a withdrawn or replaced engine played still count for its team, unless they are voided.

**Set the result** decides a game the program could not decide well: a hang it did not catch, a won ending the
engines keep shuffling, a game that ended for a wrong reason. Choose 1-0, ½-½ or 0-1 and give a reason; the points
it moves are shown before *Set the result* applies it. The standings, the crosstable, the ratings and the
submission checklist count the new result (an adjudication: a loss on time set to a draw no longer counts as one);
the *Games* tab marks it with a `*`, its tooltip giving the reason and how the game ended as played, and *Back to
the result as played* takes it back. A game being played is ended the same way: right click on its board in the
*Live* tab, *End with result* (also in the command palette for the game followed). The engine on move is stopped,
fastchess ends the game at once and it counts with the result chosen; the engine starts again for its next game.
It needs *Restart an engine that crashes* (setup, *PGN and misc*, on by default).

Voided games and results set by hand stay in the PGN files and the journal as they were played: the PGN files
are only ever appended to. What reads the games applies them: voided games are left out, and a result set by hand
is in every export (*Export game…*, *Export all…*, *Export for a rating list…*, the head-to-head export of the
*Ratings* page, the file of *Also append every game to*) with `[Termination "adjudicated by the operator"]` and the
reason in a comment before the result. **Undo the last change** is offered until the next game starts: nothing has
been played under the change yet, so it can be taken back exactly (games that were already running when it was
made are kept). A result set by hand can be undone as long as it is the last change, games played since or not
(in a knockout, whose next rounds follow from the results, until the next game starts); the *Games* tab offers
*Undo* beside the game too.

### Queue and relaunch

The **Queue** on the home page holds tournaments to play one after the other: when one ends by itself, the next
starts (not after a stop or an error, nor while *Pause all* holds the queue). With *Start when cores are free*, the
next one starts beside those playing as soon as the budget of cores is more than they can take, each at its
concurrency. They can be reordered or removed, and the first started now when nothing runs. The home page tells when
the running tournament ends (*Ends about HH:MM (in …)*) and when the whole queue does, each queued tournament with its
length and, while the lengths before it are known, when it starts; the tournaments whose length cannot be known (an
SPRT without a maximum, nodes or depth per move) are counted apart.

**Relaunch with…** on a finished match (in its header, or under *Last finished* on the home page) plays it again
with another engine A, typically the next version, against the same engine B; the other settings are those of the
*New tournament* page.

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
  game recorded. `state.json`, which the page reads, holds all of it for scripts. The engines' logos (see
  *Appearance*) show beside their names, as small PNG pictures the program makes from them and serves itself; the
  page learns only those of the tournament's engines.
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

A team match shows the teams' score everywhere: in the web page's header and above its standings (with team A's
score %, Elo ± and LOS), in `standings.html` (the teams, then each team's engines against the other team's), in
`tournament.json` and `state.json` (`teams`, `team_elo`, `team_elo_error`, `team_los`), and in the TLCS crosstable, as
a line under the table ("Lynx 17½ - 18½ Otter · 48.6 % · Elo -9.7 ± 31.2"). Its games carry `WhiteTeam` and
`BlackTeam` tags in the PGN files, which the exports keep.

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

No window is opened; the version, then the progress, are printed on the console, with the tries of an engine that
cannot start, the engine set aside (with its engine log) or back, as in the window, and the standings every 20 games
and at the end (a team match: the teams' scores, then each team's engines against the other's), a tournament's
ending with the line of the standings copies (games, draws, time control, book and event). **Ctrl+C** stops
after the
running games and the tournament can be resumed later, from the command line or the GUI. Exit codes: 0 finished,
1 error while playing, 2 nothing to run, 3 stopped before the end (by Ctrl+C, or with nothing left to play without
an engine that cannot start). The memory budget and the GPUs of the profile's **Settings** apply to it too.

`--detach` plays it in the background instead: the command returns at once, and the tournament goes on without a
terminal; the window shows it (it connects to it) when it opens with the same profile, and `--attach` talks with it:

```
fastchess-gui --run FOLDER --detach         # in the background, on a local socket
fastchess-gui --attach FOLDER               # its control channel on the standard input and output
```

The window drives each tournament the same way: `--run FOLDER --control socket` (or `--control stdio`) plays it
without printing, talking JSON lines with the window: the tournament's events (its state, each journal line, the
games in progress move by move, each game recorded, its log, its end) and the window's commands (pause, resume, stop,
changes, the end of a game, its share of the machine with `--allowance THREADS:MEMORY_MB:GPUS:ONE`). The first line
each way is a hello, `{"hello": {"protocol": 1, ...}}`; a script through `--attach` gets the tournament's hello and
events, and may send commands such as `{"c": "pause"}`. One window (or script) at a time talks with a tournament.

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

## Machines

Other computers to run tournaments on: a test box, a server, a second PC. **Settings** > *Machines* lists them. A
machine is reached with SSH: nothing has to be opened on it but its SSH server, and the program keeps nothing secret.

*Add a machine*, then on its card:

- *Name*, *Host* (its name on the network, or its address), *Port* (22) and *User*, the account its tournaments run
  as;
- *Log in with*: the *SSH agent* (ssh-agent on Linux and macOS, Pageant or the *OpenSSH Authentication Agent*
  service on Windows; add your key to it with `ssh-add`), or a *Key file*, RSA or Ed25519 (`~/.ssh/id_ed25519`).
  The passphrase of a key file is asked once a session: it stays in memory and is never written anywhere. The
  machine must know the key: its public half (`id_ed25519.pub`) goes in `~/.ssh/authorized_keys` on the machine
  (`ssh-copy-id user@host` puts it there), or, for an administrator of a Windows machine, in
  `C:\ProgramData\ssh\administrators_authorized_keys`;
- *Folder there*: fastchess-gui's folder on the machine, for the program, its tournaments, books and tablebases,
  relative to the user's home folder (`fastchess-gui` by default) or absolute (`/srv/fcg`, `D:\fcg`);
- *Budget*: the cores and the memory the tournaments on the machine may use at once; 0 takes what the test found,
  every thread but one and 80 % of the memory.

*Test the connection* logs in, then tells the machine's system, threads, memory, the free space where its folder is,
and whether fastchess-gui is installed there, in which version. The first connection shows the machine's host key
(its SHA256 fingerprint) and trusts it from then on (`known_hosts.json` in the profile, the broadcast tunnel's too).
When the key changes later, nothing is sent and the card says so, with *Trust the new key* for a machine that was
reinstalled. Every failure names the machine and what to do: the host that cannot be reached, a key the machine
refuses (and where its public half goes), a passphrase that is not the right one, no agent running.

*Install fastchess-gui* (*Update fastchess-gui* once it is there) copies the program to the machine's folder over
SFTP, then checks it starts there (`fastchess-gui --version`). It copies this program when the machine runs the
same system (Linux x86-64 to Linux x86-64); for another one, a Windows machine from Linux say, download the release
for that system and choose its program on the card (*fastchess-gui for Windows x86_64*). A copy cut by the network
resumes where it stopped.

*Engines on this machine*: the library's engines the machine has, each with its path there (`/opt/engines/stockfish`,
`C:\Engines\sf.exe`). *Check the engines* checks each path: the program is there, can be run, and answers `uci`
(`xboard` for a WinBoard engine) within 10 seconds. The mark beside it, and its tooltip, say what was found: the
engine's name, or why it does not run (missing, not executable, made for another system, silent).

*Books and tablebases*: *Check what the setup uses* compares the opening book and the participants' own books of
the setup with the machine's books folder, by size and SHA-256 (a book of the same name but another content is not
the same book), and the setup's Syzygy files of 6 pieces at most with the machine's tablebases folder, by name and
size. *Push the missing files* copies what is missing or different, in the background, with the time left; each book
is checked by its hash once copied, each table by its size, and a copy cut short resumes. Tables of 7 pieces are
not offered: they take terabytes. A tournament does not start on a machine without its book; tablebases missing
are a warning (the adjudication and the engines do without them).

### A tournament on a machine

In the setup, *Run on* (in *Concurrency*) chooses where the tournament plays: *This computer*, or a machine. *Start*
then prepares it there: fastchess-gui must be installed there in this version (else *Update fastchess-gui*), every
engine must have its path on the machine, and the setup's books must be in its books folder (else the start says
which is missing, and *Push the missing files* puts it there); tablebases missing are only a warning. The tournament
gets its folder in `tournaments/` of the machine's folder, with the engines, books and tablebases at their paths
there, and its process starts there out of the SSH session (`fastchess-gui --run <folder> --detach`): it plays on
whatever happens to the window or the network. The window follows it as a tournament of this computer: Live,
Games, the standings, pause, stop and the changes, through the control channel of its process
(`fastchess-gui --attach <folder>` over SSH).

Its card on the home page says *On <machine>*, and *Reaching <machine>…* while the connection is cut: the window
connects again on its own, a little longer after each failure, and takes up the events where its copy stopped.
The journal and the PGN files are copied back to the tournament's folder here as they grow, every few seconds, so
that Ratings, exports and the submission checklist read them; the machine's folder stays the reference
(`machine.json` here says which machine and folder). Closing the window leaves it playing there; the next launch
shows it again. *Resume* of an interrupted tournament of a machine resumes it there. The machine's budget (its
cores and memory) is shared by its own tournaments, apart from this computer's. One tournament plays on one machine:
its games are not spread over several.

A machine needs an OpenSSH server (Linux, macOS, BSD; Windows 10 1809 and later), with an RSA host key and SFTP,
both there by default. Commands go through `sh` whatever the user's shell is (fish or csh are fine), and through
cmd on Windows, the default shell of its OpenSSH server.

### A Windows machine

Windows 10 (1809 and later) and Windows 11 include an OpenSSH server, not installed by default. In PowerShell, as
administrator:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

The installation opens port 22 in the firewall. Then put your public key in `C:\Users\<user>\.ssh\authorized_keys`,
or, for an account of the Administrators group, in `C:\ProgramData\ssh\administrators_authorized_keys`, which only
Administrators and SYSTEM may read:

```powershell
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"
```

Keep cmd as the server's shell: fastchess-gui speaks cmd, not PowerShell (if a `DefaultShell` value was set under
`HKEY_LOCAL_MACHINE\SOFTWARE\OpenSSH`, remove it). On the machine's card, choose the Windows release's
`fastchess-gui.exe`, then *Install fastchess-gui*.

## Files

Everything lives in the profile folder: `~/.local/share/fastchess/fastchess-gui/` on Linux,
`%APPDATA%\fastchess\fastchess-gui\` on Windows, `~/Library/Application Support/fastchess/fastchess-gui/` on macOS.
Files the program creates are not writable by other users, and `settings.json` is readable by its owner only once
it holds a lichess token. **Settings** shows the profile folder, with *Open the folder*.

**Portable mode.** With a file named `portable` (empty, without extension) beside the program (beside
`fastchess-gui.exe` or `fastchess-gui`, beside `fastchess-gui.app` on macOS), the profile is the folder `data`
beside the program instead, and the default tournaments folder is `data/runs`: nothing is written elsewhere, but in
the folders chosen for tournaments, exports and the like. The paths of what lies in the program's folder (engines,
books, tablebases, test suites, the PGN files of the ratings and the setup, the folders chosen) are kept relative to
it in the profile's files, so the whole folder can be moved, copied to another machine or run from a USB stick,
whatever its drive letter, and its engines are found again. **Settings** says *Portable mode* beside the profile
folder. To carry an existing profile over, copy its files into `data`. `--profile` still chooses another folder,
outside the portable mode. A tournament keeps the engines' paths of when it was created: resume an unfinished one
before moving the folder. On macOS, remove the quarantine first (see the README): otherwise macOS runs the
application from a temporary place, where the `portable` file is not.

- `engines.json`: the engine library, and `engines.json.bak` its previous version, read if the first is lost;
- `setup.json`: the last setup; `profiles.json`: your profiles of the setup's sections; `templates/*.json`: the
  saved templates; `queue.json`: the queue;
- `book-cache/`: the PGN books turned into Polyglot books for the participants' own books (removed after 30 days unused);
- `layout.ini`: the window layout and the panels of the workspace; `layouts.json`: the layouts saved by name; `fastchess-gui.log`: the log of the last session;
- `suite.json`: the last setup of the Suites page; `suites/<suite>-<date>.json`: the finished test suite runs;
- `board.json`: the settings of the Board page's analysis and games; `analyses/*.json`: the games analysed move
  by move, one file each; `games/*.pgn`: the games played from the Board page;
- `settings.json`: the tournaments folder, when another one was chosen in **Settings**, *Software rendering*, the
  folder of *Every engine of a folder…*, the tester and site of *Export for a rating list…*, the lichess token, the
  window's size and place, the check for a new version (wanted or not, when it was last made, the release it
  found, the notice hidden), the appearance (board colours, pieces, move sounds), and the machine's budget (cores,
  memory and GPUs for the tournaments, *Start when cores are free*);
- `logos/`: the engines' logos, copies of the pictures chosen on their cards;
- `machines.json`: the remote machines of **Settings** (their address, folder, budgets, engines' paths and what the
  last test found; no passphrase), and `known_hosts.json` the host keys trusted, of the machines and the tunnel;
- `running.json`: the tournaments playing now, with their weights, each removed when it stops; found at launch, it
  tells what to show again (a tournament that played on without the window) or to resume;
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
  - a folder received from someone else is safe to open: the file of *Also append every game to* must be a `.pgn`
    file (another name is ignored, with a warning), and a folder whose journal, PGN files or lock file are
    symbolic links is not opened for writing, so it cannot make the program write outside it. Resuming it runs
    the engines its `tournament.json` names, as any tournament does;
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
  - `fastchess-gui.log`: the log of the processes that played it (each tournament plays in a process of its own);
    `control.json`, while one plays it: where its local socket is (`control.sock` in the folder, or in the user's
    runtime folder when that path is too long; a named pipe on Windows), and which process listens;
  - `logs/game-N.log`: every line exchanged with both engines during game N (the number shown in the *Games*
    tab), with the time it was sent or read (`W>` sent to White's engine, `W<` read from its output, `W!` read
    from its error output: assertions, crash messages), for the games lost on time, by a crash, a stall or an illegal move
    (or every game, if asked). The first 300 lines of each engine and the last 5000 lines of the game are kept, in
    the order they came. It is what an engine author needs to reproduce a bug. For a WinBoard engine it shows the UCI side; `FCG_WB2UCI_LOG` gives the
    WinBoard side.
  - `logs/start-<engine>.log`: the last failed try to start an engine that cannot start: its program, the error,
    and the lines exchanged with the engines of that game until then (see *An engine that cannot start*).

Matches played with an earlier version (0.2.x) appear on the home page: *Import them* turns them into finished
tournaments.

## Command line

```
fastchess-gui [--profile DIR] [--autostart] [--size WxH] [--software]
              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]
              [--quit-after SECONDS]
fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json [--tlcs UDP_PORT [--tlcs-lanes]]
              [--web TCP_PORT] [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]] [--live-files [DIR]] [--lichess ROUND_URL|ROUND_ID|new]
              [--control stdio|socket] [--detach] [--allowance THREADS:MEMORY_MB:GPUS:ONE]
fastchess-gui --attach TOURNAMENT_FOLDER
fastchess-gui --version
```

- `--profile DIR` uses another profile folder;
- `--autostart` starts the saved setup at launch (rather than resuming the tournament that was running, see
  *Running a tournament*);
- `--size WxH` opens the window at that size, centred, rather than as it was left, and leaves the size and place
  kept for the next launch alone;
- `--software` has the processor draw the window (SDL's software renderer) rather than OpenGL, as *Software
  rendering* in **Settings** does, for this launch (see *The window*);
- `--shot` saves a screenshot of the window after the given number of seconds (default 5), with WINDOW brought to
  the front, and quits once every screenshot is taken. It can be repeated. WINDOW is a page (`home`, `setup`,
  `engines`, `books`, `board`, `ratings`, `ratings-engine`, `ratings-h2h`, `ratings-progress`, `console`, `settings`) or a
  tab (`live`, `games`, `openings`, `standings`, `schedule`, `bracket`, `broadcast`). The mouse is ignored in this
  mode and, on Linux, the window is rendered off screen (no window appears; set `SDL_VIDEODRIVER` to override);
  the window opens at the default size, or `--size`, and the size and place kept are left alone;
- `--focus WINDOW` opens that page or tab at launch, or: `engines-card` (the first engine's card),
  `engines-refresh` (*Refresh: the new engines of the folder*), `engines-ratings=FILE` (the preview of that rating
  list), `setup-picker` and `setup-suggest` (the library's filter focused, or the suggested opponents selected),
  `setup-open=ROW` and `setup-profiles=ROW` (a row of the setup opened, or its profile list: `format`, `time`,
  `openings`, `adjudication`, `engines`, `concurrency`, `pgn`), `setup-book` (the first participant's own book),
  `submission-export`
  (*Export for a rating list…*), `ratings-players` (the *PGN players* window), `palette[=TEXT]` (the command palette, with TEXT typed in it),
  `game-menu=N` (the menu of game N in the *Games* tab, a result chosen), `end-menu=N` (*End with result* of the
  game of slot N, from 1), `big-screen[=N]` (the big screen, of the game of slot N), `live-back=N` (the game
  followed in *Live*, stepped back N plies),

  `engines-console` (the first engine's console), `board-analyse` (the Board page, analysed), `board=FILE` (a PGN,
  FEN or EPD file on the Board page; `board-analyse=FILE` analysed, `board-review=FILE` its first game analysed move
  by move), `board-edit` (the position editor), `board-play` and `board-play-start` (a game between the first two
  engines, started), `machines` (the first remote machine's card in **Settings**; `machines-test` tested,
  `machines-files` the setup's books and tablebases compared with it);
- `--select-game N` opens game N (in finishing order) in the *Games* tab as soon as it exists;
- `--quit-after SECONDS` stops the tournament and quits;
- `--run`, `--detach`, `--attach`, `--control`, `--allowance`: see *Headless mode*;
- `--version` prints the version and the protocol of the channel the window follows a remote tournament through
  (see *Machines*), and quits.

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
