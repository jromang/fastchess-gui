#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fcg {

// Keeps the last lines fastchess prints (Logger::print) plus the GUI's own messages.
class Console {
   public:
    static Console& instance();

    // Installs the Logger sink. Call once at startup.
    void attach();

    // Also appends every line to this file (truncated first). A GUI program on Windows has no stderr.
    void logTo(const std::filesystem::path& path);

    void add(std::string line, bool warning = false);

    struct Line {
        std::string text;
        bool warning = false;
    };

    // Copies the lines if they changed since `version`.
    bool copy(std::vector<Line>& out, uint64_t& version) const;

    // How many lines were added so far, and the lines kept of those added after the first `added`: what fastchess
    // said during a tournament, for one.
    [[nodiscard]] uint64_t added() const;
    [[nodiscard]] std::vector<std::string> since(uint64_t added) const;

    void clear();

   private:
    Console() = default;

    static constexpr std::size_t MAX_LINES = 4000;

    mutable std::mutex mutex_;
    std::deque<Line> lines_;
    std::FILE* log_   = nullptr;
    uint64_t version_ = 1;
    uint64_t added_   = 0;
};

}  // namespace fcg
