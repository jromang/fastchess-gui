#include "tablebases.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace fcg {

namespace fs = std::filesystem;

namespace {

// The pieces besides the king, strongest first: the order of the letters in a table's name.
constexpr char PIECES[] = {'Q', 'R', 'B', 'N', 'P'};

// Every multiset of `count` pieces, each in PIECES order ("QQ", "QR"... "PP"), as indexes into PIECES.
void sides(int count, int from, std::vector<int>& side, std::vector<std::vector<int>>& out) {
    if (static_cast<int>(side.size()) == count) {
        out.push_back(side);
        return;
    }
    for (int i = from; i < static_cast<int>(std::size(PIECES)); ++i) {
        side.push_back(i);
        sides(count, i, side, out);
        side.pop_back();
    }
}

std::vector<std::vector<int>> sides(int count) {
    std::vector<std::vector<int>> out;
    std::vector<int> side;
    sides(count, 0, side, out);
    return out;
}

std::string sideName(const std::vector<int>& side) {
    std::string name = "K";
    for (const int i : side) name += PIECES[i];
    return name;
}

// The table name the file of a Syzygy table has, without its extension, for any piece count it is made for.
const std::set<std::string>& allTables() {
    static const std::set<std::string> all = [] {
        std::set<std::string> names;
        for (int pieces = 3; pieces <= 7; ++pieces) {
            for (auto& name : syzygyTables(pieces)) names.insert(std::move(name));
        }
        return names;
    }();
    return all;
}

int piecesOf(const std::string& table) { return static_cast<int>(table.size()) - 1; }  // the letters but the 'v'

}  // namespace

std::vector<std::string> syzygyTables(int pieces) {
    std::vector<std::string> out;
    if (pieces < 3 || pieces > 7) return out;
    // The kings aside, `strong` pieces on the side named first and `weak` on the other, never fewer. With as many
    // on both sides, the stronger side comes first: the earlier in PIECES at the first piece that differs.
    for (int weak = 0; 2 * weak <= pieces - 2; ++weak) {
        const int strong  = pieces - 2 - weak;
        const auto firsts = sides(strong), seconds = sides(weak);
        for (const auto& first : firsts) {
            for (const auto& second : seconds) {
                if (strong == weak && second < first) continue;  // the same table, the other way round
                out.push_back(sideName(first) + "v" + sideName(second));
            }
        }
    }
    return out;
}

std::vector<std::string> splitTablebasePath(const std::string& paths) {
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (begin <= paths.size()) {
        auto end = paths.find(separator, begin);
        if (end == std::string::npos) end = paths.size();
        if (end > begin) out.push_back(paths.substr(begin, end - begin));
        begin = end + 1;
    }
    return out;
}

TablebaseCheck checkTablebases(const std::string& paths) {
    TablebaseCheck check;
    const auto& all = allTables();
    std::set<std::string> wdl;
    for (const auto& dir : splitTablebasePath(paths)) {
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        if (ec) {
            check.missing_dirs.push_back(dir);
            continue;
        }
        for (const fs::directory_iterator last; !ec && it != last; it.increment(ec)) {
            const auto& path = it->path();
            const auto ext   = path.extension().string();
            if (ext != ".rtbw" && ext != ".rtbz") continue;
            const auto table = path.stem().string();
            if (!all.count(table)) continue;
            std::error_code size_ec;
            const auto size = it->file_size(size_ec);
            if (size_ec) continue;
            // Syzygy files are padded to 16 bytes more than a multiple of 64: the probing code of the engines (and
            // fastchess') leaves out a file of another size.
            if (size % 64 != 16) {
                check.damaged.push_back(path.filename().string());
                continue;
            }
            if (ext == ".rtbw" && wdl.insert(table).second) check.largest = std::max(check.largest, piecesOf(table));
        }
    }
    check.wdl_files = wdl.size();
    for (int pieces = 3; pieces <= check.largest; ++pieces) {
        auto& missing = check.missing[pieces];
        for (auto& table : syzygyTables(pieces)) {
            if (!wdl.count(table)) missing.push_back(std::move(table));
        }
        if (missing.empty()) check.missing.erase(pieces);
    }
    std::sort(check.damaged.begin(), check.damaged.end());
    check.damaged.erase(std::unique(check.damaged.begin(), check.damaged.end()), check.damaged.end());
    return check;
}

TablebaseChecks::~TablebaseChecks() {
    if (job_.valid()) job_.wait();
}

const TablebaseCheck* TablebaseChecks::get(const std::string& paths) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (job_.valid() && job_.wait_for(seconds(0)) == std::future_status::ready) {
        auto& done   = entries_[job_paths_];
        done.check   = job_.get();
        done.checked = now;
    }
    // The paths not asked for a while (the setup changed) are forgotten.
    std::erase_if(entries_, [&](const auto& e) { return e.first != paths && now - e.second.asked > minutes(1); });

    auto& entry = entries_[paths];
    entry.asked = now;
    if (!job_.valid() && (!entry.check || now - entry.checked > seconds(5))) {
        job_paths_ = paths;
        job_       = std::async(std::launch::async, [paths] { return checkTablebases(paths); });
    }
    return entry.check ? &*entry.check : nullptr;
}

}  // namespace fcg
