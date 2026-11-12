#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <game/pgn/pgn_builder.hpp>

#include <tour/legacy.hpp>
#include <tour/state.hpp>
#include <tour/summary.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

std::string pgnGame(const std::string& white, const std::string& black, chess::GameResult white_result,
                    std::size_t round, fastchess::pgn::CommentStyle style) {
    fastchess::EngineConfiguration w, b;
    w.name                  = white;
    b.name                  = black;
    const auto black_result = white_result == chess::GameResult::WIN    ? chess::GameResult::LOSE
                              : white_result == chess::GameResult::LOSE ? chess::GameResult::WIN
                                                                        : chess::GameResult::DRAW;
    fastchess::MatchData data(chess::constants::STARTPOS);
    data.players     = {fastchess::MatchData::PlayerInfo(w, white_result),
                        fastchess::MatchData::PlayerInfo(b, black_result)};
    data.termination = fastchess::MatchTermination::ADJUDICATION;
    data.reason      = "test";
    data.duration    = "00:01:05";
    return fastchess::pgn::PgnBuilder(fastchess::config::Pgn{}, data, round, style).get();
}

}  // namespace

TEST_CASE("a match folder of fastchess-gui 0.2 becomes a finished tournament") {
    const auto root = fs::temp_directory_path() / "fcg-tour-legacy";
    fs::remove_all(root);
    const auto dir = root / "20261001-120000_dev_vs_main";
    fs::create_directories(dir);

    // config.json as fastchess' saveJson() writes it.
    nlohmann::ordered_json config = fastchess::config::Tournament{};
    std::vector<fastchess::EngineConfiguration> engines(2);
    engines[0].name   = "dev";
    engines[1].name   = "main";
    config["engines"] = engines;
    config["stats"]   = nlohmann::json::object();
    std::ofstream(dir / "config.json") << config.dump(4);

    // Pair 1 complete (dev white wins, then draw), pair 2 half played; the second game finished first.
    using chess::GameResult;
    for (const auto style : {fastchess::pgn::CommentStyle::Fastchess, fastchess::pgn::CommentStyle::ChessBase}) {
        const auto file = style == fastchess::pgn::CommentStyle::Fastchess ? "games.pgn" : "games-chessbase.pgn";
        std::ofstream out(dir / file, std::ios::binary);
        out << pgnGame("main", "dev", GameResult::DRAW, 1, style);
        out << pgnGame("other", "engine", GameResult::WIN, 1, style);  // not imported, but not removed either
        out << pgnGame("dev", "main", GameResult::WIN, 1, style);
        out << pgnGame("dev", "main", GameResult::LOSE, 2, style);
    }

    const auto pgn = [&] {
        std::ifstream in(dir / "games.pgn", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto original = pgn();

    REQUIRE(legacyRuns(root).size() == 1);
    importLegacyRun(dir);
    CHECK(legacyRuns(root).empty());

    const auto summary = summarize(dir);
    REQUIRE(summary);
    CHECK(summary->played == 3);
    CHECK(summary->finished);
    CHECK(summary->spec.name == "dev vs main");

    auto state         = TournamentState::open(dir);
    const auto results = state->results();
    REQUIRE(results.size() == 3);
    CHECK(pgn() == original);
    CHECK(results[0].encounter == 0);
    CHECK(results[0].game == 1);  // main had White
    CHECK(results[1].game == 0);
    CHECK(results[2].encounter == 1);

    // One complete pair: dev won 1.5 - 0.5.
    const auto stats = state->pairStats(0, 1);
    CHECK(stats.sum() == 2);
    CHECK(stats.wins == 1);
    CHECK(stats.draws == 1);
    CHECK(stats.penta_WD == 1);

    // Games are read back from the original files.
    const auto text = readSpan(state->pgnPath(), results[2].pgn);
    REQUIRE(text);
    CHECK(text->find("[Round \"2\"]") != std::string::npos);
    CHECK(readSpan(state->chessbasePgnPath(), results[2].pgn_chessbase)->find("[Round \"2\"]") != std::string::npos);
}
