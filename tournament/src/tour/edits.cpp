#include "edits.hpp"

#include <algorithm>
#include <stdexcept>

namespace fcg::tour {

namespace {

std::vector<int> activeIds(const TournamentSpec& spec) {
    std::vector<int> ids;
    for (const auto& p : spec.participants) {
        if (p.active) ids.push_back(p.id);
    }
    return ids;
}

// Who a new participant meets: everyone in a round robin, the tested engines in a gauntlet.
std::vector<int> opponents(const TournamentSpec& spec, int id) {
    const auto active = activeIds(spec);
    std::vector<int> out;
    if (spec.format == Format::Gauntlet) {
        const auto seeds = std::min<std::size_t>(static_cast<std::size_t>(std::max(1, spec.seeds)), active.size());
        for (std::size_t i = 0; i < seeds; ++i) {
            if (active[i] != id) out.push_back(active[i]);
        }
    } else {
        for (const int other : active) {
            if (other != id) out.push_back(other);
        }
    }
    return out;
}

// One encounter per opponent, in extra rounds after the cycle's last one; colours alternate by cycle.
std::vector<Encounter> encountersOf(int id, const std::vector<int>& opps, uint32_t cycle, uint32_t first_round) {
    std::vector<Encounter> out;
    for (std::size_t k = 0; k < opps.size(); ++k) {
        Encounter e;
        e.cycle  = cycle;
        e.round  = first_round + static_cast<uint32_t>(k);
        e.first  = cycle % 2 == 1 ? id : opps[k];
        e.second = cycle % 2 == 1 ? opps[k] : id;
        out.push_back(e);
    }
    return out;
}

// Whole new cycles for the active participants, as the schedule would have generated them.
std::vector<Encounter> newCycle(const TournamentSpec& spec, uint32_t cycle) {
    const auto ids    = activeIds(spec);
    const int n       = static_cast<int>(ids.size());
    const auto rounds = spec.format == Format::RoundRobin ? bergerRounds(n) : gauntletRounds(n, spec.seeds);

    std::vector<Encounter> out;
    for (std::size_t r = 0; r < rounds.size(); ++r) {
        for (auto [a, b] : rounds[r]) {
            if (cycle % 2 == 0) std::swap(a, b);
            Encounter e;
            e.cycle  = cycle;
            e.round  = static_cast<uint32_t>(r + 1);
            e.first  = ids[static_cast<std::size_t>(a)];
            e.second = ids[static_cast<std::size_t>(b)];
            out.push_back(e);
        }
    }
    return out;
}

std::string uniqueName(const TournamentSpec& spec, const std::string& base) {
    const auto taken = [&](const std::string& name) {
        return std::any_of(spec.participants.begin(), spec.participants.end(),
                           [&](const Participant& p) { return p.engine.name == name; });
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const auto name = base + " (" + std::to_string(i) + ")";
        if (!taken(name)) return name;
    }
}

}  // namespace

bool isEditEvent(const std::string& type) {
    return type == "participant_added" || type == "participant_replaced" || type == "participant_withdrawn" ||
           type == "cycles_changed" || type == "games_voided";
}

nlohmann::json editEvent(const Edit& edit, uint32_t at, int new_id, const TournamentSpec& spec) {
    switch (edit.kind) {
        case Edit::Kind::AddParticipant: {
            Participant p;
            p.id          = new_id;
            p.engine      = edit.engine;
            p.engine.name = uniqueName(spec, edit.engine.name);
            return {{"t", "participant_added"}, {"at", at}, {"participant", p}, {"catch_up", edit.catch_up}};
        }
        case Edit::Kind::ReplaceParticipant: {
            Participant p;
            p.id          = new_id;
            p.engine      = edit.engine;
            p.engine.name = uniqueName(spec, edit.engine.name);
            return {{"t", "participant_replaced"}, {"at", at}, {"old", edit.participant}, {"participant", p}};
        }
        case Edit::Kind::Withdraw:
            return {{"t", "participant_withdrawn"},
                    {"at", at},
                    {"participant", edit.participant},
                    {"void_played", edit.void_played}};
        case Edit::Kind::SetCycles:
            return {{"t", "cycles_changed"}, {"at", at}, {"cycles", edit.cycles}};
        case Edit::Kind::VoidGames: {
            nlohmann::json games = nlohmann::json::array();
            for (const auto& [e, g] : edit.games) games.push_back({e, g});
            return {{"t", "games_voided"}, {"games", games}, {"replay", edit.replay}, {"why", edit.why}};
        }
    }
    return {};
}

void applyEditEvent(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule) {
    const auto type = event.at("t").get<std::string>();
    if (type == "games_voided") return;  // results only
    if (!schedule.editable()) throw std::runtime_error("a match cannot be edited this way");

    const auto size = schedule.size().value_or(0);
    const auto at   = std::min(event.at("at").get<uint32_t>(), size);
    // The cycle being played when the edit was made (0: nothing started yet).
    const uint32_t current = at > 0 ? schedule.at(at - 1)->cycle : 0;

    if (type == "participant_added") {
        const auto p = event.at("participant").get<Participant>();
        if (spec.participant(p.id)) throw std::runtime_error("participant id already used");
        spec.participants.push_back(p);
        const auto opps = opponents(spec, p.id);
        if (opps.empty()) return;

        // Future cycles get the newcomer's encounters at their end, last cycle first so positions stay valid.
        for (uint32_t c = schedule.lastCycle(); c > current; --c) {
            schedule.insert(schedule.endOfCycle(c), encountersOf(p.id, opps, c, schedule.lastRound(c) + 1));
        }
        // Cycles already started: played right away, if catching up.
        if (event.value("catch_up", true)) {
            std::vector<Encounter> block;
            for (uint32_t c = 1; c <= current; ++c) {
                const auto more = encountersOf(p.id, opps, c, schedule.lastRound(c) + 1);
                block.insert(block.end(), more.begin(), more.end());
            }
            schedule.insert(at, block);
        }
        return;
    }

    if (type == "participant_replaced") {
        const int old_id = event.at("old").get<int>();
        auto* old        = spec.participant(old_id);
        if (!old || !old->active) throw std::runtime_error("no active participant to replace");
        const auto p = event.at("participant").get<Participant>();
        if (spec.participant(p.id)) throw std::runtime_error("participant id already used");
        old->active      = false;
        old->replaced_by = p.id;
        spec.participants.push_back(p);
        schedule.replaceParticipant(old_id, p.id);
        return;
    }

    if (type == "participant_withdrawn") {
        const int id = event.at("participant").get<int>();
        auto* p      = spec.participant(id);
        if (!p || !p->active) throw std::runtime_error("no active participant to withdraw");
        p->active = false;
        schedule.eraseFrom(at, [&](const Encounter& e) { return e.first == id || e.second == id; });
        return;
    }

    if (type == "cycles_changed") {
        const int cycles = event.at("cycles").get<int>();
        if (cycles < 1) throw std::runtime_error("a tournament needs at least one cycle");
        const auto last = schedule.lastCycle();
        if (static_cast<uint32_t>(cycles) > last) {
            for (uint32_t c = last + 1; c <= static_cast<uint32_t>(cycles); ++c) {
                schedule.insert(schedule.size().value_or(0), newCycle(spec, c));
            }
        } else {
            schedule.eraseFrom(at, [&](const Encounter& e) { return e.cycle > static_cast<uint32_t>(cycles); });
        }
        spec.cycles = cycles;
        return;
    }

    throw std::runtime_error("unknown edit \"" + type + "\"");
}

}  // namespace fcg::tour
