#pragma once

// The Syzygy tablebases a tournament uses: the files each piece count needs, and what its folders hold.

#include <chrono>
#include <cstddef>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fcg {

// The Syzygy tables of exactly `pieces` pieces (3 to 7, none otherwise), named as their files are, without the
// extension: the side with more pieces first, or with as many the stronger one, each side's pieces in the order
// KQRBNP ("KQvK", "KRvKN", "KRPPvKR"). 5 tables of 3 pieces, 30 of 4, 110 of 5, 365 of 6, 1001 of 7.
[[nodiscard]] std::vector<std::string> syzygyTables(int pieces);

// The folders of a tablebase path as the engines and fastchess read it: separated by ';' on Windows, ':' elsewhere.
// Empty parts are left out.
[[nodiscard]] std::vector<std::string> splitTablebasePath(const std::string& paths);

// What the folders of a tablebase path hold.
struct TablebaseCheck {
    std::vector<std::string> missing_dirs;  // not found, or not readable
    std::size_t wdl_files = 0;              // Syzygy WDL files (.rtbw) found
    int largest           = 0;              // the most pieces of the WDL files found, 0 when there are none
    // By piece count, from 3 to `largest`: the WDL tables that are not there, in syzygyTables() order.
    std::map<int, std::vector<std::string>> missing;
    // The Syzygy files (.rtbw and .rtbz, with the extension) whose size cannot be a Syzygy file's: cut short, by a
    // download that did not end for example. Engines and fastchess check it, and leave the file out; some engines
    // stop at once when they meet one.
    std::vector<std::string> damaged;
};

// Looks at every folder of `paths` (see splitTablebasePath). Reads the folders, not the files: their names and sizes.
[[nodiscard]] TablebaseCheck checkTablebases(const std::string& paths);

// checkTablebases() off the UI thread, for the setup summary, which asks every frame while a folder holds hundreds
// of files. A path asked for is checked again every few seconds, so a file added or a download that ends shows up.
class TablebaseChecks {
   public:
    TablebaseChecks()                                  = default;
    TablebaseChecks(const TablebaseChecks&)            = delete;
    TablebaseChecks& operator=(const TablebaseChecks&) = delete;
    ~TablebaseChecks();

    // The last check of `paths`, nullptr until the first one ends.
    const TablebaseCheck* get(const std::string& paths);

   private:
    struct Entry {
        std::optional<TablebaseCheck> check;
        std::chrono::steady_clock::time_point checked{}, asked{};
    };
    std::map<std::string, Entry> entries_;
    std::future<TablebaseCheck> job_;  // one check at a time
    std::string job_paths_;
};

}  // namespace fcg
