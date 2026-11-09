# Tournament layer: technical design

Status: implemented, phases 1 to 3 (2026-11-09). This page was written as the plan and is kept as the
description of the design; the user side is in [the guide](../guide.md). UX reference: the mockups at
https://claude.ai/artifact/SvpApxS661ctzGNLTuvMQ4 (private link).

## 1. Goals

- Multi-engine tournaments (round robin, gauntlet) next to today's two-engine match (SPRT, fixed, infinite),
  with Swiss and knockout possible later without redesign.
- **Exact resume** after a crash, a power cut or a closed window, at any concurrency: no game lost, none played
  twice, a half-played game pair completed with the same opening and colours.
- **Editable while running**: add, remove or replace an engine, change the number of cycles, replay or void games,
  without losing a played game and with a single PGN.
- Results for N engines: crosstable, standings with tiebreaks, ratings with confidence intervals and an anchor.
- Usable without the GUI (tests now, a headless mode later), so the logic is testable and the GUI only displays it.
- fastchess stays the game engine: timing, adjudication, engine processes, recovery, PGN building. The fork keeps
  its current, small set of hooks.

Non-goals: CECP/WinBoard (external adapters), pondering, 10x8 variants, distributed testing.

## 2. Where it lives

```
app/            fastchess, unchanged apart from the existing hooks (fastchess_core)
tournament/     NEW: fcg_tournament, a static library, no ImGui, no SDL
  src/tour/       model, schedule, openings, journal, pgn_file, state, summary, runner, standings, edits,
                  pairing (Swiss, knockout), ratinglist (across tournaments), legacy (0.2.x runs)
  tests/          unit and integration tests (doctest, random mover engine)
gui/            fcg_lib + fastchess-gui: views only, reads the tournament library
```

Dependencies only go downward: `gui → tournament → fastchess_core`. Namespace `fcg::tour`.

## 3. Key decision: subclass `BaseTournament` from our code

`fastchess::BaseTournament` already owns what a tournament needs: the thread pool, the engine cache (one process
per engine, kept between games), CPU affinity, `playGame()` (one game with time control, adjudication, crash
recovery and the observer hooks), and the opening book. Its members are `protected`, and `startNext()` is the only
pure virtual function.

So `fcg::tour::Runner` derives from `BaseTournament` in **our** library, exactly as `RoundRobin` does in fastchess,
but asks our `TournamentState` which game to play next instead of a `Scheduler`:

```cpp
class Runner final : public fastchess::BaseTournament {
   public:
    Runner(TournamentState& state, GameObserver* downstream, RecordListener* listener);
    void start() override;        // fills the pool, waits until the state says done or stop is set
   protected:
    void startNext() override;    // claims ready games from the state until the pool is full
   private:
    void play(const GameTask& task);
    TournamentState& state_;
    GameObserver* downstream_;   // the GUI's observer, told about everything
    RecordListener* listener_;   // told about every recorded game
};
```

- Engine configurations come from the task (one per participant, with its overrides), not from the
  `config::EngineConfigs` global, so participants can be added while the runner is running.
- The pentanomial-aware `PauseGate` is used as today, with the encounter id as the pairing id.
- `playGame()` only hands the `MatchData` of a finished game to the observer, so while it runs the runner
  installs itself as fastchess' observer: on `onGameEnd(counted = true)` it records the game in the state (it
  knows which task the thread is playing), then forwards every callback to the GUI's observer. No fork change.
- SPRT is evaluated by `TournamentState` on every result with fastchess' own `SPRT` class, so the numbers stay
  identical to the command line tool.
- One runner at a time, as now (fastchess keeps its configuration and stop flag in globals).

Fork change: one line, `saveJson()` returns when `config_name` is empty, so fastchess writes no `config.json`
next to the tournament files (#8). The 165 upstream tests still pass.

Things to respect when building the runner's `config::Tournament`:

- The opening file is left empty, so `BaseTournament` does not load the book a second time: the state reads it
  and passes each opening to `playGame()` (section 6.3).
- `pgn.file` stays empty: the state writes the PGN files (section 7), so they exist in headless mode too.
- `getMaxAffinity()` reads the engine list once at construction: when an engine with `Threads > 1` is added to a
  tournament running with affinity, affinity is turned off for the new runner and the GUI says so. Edits never
  restart the runner otherwise.

## 4. Model

All plain structs with JSON conversion, in `model.hpp`. Ids are integers, assigned once and never reused.

```cpp
enum class Format { Match, RoundRobin, Gauntlet /*, Swiss, Knockout */ };

struct Conditions {             // what MatchSetup holds today, minus the two engine names
    TimeControl tc; uint64_t nodes; Openings openings; Adjudication adjudication;
    Concurrency concurrency; PgnOptions pgn; uint64_t seed;
};

struct CommonOverrides {        // "Réglages communs": forced on every participant unless overridden
    std::optional<int> hash, threads; std::optional<std::string> syzygy_path;
};

struct Participant {
    int id;
    std::string name;           // unique in the tournament; the fastchess engine name and the PGN name
    EngineSnapshot engine;      // cmd, args, dir, UCI options: copied when added, so later edits of the
                                // engine library do not change a running tournament
    Overrides overrides;        // hash, threads, time odds (percent), nodes
    enum class Status { Active, Withdrawn, Replaced } status;
    std::optional<int> replaced_by;
};

struct Encounter {              // a game pair: same opening, colours swapped (the pentanomial unit)
    int id;
    int cycle, round;           // round inside the cycle (Berger table)
    int first, second;          // participant ids; `first` has White in game 1
    std::size_t opening;        // index in the seeded opening order (6.3)
};

struct GameResult {
    int game;                   // 0 or 1 inside the encounter
    Outcome outcome;            // WhiteWins, BlackWins, Draw
    fastchess::MatchTermination termination; std::string reason;
    uint16_t plies; uint32_t duration_ms;
    PgnSpan fastchess_pgn, chessbase_pgn;
    int64_t finished_at;        // unix ms
};

struct TournamentSpec {
    std::string name; Format format; int cycles; int gauntlet_seeds;
    std::optional<SprtSettings> sprt; std::optional<uint64_t> max_games;
    Conditions conditions; CommonOverrides common;
};
```

`TournamentState` = spec + participants + encounters + results + voided games. It is rebuilt from disk (section 5)
and is the only thing the GUI reads, through a thread-safe snapshot (section 8).

## 5. Storage: a folder per tournament, an append-only journal

```
<tournaments>/2026-11-09 Blitz 2+1 Poule A/
  tournament.json        spec and initial participants, written once when the tournament is created
  journal.jsonl          everything that happened since, one JSON object per line
  games.pgn              cutechess-style comments   (as today)
  games-chessbase.pgn    [%eval] [%clk] comments     (as today)
  logs/                  engine logs kept after a crash or an illegal move
```

The default folder is `tournaments/` in the profile folder; a tournament is a folder, so it can be moved, copied,
zipped and sent.

### 5.1 Journal events

```jsonl
{"t":"encounter_started","encounter":17,"opening":{"fen":"...","moves":["e2e4","c7c5"]}}
{"t":"game_finished","encounter":17,"game":0,"outcome":"1-0","termination":"ADJUDICATION","reason":"...","plies":67,"ms":301250,"pgn":[812345,2210],"cb":[901122,2640],"at":1794245496000}
{"t":"game_voided","encounter":12,"game":1,"why":"replay requested: crash"}
{"t":"participant_added","participant":{...},"catch_up":true}
{"t":"participant_replaced","old":5,"new":9,"replay":true}
{"t":"cycles_changed","cycles":14}
{"t":"finished","why":"sprt_h1"}
```

- **Event sourcing**: the state is `tournament.json` plus the replay of the journal. A 50 000-game SPRT is about
  50 000 lines, replayed in well under a second. No snapshot needed at these sizes.
- **Encounters are derived, not stored**: they are generated deterministically from the spec, the participants and
  the edit events (section 6). Only what cannot be recomputed is journaled.
- The opening is journaled when the first game of an encounter starts, so the second game uses the same opening
  even if the book file changed or moved in between.
- The file stays readable and editable by hand, as ChessGUI testers are used to.

### 5.2 Write order and crash safety

For each finished game, on the worker thread, under one mutex:

1. append both PGN texts, flush (the spans are known);
2. append the `game_finished` line, flush and `fsync`;
3. update the in-memory state and notify the GUI.

When a tournament is opened:

- a last journal line without its newline (cut by a power failure) is ignored;
- each PGN file is truncated to the end of the last journaled span, which removes a game written in step 1 but
  never journaled;
- games that were running are simply not in the journal: they are played again, with the opening recorded by
  `encounter_started`.

Writing the PGN first means the journal never points at a game that is not on disk.

## 6. Scheduling

### 6.1 Generators

`schedule.hpp` turns (spec, participants, edits) into the ordered list of encounters. Pure functions, unit tested.

- **Round robin**: Berger tables. A cycle has n−1 rounds (n even; a bye when n is odd). Each round has every
  participant exactly once, so intermediate standings are fair and the games running at any time mix engines. The
  colours of game 1 alternate between cycles.
- **Gauntlet**: the seeds against every other participant, interleaved round by round so that every opponent is
  met before any is met twice.
- **Match**: one encounter per round, generated in blocks of 1000 as needed (infinite or SPRT), up to `max_games`.
- **Swiss and knockout** generate round r+1 only when round r is complete (`claimNext()` answers "nothing yet,
  wait", 6.4). `pairing.cpp` pairs them: Swiss by score groups, top half against bottom half, no rematch unless
  impossible, the bye to the lowest ranked player without one; knockout by a seeded bracket with byes for the top
  seeds, a tied match getting one more game pair at a time (ten at most, then the higher seed). Each round is
  journaled (`round_paired`, `tiebreak_added`), so a resume replays it instead of pairing again.

### 6.2 Edits

Edits append to the schedule and never reorder what was played:

- **Add a participant, with catch-up**: its encounters against everyone for the cycles already started are inserted
  right after the running round, then it joins the following rounds. Without catch-up, only the future cycles.
- **Replace**: the old participant's games are voided (kept, out of the standings, visible as archived), the new
  one takes its place in all its encounters.
- **Withdraw**: its unplayed games are dropped; its played games stay or are voided (the user chooses).
- **Cycles**: new cycles are appended; removing cycles drops only unplayed encounters.
- **Replay / void a game**: `game_voided`, and for replay a new game in the same encounter.

`impact(edit)` computes the preview shown in the GUI (games added or replayed, new estimated end) by applying the
edit to a copy of the state. The same code path applies it for real, so the preview cannot lie.

### 6.3 Openings

The book is read once with fastchess' readers and shuffled with the tournament seed (the same Fisher-Yates as
fastchess, on a generator of our own, so fastchess' global generator does not matter), giving a fixed order. Encounter k uses opening k mod size. The tournament records the book path, its size
and a fingerprint (size + CRC32 of the file): when the book changed at resume, the GUI warns and the journaled
openings of started encounters still apply.

### 6.4 Dispatch

`TournamentState::claimNext()` returns the next game to play: the first unplayed game in schedule order whose
encounter is not blocked. Game 1 of an encounter is claimed right after game 0, as today, so pairs complete
quickly and pentanomial statistics stay balanced. A claimed game is in memory only ("running"); if the process dies
it is unclaimed by construction.

## 7. Recording

`TournamentState::complete()` writes both PGN files (fastchess' `PgnBuilder`, both comment styles) and the journal
line, in the order of section 5.2, under the state's mutex. `PgnFile` moved from the GUI to the library. The GUI
observer keeps only what the views need and receives each recorded game through `RecordListener`.

`summarize()` reads `tournament.json` and counts journal lines, without loading the opening book or writing
anything (a cut last line is ignored, not repaired, since the tournament may be running): it is what lists
tournaments and offers to resume them.

## 8. Threads and the GUI

- Worker threads call `TournamentState::complete()` (under its mutex), which writes the files and bumps
  a version counter.
- The GUI thread calls `state.snapshot(version)` once per frame: it returns a new `std::shared_ptr<const Snapshot>`
  only when something changed (standings, crosstable, recent games, progress). Views read the snapshot and never
  lock.
- Edits from the GUI are commands: `state.apply(Edit)` validates, journals, regenerates the schedule; the runner
  sees the new games at its next `claimNext()`.
- The live data per slot (moves, clocks, PV) keeps today's `GuiObserver` design, extended from 2 to N engines.

## 9. Statistics and ratings

- **Two-engine match**: unchanged. fastchess `Stats` (WDL and pentanomial), `EloWDL`/`EloPentanomial`, `SPRT`.
- **Per pair** in a multi-engine tournament: the same `Stats`, so the crosstable cell detail can show Elo
  difference, LOS and pentanomial counts with fastchess' formulas.
- **Ratings for N engines**: maximum likelihood on the logistic model with draws as half points (as Ordo does by
  default), solved by Newton iteration on at most a few hundred players. Confidence intervals from the inverse of
  the Hessian (Fisher information). Either the mean is 0 or one participant is the anchor at a given rating.
  Checked in tests against Ordo's output on a fixed PGN.
- **Tiebreaks**: points, then Sonneborn-Berger, then head-to-head, then wins; a Swiss ranks by Buchholz before
  Sonneborn-Berger, and its byes count a point.
- **Rating list**: the same maximum likelihood over every game of every tournament folder, engines identified by
  name, optionally restricted to one time control (`ratinglist.cpp`).
- **Health**: per participant time losses, crashes, illegal moves and response latency, from `MatchData`.

## 10. GUI structure

- `App` keeps the window, the theme, the engine library and the list of open tournaments; one `Workspace` per open
  tournament holds its snapshot, live slots and view state. Only one can run at a time; the others are read-only
  until then.
- Views per screen of the mockups: `view_home`, `view_new_tournament`, `view_engines`, and the workspace tabs
  `overview`, `live`, `standings`, `schedule`, `games`, `charts`, `broadcast`, `log`.
- Fixed layouts per tab; the dockspace goes away. A board can be detached into its own OS window with ImGui
  multi-viewports (supported by the docking branch and the SDL3 backend).
- `theme.cpp`: colour tokens for dark and light, applied to `ImGuiStyle` and used by the custom drawing (boards,
  bars, charts). Fonts: Inter (OFL) and an icon font such as Lucide (ISC), embedded like the current font.
- `setup.json` becomes the "last match" template, so the existing two-engine flow opens pre-filled.

## 11. Testing

- Schedule: every pair plays exactly 2 × cycles games, colours balanced, one game per participant per round,
  catch-up after an edit, determinism for a given seed.
- Journal: replay, a truncated last line, PGN truncation to the last span, unknown event types rejected with a
  readable error.
- Resume: run a tournament with the random mover, stop it abruptly at several points (including between steps 1
  and 2 of 5.2), resume, and check the final game count, the pairs and the PGN.
- Ratings: against Ordo on a fixture; symmetric cases; anchor shifting.
- Runner: existing harness (`run_tests.cpp`) extended to N engines, pause and stop as today.

## 12. Milestones

Each milestone ships and keeps everything that works today working.

All done.

1. **Library core**: model, journal, schedule (match, round robin, gauntlet), state, runner. The
   current GUI runs its matches through it: SPRT matches gain exact resume. No UI change.
2. **New shell**: navigation rail, home (tournament list, resume), dark and light themes, fonts and icons, the match
   workspace with today's views moved into tabs.
3. **Multi-engine**: the new-tournament page (participants, common overrides, live summary), round robin and
   gauntlet, overview, standings, crosstable, ratings.
4. **Live**: mosaic and focus, detachable boards.
5. **Editing**: add, replace, withdraw, cycles, replay, void, with the impact preview.
6. **Engine library**: health check, import from ChessGUI, cutechess, Banksia and Arena, groups.
7. **Workflow**: queue, "Relancer avec…", templates, headless `fastchess-gui --run <folder>`.

Then phase 2 (broadcast: files, TLCS, web page) and phase 3 (Swiss, knockout, rating database across tournaments).

## 13. Open points

- Tournaments live in `runs/` of the profile folder; *Open a folder…* opens one from anywhere, but there is no
  setting for another default location yet.
- Earlier `runs/` (0.2.x) are imported as finished matches from their PGN and `config.json` (`legacy.cpp`).
- Edits cannot be undone; a mistaken change is corrected by another one (replay, cycles).
- The TLCS server follows the protocol as documented by TLCV and node-tlcv, but has not been tried against a real
  TLCV client yet.
