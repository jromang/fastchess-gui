#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <fmt/include/fmt/format.h>

#include "portable.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
    auto dir = fs::temp_directory_path() /
               fmt::format("fcg-tests-{}-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);
    return fs::absolute(dir).lexically_normal();
}

// The portable mode on for the time of a test: it is the program's, global.
struct PortableBase {
    explicit PortableBase(const fs::path& folder) { portable::setBase(folder); }
    ~PortableBase() { portable::setBase({}); }
    PortableBase(const PortableBase&)            = delete;
    PortableBase& operator=(const PortableBase&) = delete;
};

std::string json(const fs::path& file) {
    std::ifstream in(file);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("portable: the paths in the program's folder kept relative to it") {
#ifdef _WIN32
    const fs::path base         = "C:\\Tools\\fastchess-gui";
    const std::string engine    = "C:\\Tools\\fastchess-gui\\engines\\sf\\stockfish.exe";
    const std::string elsewhere = "D:\\engines\\lc0.exe";
#else
    const fs::path base         = "/media/usb/fastchess-gui";
    const std::string engine    = "/media/usb/fastchess-gui/engines/sf/stockfish";
    const std::string elsewhere = "/opt/engines/lc0";
#endif
#ifdef _WIN32
    CHECK(portable::stored(engine, base) == "engines/sf/stockfish.exe");
#else
    CHECK(portable::stored(engine, base) == "engines/sf/stockfish");
#endif
    CHECK(portable::stored(elsewhere, base) == elsewhere);
    CHECK(portable::stored("", base).empty());
    CHECK(portable::stored(engine, fs::path{}) == engine);  // not portable: as it is
    // A neighbour whose name starts the same is not inside.
    CHECK(portable::stored((base.parent_path() / "fastchess-gui-old" / "x").string(), base) ==
          (base.parent_path() / "fastchess-gui-old" / "x").string());
    CHECK(portable::stored((base / ".." / "other").string(), base) == (base / ".." / "other").string());

    CHECK(fs::path(portable::resolved(portable::stored(engine, base), base)) == fs::path(engine));
    CHECK(portable::resolved(elsewhere, base) == elsewhere);
    CHECK(portable::resolved("engines/sf/stockfish", fs::path{}) == "engines/sf/stockfish");

    // Moved: the same relative path, another folder.
    const auto moved = base.parent_path() / "moved";
    CHECK(fs::path(portable::resolved("engines/sf/stockfish", moved)) == moved / "engines" / "sf" / "stockfish");
}

TEST_CASE("portable: tablebase folders, a list") {
#ifdef _WIN32
    const fs::path base    = "C:\\fcg";
    const std::string list = "C:\\fcg\\syzygy\\345;D:\\syzygy\\6";
    const std::string kept = "syzygy/345;D:\\syzygy\\6";
#else
    const fs::path base    = "/fcg";
    const std::string list = "/fcg/syzygy/345:/data/syzygy/6";
    const std::string kept = "syzygy/345:/data/syzygy/6";
#endif
    CHECK(portable::storedList(list, base) == kept);
    CHECK(portable::resolvedList(kept, base) == list);
    CHECK(portable::storedList("", base).empty());
}

TEST_CASE("portable: the marker beside the program, or beside the macOS bundle") {
    const auto dir = tempDir("portable-marker");
    const auto exe = dir / "fastchess-gui";
    CHECK(portable::detect(exe).empty());
    std::ofstream(dir / portable::MARKER).put('\n');
    CHECK(portable::detect(exe) == dir);

    CHECK(portable::programFolder(dir / "fastchess-gui.app" / "Contents" / "MacOS" / "fastchess-gui") == dir);
    CHECK(portable::programFolder(dir / "bin" / "fastchess-gui") == dir / "bin");
    CHECK(portable::detect(dir / "fastchess-gui.app" / "Contents" / "MacOS" / "fastchess-gui") == dir);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("portable: the profile's files keep relative paths, and the folder can move") {
    const auto program = tempDir("portable-program");
    const auto data    = program / portable::DATA_FOLDER;
    const auto engine  = (program / "engines" / "sf" / "stockfish").string();
    const auto book    = (program / "books" / "8moves.epd").string();
    const auto outside = (program.parent_path() / "elsewhere.pgn").string();
    {
        const PortableBase on(program);
        const Storage storage(data);

        EngineEntry e;
        e.name = "Stockfish";
        e.cmd  = engine;
        storage.saveEngines({e});
        BookEntry b;
        b.name = "8 moves";
        b.path = book;
        storage.saveBooks({b});
        MatchSetup setup;
        setup.book        = book;
        setup.book_a.path = book;
        setup.pgn_out     = outside;
        setup.participants.push_back({});
        setup.participants[0].book.path = book;
        setup.tb_paths                  = (program / "syzygy").string();
        storage.saveSetup(setup);
        storage.saveQueue({{"next", setup}});
        storage.setEngineFolder(program / "engines");
        storage.setRatingPgns({program / "pgn" / "ccrl.pgn"});

        // On disk, relative; what is outside stays absolute.
        const auto engines_json = json(data / "engines.json");
        CHECK(engines_json.find("\"engines/sf/stockfish\"") != std::string::npos);
        CHECK(engines_json.find(program.string()) == std::string::npos);
        CHECK(json(data / "books.json").find("\"books/8moves.epd\"") != std::string::npos);
        const auto setup_json = json(data / "setup.json");
        CHECK(setup_json.find("\"books/8moves.epd\"") != std::string::npos);
        CHECK(setup_json.find("\"syzygy\"") != std::string::npos);
        CHECK(setup_json.find(fs::path(outside).filename().string()) != std::string::npos);
        CHECK(json(data / "queue.json").find("\"books/8moves.epd\"") != std::string::npos);
        CHECK(json(data / "settings.json").find("\"pgn/ccrl.pgn\"") != std::string::npos);

        // Read back: absolute, as the program uses them.
        CHECK(storage.loadEngines().at(0).cmd == engine);
        CHECK(storage.loadBooks().at(0).path == book);
        const auto loaded = storage.loadSetup();
        CHECK(loaded.book == book);
        CHECK(loaded.book_a.path == book);
        CHECK(loaded.participants.at(0).book.path == book);
        CHECK(loaded.pgn_out == outside);
        CHECK(storage.loadQueue().at(0).setup.book == book);
        CHECK(storage.engineFolder() == program / "engines");
        CHECK(storage.ratingPgns().at(0) == program / "pgn" / "ccrl.pgn");
        CHECK(fs::path(readSavedSetup(data / "setup.json")->book) == fs::path(book));
    }

    // The folder copied elsewhere: the paths follow.
    const auto moved = tempDir("portable-moved");
    fs::copy(program, moved, fs::copy_options::recursive);
    {
        const PortableBase on(moved);
        const Storage storage(moved / portable::DATA_FOLDER);
        CHECK(fs::path(storage.loadEngines().at(0).cmd) == moved / "engines" / "sf" / "stockfish");
        CHECK(fs::path(storage.loadSetup().book) == moved / "books" / "8moves.epd");
        CHECK(storage.loadSetup().pgn_out == outside);
        CHECK(storage.defaultRunsDir() == moved / portable::DATA_FOLDER / "runs");
    }

    // Not portable: the files are as they always were, absolute paths read as they are.
    {
        const auto plain = tempDir("portable-off");
        const Storage storage(plain);
        EngineEntry e;
        e.name = "Stockfish";
        e.cmd  = engine;
        storage.saveEngines({e});
        CHECK(json(plain / "engines.json").find(engine) != std::string::npos);
        CHECK(storage.loadEngines().at(0).cmd == engine);
        std::error_code ec;
        fs::remove_all(plain, ec);
    }

    std::error_code ec;
    fs::remove_all(program, ec);
    fs::remove_all(moved, ec);
}
