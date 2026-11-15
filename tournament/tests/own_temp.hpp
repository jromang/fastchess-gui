#pragma once

// The tests' files in a folder of the test program's own, under the system's temporary folder: what the tests take
// from std::filesystem::temp_directory_path(), and what the programs they start take, inheriting it. Two runs of the
// tests at once, or test cases run in processes of their own at the same time, then never share a folder of the same
// name. Removed when the tests end.

#include <filesystem>
#include <string>
#include <system_error>

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

}  // namespace fcg::test
