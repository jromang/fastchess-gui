#pragma once

// The home page as a control room (view_home.cpp): what runs, the machine's share of it, what needs action, the queue
// and the last tournaments finished. Here the parts that need no App: the card of a running tournament and the bars
// of the resources, drawn from what they show (so that one card is drawn per tournament once several run at once),
// and the plain computations behind the page.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <tour/model.hpp>
#include <tour/summary.hpp>

#include "notice.hpp"

namespace fcg::home {

// "Round robin": a format's name in the language of the interface.
[[nodiscard]] std::string formatLabel(tour::Format format);

// What the list of tournaments says of one: "Running", "Interrupted", "H1 accepted", "Finished"...
[[nodiscard]] std::string statusLabel(const tour::Summary& s, bool running);

// When something `seconds` from now ends, in local time: "18:40" today, "2026-11-14 03:10" another day.
[[nodiscard]] std::string endTime(double seconds);

// The title of a section of a page, a little larger than the text.
void sectionTitle(const char* text);

// "9½", "½", "12": points as the standings write them.
[[nodiscard]] std::string halfPoints(double points);

// A tournament's result in a few words, for the list of the last ones finished: the SPRT's decision with the score,
// a match's score ("Alpha 52½ – 47½ Beta"), a knockout's winner, or the first of the standings with its points and
// games. "No game played" without a game.
[[nodiscard]] std::string resultText(const tour::Summary& s);

// The tournaments finished the home page shows, the newest first.
inline constexpr std::size_t LAST_FINISHED = 5;

// The last `count` tournaments of `list` (newest first, as the list of the home page is) that are finished, those of
// an earlier version imported included; none that is being played (`running`, its folder).
[[nodiscard]] std::vector<const tour::Summary*> lastFinished(const std::vector<tour::Summary>& list, std::size_t count,
                                                             const std::filesystem::path& running = {});

// When everything queued ends, from the length of each tournament (the running one first, then the queue): the
// seconds of those whose length is known, and how many have none (an SPRT without a game limit, nodes or depth per
// move, the running one before its pace is known).
struct QueueEnd {
    double seconds = 0;
    int unknown    = 0;
    int count      = 0;
    [[nodiscard]] bool known() const noexcept { return count > unknown; }
};
[[nodiscard]] QueueEnd queueEnd(const std::vector<std::optional<double>>& lengths);

// The free space of the drive a folder is on (the closest folder of its path that exists, for one not made yet).
struct DiskSpace {
    std::filesystem::path dir;
    std::uintmax_t available = 0;  // bytes the program may write
    std::uintmax_t capacity  = 0;
};
[[nodiscard]] std::optional<DiskSpace> diskSpace(const std::filesystem::path& dir);

// A drive too full for a night of games: under 1 GB free, or under 2 % of a drive under 500 GB (2 % of a larger one
// still holds the PGN files of thousands of tournaments).
[[nodiscard]] bool diskLow(std::uintmax_t available, std::uintmax_t capacity);

// What the home page says of a drive too full: how much is left of how much, where, and what happens when it is full,
// with Open the folder and Change the tournaments folder….
[[nodiscard]] Notice diskNotice(const DiskSpace& disk);

// A share of one of the machine's resources: the cores of a tournament, later an analysis' too, and the memory and
// the GPUs once tournaments have a budget of them.
struct Share {
    std::string label;    // drawn in its part of the bar when it fits: "Gauntlet 40/15 · 8"
    std::string tooltip;  // at the cursor, over its part
    double amount = 0;    // in the bar's unit
    ImU32 colour  = 0;
};

// One resource: what the machine has and who uses how much of it, the rest free.
struct ResourceBar {
    std::string title;    // "Cores"
    std::string caption;  // after the bar: "6 of 16 in use"
    std::string tooltip;  // at the cursor, over the title and the caption: what the capacity is
    std::string free;     // the label of the part left: "10 free"
    double capacity = 0;
    std::vector<Share> shares;
};

// The bars one under the other, their titles in a column of their own.
void drawResourceBars(const std::vector<ResourceBar>& bars);

// What the card of a running tournament shows.
struct RunCard {
    std::string id;  // tells its buttons from another card's
    std::string name;
    std::string format;       // "Gauntlet · 40/15 · 12 engines"
    std::string state;        // "Running", "Paused", "Stopping…"
    bool warn_state = false;  // paused or stopping: in the warning colour
    uint64_t played = 0;
    uint64_t total  = 0;  // 0: until stopped (a match without a game limit)
    int playing     = 0;  // games being played
    std::string end;      // "Ends about 03:40 (in 2 h 10 min)", or why it is not known
    std::string leader;   // "Leader: Alpha 3.2 +41.0 ±18.2", "LLR 1.12 (−2.94, 2.94)"
    std::string detail;   // under it, grey: "Elo +3.1 ±4.0 · Alpha 412½ – 387½ Beta"
    // Its share of the cores against the other tournaments running, shown once several share them.
    std::optional<int> weight;
    // Drawn on its own line under the buttons when given: its weight and its cores, once several tournaments share
    // the machine (it then takes the place of `weight`).
    std::function<void()> controls;
    bool paused     = false;
    bool can_pause  = true;  // not while it stops
    bool can_resume = true;
};
enum class CardAction { None, Open, Pause, Resume };

// The card, `width` wide (0: the width left): its name and state, its format, a progress bar with the games played of
// those planned and when it ends, the leader, and Open and Pause (or Resume).
CardAction drawRunCard(const RunCard& card, float width = 0);

}  // namespace fcg::home
