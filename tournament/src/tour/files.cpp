#include "files.hpp"

#include <cctype>
#include <cerrno>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX  // the toolchain may define it already
#        define NOMINMAX
#    endif
#    include <io.h>
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/file.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

constexpr const char* LOCK_FILE = "tournament.lock";

std::string lastError() {
#ifdef _WIN32
    return std::system_category().message(static_cast<int>(GetLastError()));
#else
    return std::generic_category().message(errno);
#endif
}

#ifndef _WIN32
// The rename of a file is on the disk once its folder is.
void syncFolder(const fs::path& dir) {
    const int fd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    ::fsync(fd);
    ::close(fd);
}
#endif

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace

std::FILE* openFile(const fs::path& file, const char* mode) {
#ifdef _WIN32
    return _wfopen(file.c_str(), fs::path(mode).c_str());
#else
    return std::fopen(file.c_str(), mode);
#endif
}

bool syncFile(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(fileno(file)) == 0;
#endif
}

void replaceFile(const fs::path& file, std::string_view text, bool backup) {
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (backup && fs::is_regular_file(file, ec)) replaceFile(fs::path(file).concat(".bak"), readAll(file));

    const auto tmp  = fs::path(file).concat(".tmp");
    const auto fail = [&](const fs::path& which) {
        const auto reason = lastError();
        fs::remove(tmp, ec);
        throw std::runtime_error("cannot write " + which.string() + ": " + reason);
    };

#ifdef _WIN32
    std::FILE* out = openFile(tmp, "wb");
    if (!out) fail(tmp);
    const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size() && syncFile(out);
    if (std::fclose(out) != 0 || !written) fail(tmp);
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail(file);
#else
    // Made with the permissions of the file it replaces: settings.json holds a token only its owner may read.
    struct stat old{};
    const bool had_file = ::stat(file.c_str(), &old) == 0;
    const mode_t mode   = had_file ? (old.st_mode & 07777) : 0666;
    ::unlink(tmp.c_str());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) fail(tmp);
    bool written = !had_file || ::fchmod(fd, mode) == 0;
    for (std::size_t done = 0; written && done < text.size();) {
        const auto n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        written = n > 0;
        if (written) done += static_cast<std::size_t>(n);
    }
    written = written && ::fsync(fd) == 0;
    if (::close(fd) != 0 || !written) fail(tmp);
    if (::rename(tmp.c_str(), file.c_str()) != 0) fail(file);
    syncFolder(file.parent_path());
#endif
}

class FolderLock::Held {
   public:
    explicit Held(const fs::path& dir);
    ~Held();

    Held(const Held&)            = delete;
    Held& operator=(const Held&) = delete;

   private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

FolderLock::FolderLock(const fs::path& dir) {
    // The locks this program holds, by folder; a child process made by fork() does not hold its parent's.
    struct Entry {
        long pid = 0;
        std::weak_ptr<Held> held;
    };
    static std::mutex mutex;
    static std::map<fs::path, Entry> held;

    std::error_code ec;
    auto key = fs::weakly_canonical(dir, ec);
    if (ec) key = fs::absolute(dir, ec);

    std::lock_guard<std::mutex> lock(mutex);
    auto& entry = held[key];
    if (entry.pid == processId()) held_ = entry.held.lock();
    if (!held_) {
        held_ = std::make_shared<Held>(dir);
        entry = {processId(), held_};
    }
}

FolderLock::~FolderLock() = default;

FolderLock::Held::Held(const fs::path& dir) {
    const auto file = dir / LOCK_FILE;
    const auto busy = [&](const std::string& pid) {
        return FolderBusy("this tournament is open in another program" + (pid.empty() ? "" : " (process " + pid + ")") +
                          "; close it there first: " + dir.string());
    };

#ifdef _WIN32
    // Shared for reading only: a second program asking to write is refused until this one closes it.
    handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        if (GetLastError() != ERROR_SHARING_VIOLATION) {
            throw std::runtime_error("cannot write " + file.string() + ": " + lastError());
        }
        std::string pid;
        if (HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            h != INVALID_HANDLE_VALUE) {
            char buffer[32];
            DWORD n = 0;
            if (ReadFile(h, buffer, sizeof(buffer), &n, nullptr)) pid.assign(buffer, n);
            CloseHandle(h);
        }
        throw busy(pid);
    }
    const auto pid = std::to_string(processId());
    DWORD n        = 0;
    SetEndOfFile(handle_);
    WriteFile(handle_, pid.data(), static_cast<DWORD>(pid.size()), &n, nullptr);
#else
    fd_ = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd_ < 0) throw std::runtime_error("cannot write " + file.string() + ": " + lastError());
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        const bool held = errno == EWOULDBLOCK;
        const auto why  = lastError();
        char buffer[32];
        const auto n = ::read(fd_, buffer, sizeof(buffer));
        ::close(fd_);
        fd_ = -1;
        if (!held) throw std::runtime_error("cannot lock " + file.string() + ": " + why);
        throw busy(n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string());
    }
    // The pid is only for the message of a program refused: no matter if it cannot be written.
    const auto pid                     = std::to_string(processId());
    [[maybe_unused]] const bool stated = ::ftruncate(fd_, 0) == 0 && ::write(fd_, pid.data(), pid.size()) > 0;
#endif
}

FolderLock::Held::~Held() {
#ifdef _WIN32
    if (handle_) CloseHandle(handle_);
#else
    if (fd_ >= 0) ::close(fd_);
#endif
}

std::string fileNameOf(std::string name) {
    for (auto& c : name) {
        if (static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            c = '_';
    }
    for (auto i = name.size(); i > 0 && (name[i - 1] == '.' || name[i - 1] == ' '); --i) name[i - 1] = '_';

    // CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9, in any case and with any extension, are devices.
    std::string stem = name.substr(0, name.find('.'));
    for (auto& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const bool device = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
                        (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
                         stem[3] >= '1' && stem[3] <= '9');
    if (device) name.insert(0, "_");
    return name;
}

}  // namespace fcg::tour
