#include "schedule.hpp"

#include <algorithm>
#include <stdexcept>

namespace fcg::tour {

std::vector<std::vector<std::pair<int, int>>> bergerRounds(int n) {
    std::vector<std::vector<std::pair<int, int>>> rounds;
    if (n < 2) return rounds;

    // An odd field gets a phantom player; meeting it is a bye.
    const int m     = n % 2 == 0 ? n : n + 1;
    const int fixed = m - 1;

    for (int r = 0; r < m - 1; ++r) {
        std::vector<std::pair<int, int>> round;
        for (int i = 0; i < m / 2; ++i) {
            int a = (r + i) % (m - 1);
            int b = i == 0 ? fixed : (r - i + (m - 1)) % (m - 1);

            // The fixed player alternates colours from round to round.
            if (i == 0 && r % 2 == 1) std::swap(a, b);
            if (a >= n || b >= n) continue;
            round.emplace_back(a, b);
        }
        rounds.push_back(std::move(round));
    }
    return rounds;
}

std::vector<std::vector<std::pair<int, int>>> gauntletRounds(int n, int seeds) {
    std::vector<std::vector<std::pair<int, int>>> rounds;
    seeds         = std::clamp(seeds, 1, std::max(1, n - 1));
    const int opp = n - seeds;
    if (opp < 1) return rounds;

    // Seed k meets opponent (r + k) mod opp in round r, so the seeds do not all meet the same engine at once.
    for (int r = 0; r < opp; ++r) {
        std::vector<std::pair<int, int>> round;
        for (int k = 0; k < seeds; ++k) round.emplace_back(k, seeds + (r + k) % opp);
        rounds.push_back(std::move(round));
    }
    return rounds;
}

std::vector<std::vector<TeamPair>> teamRounds(int a, int b, bool boards) {
    std::vector<std::vector<TeamPair>> rounds;
    if (a < 1 || b < 1) return rounds;

    // Board k: team A has White in the first game on the odd boards (k even), team B on the others.
    if (boards) {
        std::vector<TeamPair> round;
        for (int k = 0; k < std::min(a, b); ++k) round.push_back({k, k, k % 2 == 0});
        rounds.push_back(std::move(round));
        return rounds;
    }

    // The smaller team's player k meets the larger team's (k + r) mod its size in round r: every pair once, and a
    // player of the larger team sits a round out when the teams differ in size. Team A has White first when the two
    // places add up to an even number: every player's colours alternate from one opponent to the next.
    const int small = std::min(a, b), large = std::max(a, b);
    for (int r = 0; r < large; ++r) {
        std::vector<TeamPair> round;
        for (int k = 0; k < small; ++k) {
            const int other  = (k + r) % large;
            const bool white = (k + other) % 2 == 0;
            round.push_back(a <= b ? TeamPair{k, other, white} : TeamPair{other, k, white});
        }
        rounds.push_back(std::move(round));
    }
    return rounds;
}

std::vector<Encounter> teamCycle(const std::vector<int>& a, const std::vector<int>& b, bool boards, uint32_t cycle) {
    std::vector<Encounter> out;
    const auto rounds = teamRounds(static_cast<int>(a.size()), static_cast<int>(b.size()), boards);
    for (std::size_t r = 0; r < rounds.size(); ++r) {
        for (const auto& pair : rounds[r]) {
            // As in a round robin, the colours of the first game are reversed from one cycle to the next.
            const bool a_white = pair.a_white == (cycle % 2 == 1);
            Encounter e;
            e.cycle  = cycle;
            e.round  = static_cast<uint32_t>(r + 1);
            e.first  = a_white ? a[static_cast<std::size_t>(pair.a)] : b[static_cast<std::size_t>(pair.b)];
            e.second = a_white ? b[static_cast<std::size_t>(pair.b)] : a[static_cast<std::size_t>(pair.a)];
            out.push_back(e);
        }
    }
    return out;
}

Schedule::Schedule(const TournamentSpec& spec) : format_(spec.format) {
    const auto& people = spec.participants;
    const int n        = static_cast<int>(people.size());

    if (format_ == Format::Match) {
        if (n != 2) throw std::invalid_argument("a match needs exactly two participants");
        match_a_ = people[0].id;
        match_b_ = people[1].id;
        if (spec.max_games > 0) size_ = static_cast<uint32_t>((spec.max_games + 1) / 2);
        return;
    }

    if (n < 2) throw std::invalid_argument("a tournament needs at least two participants");
    if (spec.cycles < 1) throw std::invalid_argument("a tournament needs at least one cycle");

    // Swiss and knockout rounds are added as they are paired.
    if (pairedAsItGoes(format_)) {
        if (format_ == Format::Swiss && spec.rounds < 1) throw std::invalid_argument("a Swiss needs rounds");
        size_ = 0;
        return;
    }

    if (format_ == Format::TeamMatch) {
        const auto a = spec.teamMembers(0), b = spec.teamMembers(1);
        if (a.empty() || b.empty()) throw std::invalid_argument("a team match needs an engine in each team");
        if (spec.team_boards && a.size() != b.size())
            throw std::invalid_argument("a team match board by board needs teams of the same size");
        for (int c = 1; c <= spec.cycles; ++c) {
            for (auto e : teamCycle(a, b, spec.team_boards, static_cast<uint32_t>(c))) {
                e.index = static_cast<uint32_t>(list_.size());
                list_.push_back(e);
            }
        }
        size_ = static_cast<uint32_t>(list_.size());
        return;
    }

    const auto rounds = format_ == Format::RoundRobin ? bergerRounds(n) : gauntletRounds(n, spec.seeds);

    for (int c = 0; c < spec.cycles; ++c) {
        for (std::size_t r = 0; r < rounds.size(); ++r) {
            for (auto [a, b] : rounds[r]) {
                // Colours of the first game alternate from one cycle to the next.
                if (c % 2 == 1) std::swap(a, b);

                Encounter e;
                e.index  = static_cast<uint32_t>(list_.size());
                e.cycle  = static_cast<uint32_t>(c + 1);
                e.round  = static_cast<uint32_t>(r + 1);
                e.first  = people[a].id;
                e.second = people[b].id;
                list_.push_back(e);
            }
        }
    }
    size_ = static_cast<uint32_t>(list_.size());
}

std::optional<Encounter> Schedule::at(uint32_t index) const {
    if (size_ && index >= *size_) return std::nullopt;

    if (format_ == Format::Match) {
        Encounter e;
        e.index  = index;
        e.cycle  = 1;
        e.round  = index + 1;
        e.first  = match_a_;
        e.second = match_b_;
        return e;
    }

    return list_[index];
}

void Schedule::renumber() {
    for (std::size_t i = 0; i < list_.size(); ++i) list_[i].index = static_cast<uint32_t>(i);
    size_ = static_cast<uint32_t>(list_.size());
}

void Schedule::insert(uint32_t at, const std::vector<Encounter>& encounters) {
    const auto pos = list_.begin() + std::min<std::size_t>(at, list_.size());
    list_.insert(pos, encounters.begin(), encounters.end());
    renumber();
}

void Schedule::replaceParticipant(int old_id, int new_id) {
    for (auto& e : list_) {
        if (e.first == old_id) e.first = new_id;
        if (e.second == old_id) e.second = new_id;
    }
}

uint32_t Schedule::endOfCycle(uint32_t cycle) const {
    for (std::size_t i = list_.size(); i-- > 0;) {
        if (list_[i].cycle == cycle) return static_cast<uint32_t>(i + 1);
    }
    return static_cast<uint32_t>(list_.size());
}

uint32_t Schedule::lastCycle() const {
    uint32_t c = 0;
    for (const auto& e : list_) c = std::max(c, e.cycle);
    return c;
}

uint32_t Schedule::lastRound(uint32_t cycle) const {
    uint32_t r = 0;
    for (const auto& e : list_) {
        if (e.cycle == cycle) r = std::max(r, e.round);
    }
    return r;
}

}  // namespace fcg::tour
