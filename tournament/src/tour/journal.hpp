#pragma once

// journal.jsonl: what happened in a tournament, one JSON object per line, appended and synced to disk as it
// happens. Never rewritten, so a crash can at worst cut the last line short.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <json.hpp>

namespace fcg::tour {

class Journal {
   public:
    // Reads every complete line. A last line without its newline (a power failure in the middle of a write) is
    // dropped, and with `repair` cut from the file. A line that is not JSON throws std::runtime_error with its
    // number; with `damaged`, it is skipped instead and described there.
    // `consumed`, when given, receives the bytes of the complete lines: where the next line will be appended.
    [[nodiscard]] static std::vector<nlohmann::json> read(const std::filesystem::path& file, bool repair = true,
                                                          std::vector<std::string>* damaged = nullptr,
                                                          uint64_t* consumed                = nullptr);

    // Opens the file for appending, creating it if needed. Throws when it cannot. A last line left without its
    // newline gets one, so that the next line is not glued to it.
    explicit Journal(const std::filesystem::path& file);
    ~Journal();

    Journal(const Journal&)            = delete;
    Journal& operator=(const Journal&) = delete;

    // Writes one line and syncs it to disk. Throws when the write fails, and from then on: what was written of
    // that line is not known. Thread safe.
    void append(const nlohmann::json& event);

    // Told about each line once it is written and synced: where it starts in the file, where it ends, and the event.
    // The tournament process sends them to the window, whose copy of the tournament follows them
    // (TournamentState::follow()). Called with the journal's lock held, and often the tournament's: it must not call
    // either.
    using Listener = std::function<void(uint64_t at, uint64_t end, const nlohmann::json& event)>;
    void setListener(Listener listener);

   private:
    std::FILE* file_ = nullptr;
    bool failed_     = false;
    uint64_t size_   = 0;  // where the next line goes
    Listener listener_;
    std::mutex mutex_;
};

}  // namespace fcg::tour
