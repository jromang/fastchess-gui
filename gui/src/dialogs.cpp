#include "dialogs.hpp"

#include <SDL3/SDL_dialog.h>

namespace fcg {

struct Dialogs::Request {
    Dialogs* owner;
    Purpose purpose;
    // SDL keeps pointers to the filter strings until the callback has run.
    std::vector<std::pair<std::string, std::string>> filter_strings;
    std::vector<SDL_DialogFileFilter> filters;
    std::string start;
};

void Dialogs::callback(void* userdata, const char* const* files, int) {
    auto* request = static_cast<Request*>(userdata);
    auto* owner   = request->owner;

    {
        std::lock_guard<std::mutex> lock(owner->mutex_);
        if (files && files[0]) owner->answers_.push_back({request->purpose, files[0]});
        owner->pending_--;
    }

    delete request;
}

void Dialogs::openFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    auto* request = new Request{this, purpose, filters, {}, start};
    for (const auto& [name, pattern] : request->filter_strings)
        request->filters.push_back({name.c_str(), pattern.c_str()});

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_++;
    }

    SDL_ShowOpenFileDialog(
        &Dialogs::callback, request, window_, request->filters.empty() ? nullptr : request->filters.data(),
        static_cast<int>(request->filters.size()), request->start.empty() ? nullptr : request->start.c_str(), false);
}

void Dialogs::openFolder(Purpose purpose, const std::string& start) {
    auto* request = new Request{this, purpose, {}, {}, start};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_++;
    }

    SDL_ShowOpenFolderDialog(&Dialogs::callback, request, window_,
                             request->start.empty() ? nullptr : request->start.c_str(), false);
}

void Dialogs::saveFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                       const std::string& start) {
    auto* request = new Request{this, purpose, filters, {}, start};
    for (const auto& [name, pattern] : request->filter_strings)
        request->filters.push_back({name.c_str(), pattern.c_str()});

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_++;
    }

    SDL_ShowSaveFileDialog(
        &Dialogs::callback, request, window_, request->filters.empty() ? nullptr : request->filters.data(),
        static_cast<int>(request->filters.size()), request->start.empty() ? nullptr : request->start.c_str());
}

std::optional<Dialogs::Answer> Dialogs::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (answers_.empty()) return std::nullopt;

    auto answer = answers_.front();
    answers_.erase(answers_.begin());
    return answer;
}

bool Dialogs::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ > 0;
}

}  // namespace fcg
