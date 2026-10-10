#include "console.hpp"

#include <core/logger/logger.hpp>

namespace fcg {

Console& Console::instance() {
    static Console console;
    return console;
}

void Console::attach() {
    fastchess::Logger::setPrintSink([](fastchess::Logger::Level level, std::string_view message) {
        // Logger::print may pass several lines at once.
        std::size_t start = 0;
        while (start < message.size()) {
            auto end = message.find('\n', start);
            if (end == std::string_view::npos) end = message.size();
            if (end > start) {
                Console::instance().add(std::string(message.substr(start, end - start)),
                                        level >= fastchess::Logger::Level::WARN);
            }
            start = end + 1;
        }
    });
}

void Console::logTo(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_) std::fclose(log_);
    log_ = std::fopen(path.c_str(), "w");
}

void Console::add(std::string line, bool warning) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_) {
        std::fprintf(log_, "%s%s\n", warning ? "! " : "", line.c_str());
        std::fflush(log_);
    }
    lines_.push_back({std::move(line), warning});
    while (lines_.size() > MAX_LINES) lines_.pop_front();
    version_++;
}

bool Console::copy(std::vector<Line>& out, uint64_t& version) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (version == version_) return false;
    out.assign(lines_.begin(), lines_.end());
    version = version_;
    return true;
}

void Console::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    version_++;
}

}  // namespace fcg
