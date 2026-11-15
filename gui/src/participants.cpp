#include "participants.hpp"

#include <algorithm>

#include "i18n.hpp"
#include "run.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// Sorted, without duplicates nor indices outside the list.
std::vector<int> validIndices(std::vector<int> indices, std::size_t size) {
    std::erase_if(indices, [&](int i) { return i < 0 || i >= static_cast<int>(size); });
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

}  // namespace

std::vector<int> moveParticipants(std::vector<ParticipantSetup>& list, std::vector<int> moved, int to) {
    moved = validIndices(std::move(moved), list.size());
    to    = std::clamp(to, 0, static_cast<int>(list.size()));
    if (moved.empty()) return {};
    std::vector<ParticipantSetup> taken, rest;
    int insert = 0;  // where `to` falls among the participants that stay
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (std::binary_search(moved.begin(), moved.end(), i)) {
            taken.push_back(std::move(list[i]));
        } else {
            if (i < to) ++insert;
            rest.push_back(std::move(list[i]));
        }
    }
    rest.insert(rest.begin() + insert, std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end()));
    list = std::move(rest);
    std::vector<int> now;
    for (int k = 0; k < static_cast<int>(taken.size()); ++k) now.push_back(insert + k);
    return now;
}

int removeParticipants(std::vector<ParticipantSetup>& list, std::vector<int> indices) {
    indices = validIndices(std::move(indices), list.size());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) list.erase(list.begin() + *it);
    return static_cast<int>(indices.size());
}

bool addToSetup(MatchSetup& setup, const std::vector<std::string>& names, int at) {
    if (names.empty()) return false;
    if (setup.format == TournamentFormat::Match) {
        bool changed = false;
        for (const auto& name : names) {
            auto& slot = setup.engine_a.empty() ? setup.engine_a : setup.engine_b;
            if (slot == name) continue;
            slot    = name;
            changed = true;
        }
        return changed;
    }
    auto& list = setup.participants;
    int where  = at < 0 ? static_cast<int>(list.size()) : std::min(at, static_cast<int>(list.size()));
    bool added = false;
    for (const auto& name : names) {
        if (std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.name == name; })) continue;
        list.insert(list.begin() + where++, ParticipantSetup{name});
        added = true;
    }
    return added;
}

std::vector<int> setupTeams(const MatchSetup& setup) {
    const auto& list = setup.participants;
    const int n      = static_cast<int>(list.size());
    std::vector<int> teams(list.size(), -1);
    int sizes[2] = {0, 0};
    for (int i = 0; i < n; ++i) {
        if (list[i].team == 1 || list[i].team == 2) {
            teams[i] = list[i].team - 1;
            sizes[teams[i]]++;
        }
    }
    // None chosen: the upper half, the larger one when the number is odd, against the lower half.
    if (sizes[0] + sizes[1] == 0) {
        for (int i = 0; i < n; ++i) teams[i] = i < (n + 1) / 2 ? 0 : 1;
        return teams;
    }
    for (int i = 0; i < n; ++i) {
        if (teams[i] >= 0) continue;
        teams[i] = sizes[1] < sizes[0] ? 1 : 0;
        sizes[teams[i]]++;
    }
    return teams;
}

void setTeam(MatchSetup& setup, const std::vector<int>& indices, int team) {
    const auto teams = setupTeams(setup);
    auto& list       = setup.participants;
    for (std::size_t i = 0; i < list.size(); ++i) list[i].team = teams[i] + 1;
    for (const int i : validIndices(indices, list.size())) list[i].team = team == 1 ? 2 : 1;
}

void splitTeams(MatchSetup& setup) {
    for (auto& p : setup.participants) p.team = 0;
}

std::pair<int, int> teamSizes(const MatchSetup& setup) {
    const auto teams = setupTeams(setup);
    const auto b     = static_cast<int>(std::count(teams.begin(), teams.end(), 1));
    return {static_cast<int>(teams.size()) - b, b};
}

std::vector<std::string> participantDifferences(const ParticipantSetup& p, const MatchSetup& setup) {
    std::vector<std::string> out;
    if (p.hash > 0 && p.hash != setup.common_hash) out.push_back(trf("Hash {} MB", p.hash));
    if (p.threads > 0 && p.threads != setup.common_threads) {
        out.push_back(trn("{} thread", "{} threads", static_cast<uint64_t>(p.threads), p.threads));
    }
    if (setup.limit == LimitKind::Nodes && p.nodes > 0 && p.nodes != setup.nodes)
        out.push_back(trn("{} node per move", "{} nodes per move", p.nodes, ui::formatCount(p.nodes)));
    if (setup.limit == LimitKind::Depth && p.depth > 0 && p.depth != setup.depth)
        out.push_back(trf("depth {}", p.depth));
    if (!clockFree(setup.limit) && p.time_percent != 100) out.push_back(trf("{} % of the time", p.time_percent));
    if (!p.book.empty()) out.push_back(ownBookText(ownBookName(p.book), p.book.depth));
    return out;
}

}  // namespace fcg
