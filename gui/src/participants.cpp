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
