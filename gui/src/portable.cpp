#include "portable.hpp"

#include <system_error>

namespace fcg::portable {

namespace fs = std::filesystem;

namespace {

fs::path g_base;

#ifdef _WIN32
constexpr char LIST_SEPARATOR = ';';
#else
constexpr char LIST_SEPARATOR = ':';
#endif

template <typename Each>
std::string eachOfList(const std::string& paths, Each each) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        const auto end = paths.find(LIST_SEPARATOR, at);
        out += each(paths.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        out += LIST_SEPARATOR;
        at = end + 1;
    }
    return out;
}

}  // namespace

fs::path programFolder(const fs::path& executable) {
    auto folder = executable.parent_path();
    // fastchess-gui.app/Contents/MacOS/fastchess-gui: the folder of the bundle.
    if (folder.filename() == "MacOS" && folder.parent_path().filename() == "Contents" &&
        folder.parent_path().parent_path().extension() == ".app") {
        return folder.parent_path().parent_path().parent_path();
    }
    return folder;
}

fs::path detect(const fs::path& executable) {
    if (executable.empty()) return {};
    const auto folder = programFolder(executable);
    std::error_code ec;
    if (folder.empty() || !fs::exists(folder / MARKER, ec)) return {};
    return fs::absolute(folder, ec).lexically_normal();
}

void setBase(const fs::path& folder) { g_base = folder.empty() ? fs::path{} : folder.lexically_normal(); }

const fs::path& base() { return g_base; }

std::string stored(const std::string& path, const fs::path& base) {
    if (base.empty() || path.empty()) return path;
    const auto p = fs::path(path).lexically_normal();
    if (!p.is_absolute()) return path;
    const auto relative = p.lexically_relative(base.lexically_normal());
    if (relative.empty() || *relative.begin() == "..") return path;  // elsewhere, or another drive
    return relative.generic_string();
}

std::string resolved(const std::string& path, const fs::path& base) {
    if (base.empty() || path.empty()) return path;
    const fs::path p(path);
    // "C:engine.exe" has a drive but is not absolute: not one of ours either.
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory()) return path;
    if (p == ".") return base.string();
    return (base / p).lexically_normal().string();
}

std::string storedList(const std::string& paths, const fs::path& base) {
    if (base.empty() || paths.empty()) return paths;
    return eachOfList(paths, [&](const std::string& p) { return stored(p, base); });
}

std::string resolvedList(const std::string& paths, const fs::path& base) {
    if (base.empty() || paths.empty()) return paths;
    return eachOfList(paths, [&](const std::string& p) { return resolved(p, base); });
}

std::string stored(const std::string& path) { return stored(path, g_base); }
std::string resolved(const std::string& path) { return resolved(path, g_base); }

// An engine on a machine: its program and folder are paths there, left as they are; this computer's are kept aside.
EngineEntry stored(EngineEntry engine) {
    auto& cmd = engine.machine.empty() ? engine.cmd : engine.local_cmd;
    auto& dir = engine.machine.empty() ? engine.dir : engine.local_dir;
    cmd       = stored(cmd);
    dir       = stored(dir);
    return engine;
}

void resolve(EngineEntry& engine) {
    auto& cmd = engine.machine.empty() ? engine.cmd : engine.local_cmd;
    auto& dir = engine.machine.empty() ? engine.dir : engine.local_dir;
    cmd       = resolved(cmd);
    dir       = resolved(dir);
}

BookEntry stored(BookEntry book) {
    book.path = stored(book.path);
    return book;
}

void resolve(BookEntry& book) { book.path = resolved(book.path); }

MatchSetup stored(MatchSetup setup) {
    setup.book        = stored(setup.book);
    setup.book_a.path = stored(setup.book_a.path);
    setup.book_b.path = stored(setup.book_b.path);
    for (auto& p : setup.participants) p.book.path = stored(p.book.path);
    setup.tb_paths      = storedList(setup.tb_paths, g_base);
    setup.pgn_out       = stored(setup.pgn_out);
    setup.broadcast_dir = stored(setup.broadcast_dir);
    return setup;
}

void resolve(MatchSetup& setup) {
    setup.book        = resolved(setup.book);
    setup.book_a.path = resolved(setup.book_a.path);
    setup.book_b.path = resolved(setup.book_b.path);
    for (auto& p : setup.participants) p.book.path = resolved(p.book.path);
    setup.tb_paths      = resolvedList(setup.tb_paths, g_base);
    setup.pgn_out       = resolved(setup.pgn_out);
    setup.broadcast_dir = resolved(setup.broadcast_dir);
}

}  // namespace fcg::portable
