#include "start_failures.hpp"

#include <matchmaking/tournament/base/tournament.hpp>

namespace fcg::tour {

static_assert(StartFailures::TRIES == 1 + fastchess::BaseTournament::ENGINE_START_RETRIES);
static_assert(StartFailures::RETRY_PAUSE == fastchess::BaseTournament::ENGINE_START_RETRY_PAUSE);

std::optional<StartFailureLine> parseStartFailure(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);

    constexpr std::string_view marker = " engine startup failure: \"";
    const auto at                     = line.find(marker);
    const auto level                  = line.find("; ");
    if (at == std::string_view::npos || level == std::string_view::npos || level > at) return std::nullopt;
    const auto head = line.substr(0, level);
    if (head != "Warning" && head != "Fatal") return std::nullopt;

    // The error is quoted, followed by the wait before the next try, if any; it may hold quotes itself.
    auto error       = line.substr(at + marker.size());
    const auto close = error.rfind('"');
    if (close == std::string_view::npos) return std::nullopt;
    error = error.substr(0, close);

    return StartFailureLine{std::string(line.substr(level + 2, at - level - 2)), std::string(error)};
}

StartFailures::Outcome StartFailures::failed(int participant, uint64_t game, const std::string& name,
                                             const std::string& error, Clock::time_point now,
                                             const std::filesystem::path& log) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++version_;
    auto [it, fresh] = engines_.try_emplace(participant);
    auto& e          = it->second;
    e.participant    = participant;
    e.name           = name;
    e.error          = error;
    if (!log.empty()) e.log = log;
    if (fresh) e.since = std::chrono::system_clock::now();

    // Games claimed before its previous failure fail with it; only the one claimed to try it again is a new try.
    if (!fresh && !(e.trying && e.trying_game == game)) return Outcome::Same;

    e.trying = false;
    ++e.tries;
    if (e.failed) {
        e.next_try = now + retry_every_;
        return Outcome::StillFailing;
    }
    if (e.tries >= TRIES) {
        e.failed   = true;
        e.next_try = now + retry_every_;
        return Outcome::Failed;
    }
    e.next_try = now + retry_pause_;
    return Outcome::Retry;
}

bool StartFailures::started(int participant) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!engines_.erase(participant)) return false;
    ++version_;
    return true;
}

void StartFailures::claimed(int participant, uint64_t game) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = engines_.find(participant);
    if (it == engines_.end() || it->second.trying) return;
    it->second.trying      = true;
    it->second.trying_game = game;
    ++version_;
}

void StartFailures::released(uint64_t game) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, e] : engines_) {
        if (e.trying && e.trying_game == game) {
            e.trying = false;
            ++version_;
        }
    }
}

std::set<int> StartFailures::setAside(Clock::time_point now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<int> out;
    for (const auto& [id, e] : engines_) {
        if (e.trying || now < e.next_try) out.insert(id);
    }
    return out;
}

bool StartFailures::tryNow(int participant, Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = engines_.find(participant);
    if (it == engines_.end() || it->second.trying) return false;
    it->second.next_try = std::min(it->second.next_try, now);
    ++version_;
    return true;
}

void StartFailures::postpone(Clock::duration paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, e] : engines_) {
        if (!e.trying) e.next_try += paused;
    }
}

bool StartFailures::lastChance(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool any = false;
    for (auto& [id, e] : engines_) {
        if (!e.failed || e.trying || e.last_chance) continue;
        e.last_chance = true;
        e.next_try    = std::min(e.next_try, now);
        any           = true;
    }
    if (any) ++version_;
    return any;
}

void StartFailures::forget(int participant) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (engines_.erase(participant)) ++version_;
}

std::vector<StartFailures::Engine> StartFailures::engines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Engine> out;
    out.reserve(engines_.size());
    for (const auto& [id, e] : engines_) out.push_back(e);
    return out;
}

std::optional<StartFailures::Engine> StartFailures::engine(int participant) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = engines_.find(participant);
    if (it == engines_.end()) return std::nullopt;
    return it->second;
}

bool StartFailures::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engines_.empty();
}

bool StartFailures::deciding() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, e] : engines_) {
        if (!e.failed || e.trying) return true;
    }
    return false;
}

void StartFailures::setWaits(Clock::duration retry_pause, Clock::duration retry_every) {
    std::lock_guard<std::mutex> lock(mutex_);
    retry_pause_ = retry_pause;
    retry_every_ = retry_every;
}

void StartFailures::setStalled(bool stalled) {
    stalled_ = stalled;
    ++version_;
}

}  // namespace fcg::tour
