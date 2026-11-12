#pragma once

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>

#include <core/memory/scope_guard.hpp>

namespace fastchess::util {

// An entry for the cache pool, should be guarded by a ScopeGuard to release the entry
// when it goes out of scope and make it available for other threads to use again.
template <typename T, typename ID>
class CachedEntry : public ScopeEntry {
   public:
    template <typename... ARGS>
    CachedEntry(const ID& identifier, ARGS&&... arg)
        : ScopeEntry(false), entry_(std::make_unique<T>(std::forward<ARGS>(arg)...)), id(identifier) {}

    T* operator->() noexcept { return entry_.get(); }
    const T* operator->() const noexcept { return entry_.get(); }

    [[nodiscard]] auto& get() noexcept { return entry_; }

   private:
    std::unique_ptr<T> entry_;
    ID id;
    std::uint64_t last_used_ = 0;  // when it was last handed out, in getEntry() calls

    template <typename TT, typename II>
    friend class CachePool;
};

// CachePool is a pool of objects which can be reused. An object is indentified by an ID
// type and value. The pool is thread safe.
//
// With a maximum size, the entries not in use that were used the longest ago make room for new ones: a
// tournament of hundreds of engines does not keep a process of each alive until its end.
template <typename T, typename ID>
class CachePool {
   public:
    using Entries = std::list<CachedEntry<T, ID>>;

    explicit CachePool(std::size_t max_size = 0) : max_size_(max_size) {}

    template <typename... ARGS>
    [[nodiscard]] auto getEntry(const ID& identifier, ARGS&&... arg) {
        // Destroyed after the lock is released: an engine takes a moment to quit.
        Entries evicted;
        std::lock_guard<std::mutex> lock(access_mutex_);
        ++uses_;

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->available_ && it->id == identifier) {
                // block the entry from being used by other threads
                it->available_ = false;
                it->last_used_ = uses_;
                return it;
            }
            ++it;
        }

        while (max_size_ > 0 && cache_.size() >= max_size_) {
            auto oldest = cache_.end();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->available_ && (oldest == cache_.end() || it->last_used_ < oldest->last_used_)) oldest = it;
            }
            if (oldest == cache_.end()) break;  // all in use
            evicted.splice(evicted.end(), cache_, oldest);
        }

        cache_.emplace_back(identifier, std::forward<ARGS>(arg)...);
        cache_.back().last_used_ = uses_;
        return std::prev(cache_.end());
    }

    // Takes out the entries not in use whose ID `retired` accepts (an engine withdrawn from the tournament...) and
    // hands them over: the caller destroys them once it holds no lock any more, as an engine that hangs can take
    // its kill timeout to quit.
    template <typename Retired>
    [[nodiscard]] Entries evictIdle(Retired retired) {
        Entries evicted;
        std::lock_guard<std::mutex> lock(access_mutex_);
        for (auto it = cache_.begin(); it != cache_.end();) {
            const auto next = std::next(it);
            if (it->available_ && retired(it->id)) evicted.splice(evicted.end(), cache_, it);
            it = next;
        }
        return evicted;
    }

    void deleteFromCache(const typename Entries::iterator& it) {
        assert(it != cache_.end());
        assert(!it->available_);
        // Destroyed after the lock is released, as in getEntry().
        Entries deleted;
        std::lock_guard<std::mutex> lock(access_mutex_);
        deleted.splice(deleted.end(), cache_, it);
    }

   private:
    Entries cache_;
    std::mutex access_mutex_;
    std::size_t max_size_ = 0;  // 0: no limit
    std::uint64_t uses_   = 0;
};

}  // namespace fastchess::util
