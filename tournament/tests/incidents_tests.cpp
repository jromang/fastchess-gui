#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <json.hpp>

#include <tour/incidents.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;
using fastchess::MatchTermination;

namespace {

// A tournament folder as tournamentDirs() finds it, with a journal of games.
fs::path tournament(const fs::path& root, const std::string& name) {
    const auto dir = root / name;
    fs::create_directories(dir);
    std::ofstream(dir / "tournament.json") << "{}";
    return dir;
}

void game(const fs::path& dir, MatchTermination how, int64_t at, bool complete = true) {
    const nlohmann::json event = {
        {"t", "game_finished"}, {"result", "1-0"}, {"termination", static_cast<int>(how)}, {"reason", "x"}, {"at", at}};
    std::ofstream(dir / "journal.jsonl", std::ios::app) << event.dump() << (complete ? "\n" : "");
}

}  // namespace

TEST_CASE("incidents: crashes, time losses and illegal moves of the recent games, read as the journals grow") {
    const auto root = fs::temp_directory_path() / "fcg-tour-incidents";
    fs::remove_all(root);
    const auto a = tournament(root, "20261112-100000_a");
    const auto b = tournament(root, "20261112-110000_b");

    game(a, MatchTermination::NORMAL, 5000);
    game(a, MatchTermination::TIMEOUT, 5000);
    game(a, MatchTermination::DISCONNECT, 5000);
    game(a, MatchTermination::TIMEOUT, 100);  // before the period asked for
    game(b, MatchTermination::STALL, 6000);
    game(b, MatchTermination::ILLEGAL_MOVE, 6000);
    game(b, MatchTermination::ADJUDICATION, 6000);

    IncidentScanner scanner;
    auto found = scanner.scan(root, 1000);
    CHECK(found.time_losses == 1);
    CHECK(found.crashes == 2);
    CHECK(found.illegal_moves == 1);
    CHECK(found.total() == 4);

    // The lines added since: a line still being written waits for its end.
    game(b, MatchTermination::TIMEOUT, 7000, false);
    found = scanner.scan(root, 1000);
    CHECK(found.time_losses == 1);
    std::ofstream(b / "journal.jsonl", std::ios::app) << "\n";
    found = scanner.scan(root, 1000);
    CHECK(found.time_losses == 2);

    // Later, the older games are out of the period.
    found = scanner.scan(root, 5500);
    CHECK(found.total() == 3);

    // A journal not written in the period is not opened.
    fs::last_write_time(b / "journal.jsonl", fs::file_time_type::clock::now() - std::chrono::hours(48));
    CHECK(IncidentScanner().scan(root, 0).total() == 3);  // a's games only
    CHECK(IncidentScanner().scan(root, 0, std::chrono::hours(72)).total() == 6);

    fs::remove_all(root);
}

TEST_CASE("incidents: by engine, the losers named, the losses on time in the first moves apart") {
    const auto root = fs::temp_directory_path() / "fcg-tour-incidents-engines";
    fs::remove_all(root);
    const auto dir = root / "20261112-100000_rr";
    fs::create_directories(dir);
    const nlohmann::json spec = {
        {"participants", {{{"id", 0}, {"engine", {{"name", "Alpha"}}}}, {{"id", 1}, {"engine", {{"name", "Beta"}}}}}}};
    std::ofstream(dir / "tournament.json") << spec.dump();

    const auto played = [&](int white, int black, const char* result, MatchTermination how, unsigned plies) {
        const nlohmann::json event = {{"t", "game_finished"}, {"white", white}, {"black", black},
                                      {"result", result},     {"plies", plies}, {"termination", static_cast<int>(how)},
                                      {"reason", "x"},        {"at", 5000}};
        std::ofstream(dir / "journal.jsonl", std::ios::app) << event.dump() << "\n";
    };
    played(0, 1, "0-1", MatchTermination::TIMEOUT, 12);   // Alpha, on time in the first moves
    played(1, 0, "1-0", MatchTermination::TIMEOUT, 30);   // Alpha again
    played(0, 1, "1-0", MatchTermination::TIMEOUT, 140);  // Beta, late
    played(0, 1, "1-0", MatchTermination::DISCONNECT, 50);
    played(0, 1, "1/2-1/2", MatchTermination::TIMEOUT, 90);  // a draw: no loser
    // A participant added during the tournament.
    const nlohmann::json added = {{"t", "participant_added"},
                                  {"participant", {{"id", 2}, {"engine", {{"name", "Gamma"}}}}}};
    std::ofstream(dir / "journal.jsonl", std::ios::app) << added.dump() << "\n";
    played(2, 0, "0-1", MatchTermination::ILLEGAL_MOVE, 20);

    const auto found = IncidentScanner().scan(root, 1000);
    CHECK(found.time_losses == 4);
    CHECK(found.crashes == 1);
    CHECK(found.illegal_moves == 1);
    REQUIRE(found.engines.size() == 3);
    CHECK(found.engines[0].engine == "Alpha");  // 2, before Beta's 2 by name
    CHECK(found.engines[0].time_losses == 2);
    CHECK(found.engines[0].early_time_losses == 2);
    CHECK(found.engines[0].dir == dir);
    CHECK(found.engines[1].engine == "Beta");
    CHECK(found.engines[1].time_losses == 1);
    CHECK(found.engines[1].early_time_losses == 0);
    CHECK(found.engines[1].crashes == 1);
    CHECK(found.engines[2].engine == "Gamma");
    CHECK(found.engines[2].illegal_moves == 1);
    fs::remove_all(root);
}

TEST_CASE("incidents: a game an engine on a machine lost to the network is not its crash") {
    const auto root = fs::temp_directory_path() / "fcg-tour-incidents-network";
    fs::remove_all(root);
    const auto dir = tournament(root, "20261115-100000_net");
    const auto lost = [&](uint32_t encounter, int g) {
        const nlohmann::json event = {{"t", "game_finished"},
                                      {"encounter", encounter},
                                      {"game", g},
                                      {"white", 0},
                                      {"black", 1},
                                      {"result", "1-0"},
                                      {"termination", static_cast<int>(MatchTermination::DISCONNECT)},
                                      {"reason", "x"},
                                      {"at", 5000}};
        std::ofstream(dir / "journal.jsonl", std::ios::app) << event.dump() << "\n";
    };
    lost(0, 0);
    lost(0, 1);
    const nlohmann::json voided = {{"t", "games_voided"},
                                   {"games", nlohmann::json::array({nlohmann::json::array({0, 1})})},
                                   {"replay", true},
                                   {"why", "connection to its machine lost: gpu-box"},
                                   {"network", true},
                                   {"machine", "gpu-box"}};
    std::ofstream(dir / "journal.jsonl", std::ios::app) << voided.dump() << "\n";

    IncidentScanner scanner;
    const auto found = scanner.scan(root, 0);
    CHECK(found.crashes == 1);  // game (0, 0) only
    CHECK(found.network_cuts == 1);
    fs::remove_all(root);
}
