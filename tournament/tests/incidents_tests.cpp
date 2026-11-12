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
