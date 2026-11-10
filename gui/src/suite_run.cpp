#include "suite_run.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include <engine/uci_engine.hpp>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg::tour {

// Found by nlohmann's lookup in the answer's own namespace.
void to_json(nlohmann::json& j, const SuiteAnswer& a) {
    j = {{"solved", a.solved}, {"move", a.move}, {"ms", a.time_ms}, {"depth", a.depth}, {"points", a.points}};
}

void from_json(const nlohmann::json& j, SuiteAnswer& a) {
    a.solved  = j.value("solved", false);
    a.move    = j.value("move", std::string{});
    a.time_ms = j.value("ms", int64_t{-1});
    a.depth   = j.value("depth", 0);
    a.points  = j.value("points", 0);
}

}  // namespace fcg::tour

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

std::string now(const char* format) {
    const auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), format, &tm);
    return buffer;
}

}  // namespace

SuiteColumn::Totals SuiteColumn::totals() const {
    Totals t;
    for (const auto& a : answers) {
        if (!a) continue;
        t.answered++;
        t.points += a->points;
        if (a->solved) {
            t.solved++;
            t.time_ms += std::max<int64_t>(0, a->time_ms);
        }
    }
    return t;
}

std::string suiteLimitText(const SuiteSetup& setup) {
    switch (setup.limit) {
        case LimitKind::Nodes:
            return trf("{} nodes", ui::formatCount(setup.nodes));
        case LimitKind::Depth:
            return trf("depth {}", setup.depth);
        default:
            return trf("{} s/move", fmt::format("{:g}", setup.move_s));
    }
}

std::string suiteCsv(const tour::Suite& suite, const std::vector<SuiteColumn>& columns) {
    std::string out = "id,solution";
    for (const auto& c : columns) {
        const auto name = c.when.empty() ? c.engine : c.engine + " " + c.when;
        out += "," + csvField(name) + "," + csvField(name + " ms");
        if (suite.hasPoints()) out += "," + csvField(name + " points");
    }
    out += "\n";
    for (std::size_t i = 0; i < suite.positions.size(); ++i) {
        const auto& p = suite.positions[i];
        out += csvField(p.id) + "," + csvField(p.best_text);
        for (const auto& c : columns) {
            const auto& a = i < c.answers.size() ? c.answers[i] : std::nullopt;
            out += a ? fmt::format(",{}{},{}", csvField(a->move), a->solved ? " ✓" : "", a->time_ms) : ",,";
            if (suite.hasPoints()) out += a ? fmt::format(",{}", a->points) : ",";
        }
        out += "\n";
    }
    out += "total,";
    for (const auto& c : columns) {
        const auto t = c.totals();
        out += fmt::format(",{}/{},{}", t.solved, suite.positions.size(), t.solved ? t.time_ms / t.solved : 0);
        if (suite.hasPoints()) out += fmt::format(",{}", t.points);
    }
    return out + "\n";
}

fs::path saveSuiteRun(const fs::path& dir, const fs::path& suite_file, std::size_t positions,
                      const std::vector<SuiteColumn>& columns) {
    nlohmann::json j = {
        {"suite", suite_file.filename().string()}, {"positions", positions}, {"when", now("%Y-%m-%d %H:%M")}};
    auto& cols = j["columns"] = nlohmann::json::array();
    for (const auto& c : columns) {
        if (!c.when.empty()) continue;  // a saved run is already kept
        auto answers = nlohmann::json::array();
        for (const auto& a : c.answers) answers.push_back(a ? nlohmann::json(*a) : nlohmann::json());
        cols.push_back({{"engine", c.engine}, {"limit", c.limit}, {"answers", answers}});
    }
    const auto file = dir / (suite_file.stem().string() + "-" + now("%Y%m%d-%H%M%S") + ".json");
    return writeJson(file, j) ? file : fs::path();
}

std::vector<std::pair<fs::path, std::string>> savedSuiteRuns(const fs::path& dir, const fs::path& suite_file,
                                                             std::size_t positions) {
    std::vector<std::pair<fs::path, std::string>> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        const auto j = readJson<nlohmann::json>(it->path());
        if (!j || j->value("suite", std::string{}) != suite_file.filename().string() ||
            j->value("positions", std::size_t{0}) != positions) {
            continue;
        }
        std::string engines;
        for (const auto& c : j->value("columns", nlohmann::json::array())) {
            engines += (engines.empty() ? "" : ", ") + c.value("engine", std::string{});
        }
        out.emplace_back(it->path(), fmt::format("{} · {}", j->value("when", std::string{}), engines));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    return out;
}

std::vector<SuiteColumn> loadSuiteRun(const fs::path& file) {
    std::vector<SuiteColumn> out;
    const auto j = readJson<nlohmann::json>(file);
    if (!j) return out;
    for (const auto& c : j->value("columns", nlohmann::json::array())) {
        SuiteColumn col;
        col.engine = c.value("engine", std::string{});
        col.limit  = c.value("limit", std::string{});
        col.when   = j->value("when", std::string{});
        for (const auto& a : c.value("answers", nlohmann::json::array())) {
            col.answers.push_back(a.is_null() ? std::nullopt : std::optional(a.get<tour::SuiteAnswer>()));
        }
        out.push_back(std::move(col));
    }
    return out;
}

SuiteRunner::~SuiteRunner() {
    stop();
    join();
}

void SuiteRunner::join() {
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

void SuiteRunner::start(const tour::Suite& suite, std::vector<fastchess::EngineConfiguration> engines,
                        const SuiteSetup& setup, int concurrency) {
    stop();
    join();
    suite_   = suite;
    engines_ = std::move(engines);
    setup_   = setup;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        columns_.clear();
        for (const auto& e : engines_) {
            SuiteColumn c;
            c.engine = e.name;
            c.limit  = suiteLimitText(setup);
            c.answers.resize(suite_.positions.size());
            columns_.push_back(std::move(c));
        }
    }
    next_     = 0;
    answered_ = 0;
    total_    = engines_.size() * suite_.positions.size();
    stop_     = false;
    // Engine after engine: a worker keeps one engine running, and starts the next when it moves on to it.
    const auto n = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(1, concurrency)), 1,
                                           std::max<std::size_t>(total_, 1));
    active_      = static_cast<int>(n);
    for (std::size_t i = 0; i < n; ++i) workers_.emplace_back([this] { work(); });
}

void SuiteRunner::stop() { stop_ = true; }

bool SuiteRunner::running() const { return active_ > 0; }

std::vector<SuiteColumn> SuiteRunner::columns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return columns_;
}

void SuiteRunner::work() {
    std::unique_ptr<fastchess::engine::UciEngine> engine;
    std::size_t current         = ~std::size_t(0);
    const std::size_t positions = suite_.positions.size();

    while (!stop_) {
        const auto task = next_++;
        if (task >= total_) break;
        const auto e = task / positions, p = task % positions;

        bool failed = false;
        if (e != current || !engine) {
            engine.reset();
            engine  = std::make_unique<fastchess::engine::UciEngine>(engines_[e], false);
            current = e;
            if (!engine->start(std::nullopt) || !engine->refreshUci()) failed = true;
        }
        const auto answer = failed ? tour::SuiteAnswer{} : think(*engine, suite_.positions[p], failed);
        // A crashed or hung engine is started again for the next position.
        if (failed) engine.reset();

        std::lock_guard<std::mutex> lock(mutex_);
        columns_[e].answers[p] = answer;
        answered_++;
    }
    if (engine) engine->quit();
    active_--;
}

tour::SuiteAnswer SuiteRunner::think(fastchess::engine::UciEngine& engine, const tour::SuitePosition& position,
                                     bool& failed) {
    using namespace std::chrono;
    tour::SuiteSearch search(position, setup_.hold);
    if (!engine.ucinewgame() || !engine.writeEngine("position fen " + position.fen)) {
        failed = true;
        return {};
    }

    std::string go;
    milliseconds timeout{};
    switch (setup_.limit) {
        case LimitKind::Nodes:
            go      = fmt::format("go nodes {}", setup_.nodes);
            timeout = minutes(10);
            break;
        case LimitKind::Depth:
            go      = fmt::format("go depth {}", setup_.depth);
            timeout = minutes(10);
            break;
        default: {
            const auto ms = std::max<int64_t>(1, std::llround(setup_.move_s * 1000));
            go            = fmt::format("go movetime {}", ms);
            timeout       = milliseconds(ms) + seconds(5);
        }
    }

    const auto start = steady_clock::now();
    const auto since = [&] { return duration_cast<milliseconds>(steady_clock::now() - start).count(); };
    bool stopped     = false;
    engine.setLineCallback([&](const std::string& line) {
        if (stopped) return;
        const bool held = line.rfind("info", 0) == 0 && search.info(line, since());
        if (held || stop_) {
            stopped = true;
            (void)engine.writeEngine("stop");
        }
    });
    const bool sent = engine.writeEngine(go);
    const auto read = sent ? engine.readEngine("bestmove", timeout).code : fastchess::engine::process::Status::ERR;
    engine.setLineCallback({});
    const auto elapsed = since();

    if (read != fastchess::engine::process::Status::OK) {
        failed = true;
        return search.finish("", elapsed);
    }
    return search.finish(engine.bestmove(false).value_or(""), elapsed);
}

}  // namespace fcg
