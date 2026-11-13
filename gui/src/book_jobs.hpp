#pragma once

// The participants' PGN books turned into Polyglot books in the background (tour/pgn_book.hpp), one at a time, as
// soon as a setup has one: a start seldom has to wait for it, and when it does, it says so (see startGate).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tour/pgn_book.hpp>

namespace fcg {

class PgnBookJobs {
   public:
    struct Status {
        enum class State {
            Queued,     // to be prepared, after the one being prepared
            Preparing,  // being read
            Ready,      // in the cache, for the file as it is now
            Failed,     // cannot be prepared: `error`
        };
        State state     = State::Queued;
        double progress = 0;  // Preparing: the share of the file read
        tour::PgnBookStats stats;
        std::string error;
    };

    // A conversion that ended, for the console.
    struct Ended {
        std::string pgn;
        int plies = 0;  // of its games
        Status status;  // Ready or Failed
    };

    PgnBookJobs() = default;
    ~PgnBookJobs();  // a conversion in flight is cancelled, and waited for
    PgnBookJobs(const PgnBookJobs&)            = delete;
    PgnBookJobs& operator=(const PgnBookJobs&) = delete;

    // Where the book of `pgn` for `depth` stands. Asking is wanting: what shows a book asks at each frame, and so
    // does a start waiting for it; the next poll() starts its conversion when nothing else is being converted.
    Status want(const std::string& pgn, int depth, bool chess960);

    // At each frame, after the frame's want(): collects the conversion that ended, cancels the one nobody asked
    // for since the last poll (a depth changed meanwhile, the book replaced), starts the next one asked for.
    std::vector<Ended> poll();

    // A conversion is in flight.
    [[nodiscard]] bool busy() const noexcept { return job_.has_value(); }

   private:
    struct Key {
        std::string pgn;
        int plies                          = 0;
        bool chess960                      = false;
        auto operator<=>(const Key&) const = default;
    };
    struct Book {
        uintmax_t size = 0;  // the file as it was when its status was found
        std::filesystem::file_time_type time{};
        bool known = false;  // its status was found for the file as it is
        Status status;
        uint64_t asked = 0;  // the frame it was last asked for
    };
    struct Job {
        Key key;
        uintmax_t size = 0;  // the file it reads, as it was at its start
        std::filesystem::file_time_type time{};
        std::shared_ptr<std::atomic<double>> progress;
        std::shared_ptr<std::atomic_bool> cancel;
        std::future<tour::PreparedBook> result;
    };

    std::map<Key, Book> books_;
    std::optional<Job> job_;
    uint64_t frame_ = 1;
};

}  // namespace fcg
