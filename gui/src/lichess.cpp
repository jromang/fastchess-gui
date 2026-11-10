#include "lichess.hpp"

#include <ctime>

#include <fmt/include/fmt/format.h>

#include "storage.hpp"

namespace fcg {

namespace {

constexpr auto PUSH_EVERY = std::chrono::seconds(8);

std::string headers(const std::string& event, uint32_t round, const std::string& white, const std::string& black,
                    int white_elo, int black_elo, const std::string& result, const std::string& fen, bool frc) {
    const auto tag = [](const char* name, const std::string& value) {
        std::string escaped;
        for (const char c : value) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return fmt::format("[{} \"{}\"]\n", name, escaped);
    };
    std::string out = tag("Event", event) + tag("Site", "fastchess-gui") + tag("Round", std::to_string(round)) +
                      tag("White", white) + tag("Black", black) + tag("Result", result);
    if (white_elo > 0) out += tag("WhiteElo", std::to_string(white_elo));
    if (black_elo > 0) out += tag("BlackElo", std::to_string(black_elo));
    if (frc) out += tag("Variant", "Chess960");
    if (!fen.empty()) out += tag("SetUp", "1") + tag("FEN", fen);
    return out;
}

std::string key(uint32_t round, const std::string& white, const std::string& black) {
    return fmt::format("{}|{}|{}", round, white, black);
}

std::string clock(int64_t ms) {
    const auto s = std::max<int64_t>(0, ms) / 1000;
    return fmt::format("[%clk {}:{:02}:{:02}]", s / 3600, s / 60 % 60, s % 60);
}

constexpr std::string_view START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::string now() {
    const auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    return buffer;
}

}  // namespace

std::string LichessGame::pgn(std::size_t annotate_from) const {
    std::string out = headers + "\n";
    bool white      = white_first;
    int number      = first_number;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (white) {
            out += fmt::format("{}. ", number);
        } else if (i == 0) {
            out += fmt::format("{}... ", number);
        }
        out += moves[i].san;
        if (i >= annotate_from && !moves[i].comment.empty()) out += " {" + moves[i].comment + "}";
        out += ' ';
        if (!white) number++;
        white = !white;
    }
    return out + result + "\n\n";
}

LichessGame lichessLiveGame(const LiveGame& g, const std::string& event, int white_elo, int black_elo, bool frc) {
    // The game number is 1-based; its encounter, the round of its PGN, too.
    const auto round = static_cast<uint32_t>((std::max<uint64_t>(1, g.number) - 1) / 2 + 1);
    LichessGame game;
    game.key          = key(round, g.white, g.black);
    game.headers      = headers(event, round, g.white, g.black, white_elo, black_elo, "*",
                                frc || g.start_fen != START ? g.start_fen : "", frc);
    const auto space  = g.start_fen.find(' ');
    game.white_first  = space == std::string::npos || g.start_fen[space + 1] == 'w';
    const auto last   = g.start_fen.rfind(' ');
    game.first_number = last == std::string::npos ? 1 : std::max(1, std::atoi(g.start_fen.c_str() + last + 1));

    bool white   = game.white_first;
    const auto n = g.san.size();
    for (std::size_t i = 0; i < n; ++i) {
        LichessGame::Move m{g.san[i], {}};
        // The last move of each side is one of the last two: its clock is the side's clock now.
        if (g.clocks && i + 2 >= n) {
            m.comment = clock(white ? g.white_ms : g.black_ms);
        }
        game.moves.push_back(std::move(m));
        white = !white;
    }
    return game;
}

LichessGame lichessFinishedGame(std::string_view pgn, const std::string& event, uint32_t round,
                                const std::string& white, const std::string& black, int white_elo, int black_elo,
                                const std::string& result, bool frc) {
    LichessGame game;
    game.key      = key(round, white, black);
    game.result   = result;
    game.finished = true;

    std::string fen;
    if (const auto at = pgn.find("[FEN \""); at != std::string_view::npos) {
        const auto end = pgn.find("\"]", at + 6);
        if (end != std::string_view::npos) fen = std::string(pgn.substr(at + 6, end - at - 6));
    }
    game.headers = headers(event, round, white, black, white_elo, black_elo, result, fen, frc);
    if (!fen.empty()) {
        const auto space  = fen.find(' ');
        game.white_first  = space == std::string::npos || fen[space + 1] == 'w';
        const auto last   = fen.rfind(' ');
        game.first_number = last == std::string::npos ? 1 : std::max(1, std::atoi(fen.c_str() + last + 1));
    }

    // The moves; from each comment, the clock (without milliseconds): the one thing of a comment lichess keeps in a
    // broadcast, which drops the evaluations and the text.
    auto body           = pgn.find("\n\n");
    body                = body == std::string_view::npos ? pgn.size() : body + 2;
    const auto movetext = pgn.substr(body);
    const auto command  = [](std::string_view comment, std::string_view name) -> std::string {
        const auto at = comment.find(name);
        if (at == std::string_view::npos) return {};
        const auto start = at + name.size();
        const auto value = comment.substr(start, comment.find(']', start) - start);
        return std::string(value.substr(0, value.find('.')));  // "0:00:59.120": the milliseconds left out
    };
    for (std::size_t i = 0; i < movetext.size();) {
        const char c = movetext[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++i;
        } else if (c == '{') {
            const auto end     = movetext.find('}', i);
            const auto comment = movetext.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
            if (const auto clock = command(comment, "[%clk "); !game.moves.empty() && !clock.empty()) {
                game.moves.back().comment = "[%clk " + clock + "]";
            }
            i = end == std::string_view::npos ? movetext.size() : end + 1;
        } else {
            auto end = movetext.find_first_of(" \n\r\t{", i);
            if (end == std::string_view::npos) end = movetext.size();
            const auto token  = movetext.substr(i, end - i);
            const bool number = std::isdigit(static_cast<unsigned char>(token[0])) && token.back() == '.';
            const bool over   = token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
            if (!number && !over) game.moves.push_back({std::string(token), {}});
            i = end;
        }
    }
    return game;
}

std::optional<std::string> lichessRoundId(std::string_view text) {
    while (!text.empty() && (text.back() == '/' || text.back() == ' ')) text.remove_suffix(1);
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto slash = text.rfind('/');
    const auto id    = slash == std::string_view::npos ? text : text.substr(slash + 1);
    if (id.size() != 8) return std::nullopt;
    for (const char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return std::string(id);
}

LichessRelay::~LichessRelay() { stop(); }

void LichessRelay::start(Settings settings) {
    stop();
    settings_ = std::move(settings);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        live_.clear();
        pending_.clear();
    }
    stopping_ = false;
    running_  = true;
    thread_   = std::thread([this] { loop(); });
}

void LichessRelay::stop() {
    if (!thread_.joinable()) return;
    stopping_ = true;
    wake_.notify_all();
    thread_.join();
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
}

LichessRelay::Status LichessRelay::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void LichessRelay::offer(std::vector<LichessGame> live, std::vector<LichessGame> finished) {
    std::lock_guard<std::mutex> lock(mutex_);
    live_ = std::move(live);
    for (auto& g : finished) pending_[g.key] = std::move(g);
}

HttpResult LichessRelay::post(const std::string& path, const std::string& type, const std::string& body) {
    return httpPost({settings_.base + path, settings_.token, type, body});
}

void LichessRelay::save() { writeJson(settings_.dir / "lichess.json", target_); }

bool LichessRelay::newRound() {
    const auto name = fmt::format("Part {}", target_.round_index);
    const auto r    = post("/broadcast/" + target_.tour_id + "/new", "application/x-www-form-urlencoded",
                           formEncode({{"name", name}}));
    std::string error;
    if (r.status == 200) {
        try {
            const auto j     = nlohmann::json::parse(r.body);
            target_.round_id = j.at("round").at("id").get<std::string>();
            target_.url      = j.at("round").value("url", settings_.base + "/broadcast/-/-/" + target_.round_id);
            save();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.error =
        fmt::format("lichess: cannot make a round ({}): {}", r.status, error.empty() ? r.error + r.body : error);
    return false;
}

bool LichessRelay::prepare() {
    // A round given by its URL or id; otherwise the one made earlier for this tournament, or a new broadcast.
    if (auto saved = readJson<nlohmann::json>(settings_.dir / "lichess.json")) {
        try {
            target_ = saved->get<LichessTarget>();
        } catch (...) {
            target_ = {};
        }
    }
    if (!settings_.round.empty()) {
        const auto id = lichessRoundId(settings_.round);
        if (!id) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.error = "lichess: not a broadcast round URL or id: " + settings_.round;
            return false;
        }
        if (target_.round_id != *id) {
            target_          = {};
            target_.round_id = *id;
            target_.url = settings_.round.find('/') != std::string::npos ? settings_.round
                                                                         : settings_.base + "/broadcast/-/-/" + *id;
            save();
        }
    } else if (target_.round_id.empty() || target_.tour_id.empty()) {
        auto name = settings_.name.substr(0, 80);
        while (name.size() < 3) name += '.';
        const auto r = post("/broadcast/new", "application/x-www-form-urlencoded",
                            formEncode({{"name", name},
                                        {"visibility", settings_.listed ? "public" : "unlisted"},
                                        {"markdown", "Engine games played with fastchess-gui."}}));
        std::string error;
        if (r.status == 200) {
            try {
                target_         = {};
                target_.tour_id = nlohmann::json::parse(r.body).at("tour").at("id").get<std::string>();
            } catch (const std::exception& e) {
                error = e.what();
            }
        }
        if (target_.tour_id.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.error = fmt::format("lichess: cannot make the broadcast ({}): {}", r.status,
                                        error.empty() ? r.error + r.body : error);
            return false;
        }
        if (!newRound()) return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.url     = target_.url;
    status_.running = true;
    status_.games   = target_.rounds.size();
    return true;
}

void LichessRelay::push() {
    std::vector<LichessGame> games;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& g : live_) {
            if (!target_.finished.count(g.key) && !pending_.count(g.key)) games.push_back(g);
        }
        for (const auto& [k, g] : pending_) {
            if (!target_.finished.count(k)) games.push_back(g);
        }
    }
    if (games.empty()) return;

    // Each game goes to the round it was first pushed to; new ones to the current round, or to a new one when it
    // is full (when the broadcast was made here).
    std::map<std::string, std::vector<const LichessGame*>> by_round;
    std::size_t in_round = 0;
    for (const auto& [k, r] : target_.rounds) in_round += r == target_.round_id;
    for (const auto& g : games) {
        auto it = target_.rounds.find(g.key);
        if (it == target_.rounds.end()) {
            if (in_round >= LICHESS_ROUND_GAMES && !target_.tour_id.empty()) {
                target_.round_index++;
                if (!newRound()) return;
                in_round = 0;
            }
            it = target_.rounds.emplace(g.key, target_.round_id).first;
            in_round++;
        }
        by_round[it->second].push_back(&g);
    }

    std::string error;
    for (const auto& [round, list] : by_round) {
        std::string body;
        for (const auto* g : list) body += g->pgn(target_.annotated[g->key]);
        const auto r = post("/api/broadcast/round/" + round + "/push", "text/plain", body);
        if (r.status == 429) {
            not_before_ = std::chrono::steady_clock::now() + std::chrono::minutes(1);
            error       = "lichess: too many requests, waiting a minute";
            break;
        }
        if (r.status != 200) {
            error = fmt::format("lichess ({}): {}", r.status, r.status == 0 ? r.error : r.body);
            continue;
        }
        // The games lichess could not read say why.
        try {
            for (const auto& g : nlohmann::json::parse(r.body).value("games", nlohmann::json::array())) {
                if (g.contains("error")) error = "lichess: " + g.at("error").get<std::string>();
            }
        } catch (...) {
        }
        for (const auto* g : list) {
            if (g->finished) target_.finished.insert(g->key);
            auto& annotated = target_.annotated[g->key];
            annotated       = std::max(annotated, g->moves.size());
        }
    }
    save();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& k : target_.finished) pending_.erase(k);
    status_.error     = error;
    status_.last_push = now();
    status_.games     = target_.rounds.size();
    status_.url       = target_.url;
}

void LichessRelay::loop() {
    if (!prepare()) {
        running_ = false;
        return;
    }
    auto next = std::chrono::steady_clock::now();
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_until(lock, std::max(next, not_before_), [&] { return stopping_.load(); });
        }
        const bool last = stopping_;
        if (!last && std::chrono::steady_clock::now() < not_before_) continue;
        push();
        if (last) break;
        next = std::chrono::steady_clock::now() + PUSH_EVERY;
    }
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    status_.running = false;
}

}  // namespace fcg
