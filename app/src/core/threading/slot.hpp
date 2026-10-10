#pragma once

namespace fastchess::util {

// Index of the thread pool worker running on this thread, -1 on any other thread.
// The tournament pool numbers its workers 0..concurrency-1, so a front end can use
// it to tell concurrent games apart.
inline thread_local int t_worker_index = -1;

}  // namespace fastchess::util
