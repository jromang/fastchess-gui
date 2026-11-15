#include "console.hpp"

#include <algorithm>

#include <core/logger/logger.hpp>
#include <tour/files.hpp>

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

void Console::logTo(const std::filesystem::path& path, bool append) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_) std::fclose(log_);
    log_ = tour::openFile(path, append ? "a" : "w");  // not writable by others, and the UTF-16 path on Windows
}

void Console::setListener(std::function<void(const Line&)> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(listener);
}

void Console::add(std::string line, bool warning, std::string source) {
    std::function<void(const Line&)> listener;
    Line added{std::move(line), warning, std::move(source)};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_) {
            if (added.source.empty()) {
                std::fprintf(log_, "%s%s\n", warning ? "! " : "", added.text.c_str());
            } else {
                std::fprintf(log_, "%s[%s] %s\n", warning ? "! " : "", added.source.c_str(), added.text.c_str());
            }
            std::fflush(log_);
        }
        listener = listener_;
        lines_.push_back(added);
        while (lines_.size() > MAX_LINES) lines_.pop_front();
        version_++;
        added_++;
    }
    if (listener) listener(added);
}

uint64_t Console::added() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return added_;
}

std::vector<std::string> Console::since(uint64_t added) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    // The last lines added are the last ones kept (a Clear may have dropped some).
    const auto count = static_cast<std::size_t>(std::min<uint64_t>(added_ - std::min(added, added_), lines_.size()));
    for (auto it = lines_.end() - static_cast<std::ptrdiff_t>(count); it != lines_.end(); ++it) out.push_back(it->text);
    return out;
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
