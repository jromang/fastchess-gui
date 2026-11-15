#pragma once

// The tests' files in a folder of the test program's own, under the system's temporary folder: what the tests take
// from std::filesystem::temp_directory_path(), and what the programs they start take, inheriting it. Two runs of the
// tests at once, or test cases run in processes of their own at the same time, then never share a folder of the same
// name. Removed when the tests end.

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#    include <process.h>
#    include <stdlib.h>
#else
#    include <stdlib.h>
#    include <unistd.h>
#endif

namespace fcg::test {

class OwnTempDir {
   public:
    OwnTempDir() {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto system = fs::temp_directory_path(ec);
        if (ec) return;
#ifdef _WIN32
        dir_ = system / ("fcg-tests-" + std::to_string(_getpid()));
#else
        dir_ = system / ("fcg-tests-" + std::to_string(getpid()));
#endif
        fs::remove_all(dir_, ec);  // left by an earlier process of the same id, ended before it could remove it
        if (!fs::create_directories(dir_, ec)) {
            dir_.clear();
            return;
        }
#ifdef _WIN32
        // GetTempPath() reads TMP, then TEMP.
        _wputenv_s(L"TMP", dir_.c_str());
        _wputenv_s(L"TEMP", dir_.c_str());
#else
        setenv("TMPDIR", dir_.c_str(), 1);
#endif
    }
    ~OwnTempDir() {
        std::error_code ec;
        if (!dir_.empty()) std::filesystem::remove_all(dir_, ec);
    }
    OwnTempDir(const OwnTempDir&)            = delete;
    OwnTempDir& operator=(const OwnTempDir&) = delete;

   private:
    std::filesystem::path dir_;
};

// std::filesystem::remove_all, tried again for five seconds while it fails: on Windows a folder cannot go while a
// process still holds it, an engine's that is ending for one. Then it throws as remove_all does.
inline void removeAll(const std::filesystem::path& path) {
    for (int i = 0; i < 100; ++i) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (!ec) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::filesystem::remove_all(path);
}

}  // namespace fcg::test
