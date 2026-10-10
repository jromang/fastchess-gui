#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
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
    void logTo(const std::string& path);

    void add(std::string line, bool warning = false);

    struct Line {
        std::string text;
        bool warning = false;
    };

    // Copies the lines if they changed since `version`.
    bool copy(std::vector<Line>& out, uint64_t& version) const;

    void clear();

   private:
    Console() = default;

    static constexpr std::size_t MAX_LINES = 4000;

    mutable std::mutex mutex_;
    std::deque<Line> lines_;
    std::FILE* log_   = nullptr;
    uint64_t version_ = 1;
};

}  // namespace fcg
