#include "book_jobs.hpp"

#include <chrono>
#include <system_error>

#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;

PgnBookJobs::~PgnBookJobs() {
    if (job_) {
        *job_->cancel = true;
        job_->result.wait();
    }
}

PgnBookJobs::Status PgnBookJobs::want(const std::string& pgn, int depth, bool chess960) {
    const Key key{pgn, tour::pgnBookPlies(depth), chess960};
    auto& book = books_[key];
    book.asked = frame_;

    std::error_code ec;
    const auto size = fs::file_size(pgn, ec);
    const auto time = ec ? fs::file_time_type{} : fs::last_write_time(pgn, ec);
    if (ec) {
        book.known = false;
        Status missing;
        missing.state = Status::State::Failed;
        missing.error = TR("the file is not found");
        return missing;
    }
    // Found for the file as it is: the cache is looked at again only when the file changes.
    if (!book.known || book.size != size || book.time != time) {
        book.known  = true;
        book.size   = size;
        book.time   = time;
        book.status = {};
        if (const auto cached = tour::cachedPgnBook(pgn, key.plies, chess960)) {
            book.status.state = Status::State::Ready;
            book.status.stats = cached->stats;
        }
    }
    if (job_ && job_->key == key && book.status.state == Status::State::Queued) {
        auto status     = book.status;
        status.state    = Status::State::Preparing;
        status.progress = *job_->progress;
        return status;
    }
    return book.status;
}

void PgnBookJobs::retry(const std::string& pgn, int depth, bool chess960) {
    const auto it = books_.find(Key{pgn, tour::pgnBookPlies(depth), chess960});
    if (it != books_.end() && it->second.status.state == Status::State::Failed) it->second.known = false;
}

std::vector<PgnBookJobs::Ended> PgnBookJobs::poll() {
    std::vector<Ended> ended;
    if (job_ && job_->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto& book = books_[job_->key];
        // Its result stands for the file as it was read; the file changed since, the cache is looked at again.
        const bool same = book.known && book.size == job_->size && book.time == job_->time;
        try {
            const auto prepared = job_->result.get();
            if (same) {
                book.status       = {};
                book.status.state = Status::State::Ready;
                book.status.stats = prepared.stats;
                ended.push_back({job_->key.pgn, job_->key.plies, book.status});
            } else {
                book.known = false;
            }
        } catch (const tour::BookCancelled&) {
            book.known = false;
        } catch (const std::exception& e) {
            if (same) {
                book.status       = {};
                book.status.state = Status::State::Failed;
                book.status.error = e.what();
                ended.push_back({job_->key.pgn, job_->key.plies, book.status});
            } else {
                book.known = false;
            }
        }
        job_.reset();
    }

    // Nobody asked for it since the last poll: its depth changed, or it is no longer chosen.
    if (job_ && books_[job_->key].asked < frame_) *job_->cancel = true;

    if (!job_) {
        for (const auto& [key, book] : books_) {
            if (book.asked < frame_ || !book.known || book.status.state != Status::State::Queued) continue;
            Job job;
            job.key      = key;
            job.size     = book.size;
            job.time     = book.time;
            job.progress = std::make_shared<std::atomic<double>>(0.0);
            job.cancel   = std::make_shared<std::atomic_bool>(false);
            job.result   = std::async(std::launch::async, [key, progress = job.progress, cancel = job.cancel] {
                return tour::preparePgnBook(key.pgn, key.plies, key.chess960, [&](double share) {
                    *progress = share;
                    return !*cancel;
                });
            });
            job_         = std::move(job);
            break;
        }
    }

    // Those not asked for in a while are forgotten: a depth dragged through many values leaves many behind.
    for (auto it = books_.begin(); it != books_.end();) {
        const bool running = job_ && job_->key == it->first;
        it                 = !running && it->second.asked + 600 < frame_ ? books_.erase(it) : std::next(it);
    }
    ++frame_;
    return ended;
}

}  // namespace fcg
