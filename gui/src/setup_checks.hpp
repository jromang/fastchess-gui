#pragma once

// What could go wrong with a tournament's setup, said before it starts: notices that name the engine (and its option
// or file), the resource and the fix. The setup summary shows them; see notice.hpp.

#include <optional>
#include <string>
#include <vector>

#include <tour/budget.hpp>

#include "memory.hpp"
#include "model.hpp"
#include "notice.hpp"
#include "tablebases.hpp"
#include "topology.hpp"

namespace fcg {

// The threads each engine of a game may use without slowing down the other games: the hardware threads (the
// physical cores with a clock) shared by the games played at once, as buildRun() sets their number. At least 1.
[[nodiscard]] int threadsPerGame(const MatchSetup& setup, const CpuTopology& topology);

// The Threads a participant plays with, and where they come from: its own in the setup, the setup's common ones
// (on an engine that has the option, or whose options are not known yet), else the engine's own (its setting, else
// its default).
struct ParticipantThreads {
    enum class From { Participant, Common, Engine };
    int threads = 1;
    From from   = From::Engine;
};
[[nodiscard]] ParticipantThreads participantThreads(const MatchSetup& setup, const ParticipantSetup& participant,
                                                    const EngineEntry& engine);

// What the heaviest game of a setup takes of the machine (tour::GameCost): the larger Threads of its participants as
// they will play, their two largest Hashes with what each engine process takes besides, and its engines that use a GPU
// (EngineEntry::gpu), at most two.
[[nodiscard]] tour::GameCost setupGameCost(const MatchSetup& setup, const std::vector<EngineEntry>& engines);

// What the machine's budget (Settings) lets a setup play at once, and why fewer than its concurrency: the memory, the
// GPUs. Empty `why` when the budget allows them all. The cores are not counted: a tournament alone plays its
// concurrency, the cores being shared between tournaments.
struct BudgetLimit {
    int games     = 0;  // at a time, by the memory
    int gpu_games = 0;  // of them with an engine that uses a GPU
    std::string why;
    bool warn = false;  // the games may share a GPU, or one game needs more GPUs than there are
};
[[nodiscard]] BudgetLimit budgetLimit(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                      const tour::Budget& budget, int concurrency);

// Participant `index` of setupParticipants(setup) plays with too many threads: its engine's own Threads (some engines
// take every core by default) above half the hardware threads or above threadsPerGame(), or Threads the setup gives
// above threadsPerGame(). With Lower Threads (where they come from) and, for the engine's own, Open the engine.
[[nodiscard]] std::optional<Notice> threadNotice(const MatchSetup& setup, std::size_t index,
                                                 const std::vector<EngineEntry>& engines, const CpuTopology& topology);

// threadNotice() of every participant: one for an engine that plays twice, one for the common setting.
[[nodiscard]] std::vector<Notice> threadNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                                const CpuTopology& topology);

// The engines of the setup whose program or working directory is not found, with Open the engine and Locate….
[[nodiscard]] std::vector<Notice> programNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines);

// The option values an engine of the setup refuses, its own or those the setup gives (Hash, Threads): out of its
// range, not among its choices, or an option it does not have. fastchess does not send them, and the engine plays
// with its default. Only for the engines whose options were read.
[[nodiscard]] std::vector<Notice> optionNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines);

// The memory the hash tables of the setup take (`need`, see setupMemory()), and the engine with the largest Hash: an
// error above the memory installed (the tournament cannot start), a warning above the free memory. With Open the
// engine when that Hash is the engine's own.
[[nodiscard]] Notice memoryNotice(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                  const MemoryNeed& need);

// The setup's opening book, given and not found, with Locate…; nullopt otherwise.
[[nodiscard]] std::optional<Notice> bookNotice(const MatchSetup& setup);

// The engine that could not start, as fastchess says it in the log when it gives up and stops the tournament
// ("Fatal; Alpha engine startup failure: \"…\""): its name and why. The last one of `lines`; nullopt when none says.
struct StartupFailure {
    std::string engine;
    std::string reason;
};
[[nodiscard]] std::optional<StartupFailure> startupFailure(const std::vector<std::string>& lines);

// The library engine a participant plays: its own name, or the engine's when it plays as "<engine> (<book>)"; empty
// when none of `engines`.
[[nodiscard]] std::string libraryEngine(const std::vector<EngineEntry>& engines, const std::string& participant);

// A tablebase path the setup uses, and who uses it: the tablebase adjudication, the SyzygyPath of engines.
struct TablebaseUse {
    std::string paths;  // folders separated as splitTablebasePath() reads them, relative ones made absolute
    bool adjudication = false;
    int pieces        = 0;  // the adjudication's Max pieces, 0 = what the tables cover
    std::vector<std::string> engines;

    bool operator==(const TablebaseUse&) const = default;
};

// The tablebase paths of a setup: the adjudication's, when it is on, and the SyzygyPath of each engine (the setup's
// when it gives the engines its tablebases, else the engine's setting or default), a relative folder taken from the
// engine's working directory. One per path, in that order.
[[nodiscard]] std::vector<TablebaseUse> tablebaseUses(const MatchSetup& setup, const std::vector<EngineEntry>& engines);

// The tablebase adjudication on without a folder, which the tournament cannot start with, with Locate…; nullopt
// otherwise.
[[nodiscard]] std::optional<Notice> adjudicationFolderNotice(const MatchSetup& setup);

// What is wrong with the files of a tablebase path, from the check of its folders: folders not found or without a
// tablebase file, the adjudication above the tables found, the WDL tables missing up to the pieces used, damaged
// files. Warnings, but for an adjudication without any file: fastchess cannot start the tournament then. With
// Open the folder, Lower to N pieces, Open the engine. When all is well, a line for the adjudication.
[[nodiscard]] std::vector<Notice> tablebaseNotices(const TablebaseUse& use, const TablebaseCheck& check);

}  // namespace fcg
