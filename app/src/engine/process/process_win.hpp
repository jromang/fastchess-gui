#pragma once

#ifdef _WIN64

#    include <engine/process/iprocess.hpp>

#    include <algorithm>
#    include <array>
#    include <cassert>
#    include <chrono>
#    include <string>
#    include <thread>
#    include <vector>

#    include <windows.h>

#    include <affinity/affinity.hpp>
#    include <core/globals/globals.hpp>
#    include <core/logger/logger.hpp>
#    include <core/threading/thread_vector.hpp>
#    include <engine/process/anon_pipe.hpp>

namespace fastchess {

extern util::ThreadVector<ProcessInformation> process_list;

namespace engine::process {

class Process : public IProcess {
    constexpr static int buffer_size = 4096;

    HANDLE hChildStdoutRead  = NULL;
    HANDLE hChildStdoutWrite = NULL;
    HANDLE hChildStdinRead   = NULL;
    HANDLE hChildStdinWrite  = NULL;
    // fastchess-gui: the error output's own pipe, when it is read (see capture_stderr); otherwise it goes to
    // stdout's pipe.
    HANDLE hChildStderrRead  = NULL;
    HANDLE hChildStderrWrite = NULL;
    std::array<char, buffer_size> buffer;
    std::array<char, buffer_size> err_buffer_;

   public:
    ~Process() override {
        LOG_TRACE_THREAD("Process destructor called for {} with pid {}", log_name_, pi_.dwProcessId);
        terminate();
    }

    Result init(const std::string& dir, const std::string& path, const std::string& args,
                const std::string& log_name) override {
        wd_       = dir;
        command_  = getPath(dir, path);
        args_     = args;
        log_name_ = log_name;

        current_line_.reserve(300);
        err_line_.clear();
        pi_ = PROCESS_INFORMATION{};

        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle       = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        if (!CreatePipeEx(&hChildStdoutRead, &hChildStdoutWrite, &saAttr)) {
            return Result::Error("failed to create stdout pipe");
        }

        if (!CreatePipeEx(&hChildStdinRead, &hChildStdinWrite, &saAttr)) {
            CloseHandle(hChildStdoutRead);
            CloseHandle(hChildStdoutWrite);
            return Result::Error("failed to create stdin pipe");
        }

        if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
            closesHandles();
            return Result::Error("failed to set stdout handle information");
        }

        if (!SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
            closesHandles();
            return Result::Error("failed to set stdin handle information");
        }

        err_open_ = Logger::should_log_ || capture_stderr;
        if (err_open_) {
            if (!CreatePipeEx(&hChildStderrRead, &hChildStderrWrite, &saAttr)) {
                closesHandles();
                return Result::Error("failed to create stderr pipe");
            }
            if (!SetHandleInformation(hChildStderrRead, HANDLE_FLAG_INHERIT, 0)) {
                closesHandles();
                return Result::Error("failed to set stderr handle information");
            }
        }

        try {
            STARTUPINFOA si = {};
            si.cb           = sizeof(STARTUPINFOA);
            si.hStdOutput   = hChildStdoutWrite;
            si.hStdInput    = hChildStdinRead;
            si.hStdError    = err_open_ ? hChildStderrWrite : hChildStdoutWrite;
            si.dwFlags |= STARTF_USESTDHANDLES;

            const bool created = createProcess(si);

            // Only the engine writes there: its end is then seen when the engine ends.
            if (hChildStderrWrite) {
                CloseHandle(hChildStderrWrite);
                hChildStderrWrite = NULL;
            }

            if (created) {
                process_list.push(ProcessInformation{pi_.hProcess, hChildStdoutWrite});
                return Result::OK();
            }
        } catch (const std::exception& e) {
            return Result::Error(e.what());
        }

        return Result::Error("process creation failed");
    }

    void closesHandles() {
        if (hChildStdoutRead) CloseHandle(hChildStdoutRead);
        if (hChildStdoutWrite) CloseHandle(hChildStdoutWrite);
        if (hChildStdinRead) CloseHandle(hChildStdinRead);
        if (hChildStdinWrite) CloseHandle(hChildStdinWrite);
        if (hChildStderrRead) CloseHandle(hChildStderrRead);
        if (hChildStderrWrite) CloseHandle(hChildStderrWrite);
        hChildStdoutRead = hChildStdoutWrite = hChildStdinRead = hChildStdinWrite = NULL;
        hChildStderrRead = hChildStderrWrite = NULL;
    }

    // Ends the process at once, if it still runs.
    void forceKill() noexcept {
        if (is_initialized_ && alive()) TerminateProcess(pi_.hProcess, 1);
    }

    // The operating system's id of the process, 0 before it started.
    [[nodiscard]] int64_t pid() const noexcept { return is_initialized_ ? pi_.dwProcessId : 0; }

    [[nodiscard]] Result alive() noexcept override {
        assert(is_initialized_);

        DWORD exitCode = 0;

        return GetExitCodeProcess(pi_.hProcess, &exitCode) && exitCode == STILL_ACTIVE
                   ? Result::OK()
                   : Result::Error("process terminated");
    }

    bool setAffinity(const std::vector<int>& cpus) noexcept override {
        assert(is_initialized_);
        return affinity::setProcessAffinity(cpus, pi_.hProcess);
    }

    void terminate() {
        closesHandles();

        if (!is_initialized_) {
            return;
        }

        process_list.remove_if([this](const auto& pi) { return pi.identifier == pi_.hProcess; });

        DWORD exitCode        = 0;
        const auto start_time = std::chrono::steady_clock::now();

        // give the process time to die gracefully
        while (std::chrono::steady_clock::now() - start_time < IProcess::kill_timeout) {
            GetExitCodeProcess(pi_.hProcess, &exitCode);

            if (exitCode != STILL_ACTIVE) {
                // Process has terminated
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (exitCode == STILL_ACTIVE) {
            LOG_TRACE_THREAD("Force terminating process with pid: {}", pi_.hProcess);
            TerminateProcess(pi_.hProcess, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            GetExitCodeProcess(pi_.hProcess, &exitCode);
        } else {
            LOG_TRACE_THREAD("Process with pid: {} terminated with status: {}", pi_.hProcess, exitCode);
        }

        // fastchess-gui: closing the job ends what the engine started and left running (the engine of a .bat or a
        // wrapper).
        if (job_) {
            CloseHandle(job_);
            job_ = NULL;
        }

        is_initialized_ = false;
    }

    // fastchess-gui: the error output's line being read is kept: it is not an answer to what is read next, and a
    // read often ends (on the line awaited) before the error output's line is complete.
    void setupRead() override { current_line_.clear(); }

    // Read stdout until the line matches last_word or timeout is reached
    // 0 means no timeout
    //
    // fastchess-gui: the error output, when it has a pipe of its own (see capture_stderr), is read in the same
    // waits: an engine blocked on a full error pipe would otherwise never write the line waited for.
    Result readOutput(std::vector<Line>& lines, std::string_view last_word,
                      std::chrono::milliseconds threshold) override {
        assert(is_initialized_);

        lines.clear();

        const auto startTime = std::chrono::steady_clock::now();
        const bool infinite  = threshold.count() <= 0;

        PipeRead out(hChildStdoutRead, buffer);
        PipeRead err(hChildStderrRead, err_buffer_);

        if (!out.ready() || (err_open_ && !err.ready())) {
            LOG_FATAL_THREAD("Failed to create event for overlapped I/O");
            return Result::Error("failed to create event for overlapped I/O");
        }

        // Every return goes through here: the reads still pending are cancelled, and what they read meanwhile kept.
        // What waits on the error output is read too: it was written before the line awaited (which may be the last
        // of a game), or before the engine ended (its last words: an assertion, a crash report).
        const auto finish = [&](Result result) {
            DWORD bytes = 0;
            if (out.cancel(bytes) && bytes > 0) (void)readBytes(buffer, bytes, lines, "", current_line_, false);
            if (err.cancel(bytes) && bytes > 0) (void)readBytes(err_buffer_, bytes, lines, "", err_line_, true);
            if (result.code != Status::TIMEOUT) drainErr(err, lines, result.code != Status::OK && !alive());
            return result;
        };

        const auto terminatedOr = [&](const char* message) {
            if (!alive()) return finish(Result::Error("process terminated"));
            LOG_FATAL_THREAD("{}", message);
            return finish(Result::Error(message));
        };

        while (true) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);

            if (!infinite && elapsed >= threshold) {
                return finish(Result{Status::TIMEOUT, "timeout"});
            }

            if (atomic::stop) {
                return finish(Result::Error("stop requested"));
            }

            DWORD bytesRead = 0;

            if (!out.pending) {
                const auto started = out.start(bytesRead);
                if (started == PipeRead::Started::Failed) return terminatedOr("Failed to read from pipe");
                if (started == PipeRead::Started::Done) {
                    if (bytesRead == 0) {
                        if (!alive()) return finish(Result::Error("process terminated"));
                        continue;
                    }
                    if (readBytes(buffer, bytesRead, lines, last_word, current_line_, false)) {
                        return finish(Result::OK());
                    }
                    continue;
                }
            }

            if (err_open_ && !err.pending) {
                const auto started = err.start(bytesRead);
                if (started == PipeRead::Started::Failed) {
                    // Closed by the engine, or the engine ended: its output tells which.
                    endErr(lines);
                } else if (started == PipeRead::Started::Done) {
                    (void)readBytes(err_buffer_, bytesRead, lines, "", err_line_, true);
                    continue;
                }
            }

            // The pipes first: what an engine wrote before it ended is read before its end is seen.
            HANDLE wait_handles[3];
            DWORD count           = 0;
            const DWORD out_index = count;
            wait_handles[count++] = out.event();
            const DWORD err_index = err.pending ? count : MAXDWORD;
            if (err.pending) wait_handles[count++] = err.event();
            const DWORD proc_index = count;
            wait_handles[count++]  = pi_.hProcess;

            const auto remaining =
                infinite ? INFINITE
                         : static_cast<DWORD>(std::clamp<int64_t>((threshold - elapsed).count(), 0, INFINITE - 1));
            const DWORD waitResult = WaitForMultipleObjects(count, wait_handles, FALSE, remaining);

            if (waitResult == WAIT_TIMEOUT) {
                return finish(Result{Status::TIMEOUT, "timeout"});
            }

            if (waitResult == WAIT_OBJECT_0 + out_index) {
                if (!out.complete(bytesRead)) return terminatedOr("Failed to get overlapped result");
                if (bytesRead == 0) {
                    if (!alive()) return finish(Result::Error("process terminated"));
                    continue;
                }
                if (readBytes(buffer, bytesRead, lines, last_word, current_line_, false)) {
                    return finish(Result::OK());
                }
                continue;
            }

            if (err_index != MAXDWORD && waitResult == WAIT_OBJECT_0 + err_index) {
                if (err.complete(bytesRead)) {
                    (void)readBytes(err_buffer_, bytesRead, lines, "", err_line_, true);
                } else {
                    endErr(lines);
                }
                continue;
            }

            if (waitResult == WAIT_OBJECT_0 + proc_index) {
                if (!alive()) {
                    return finish(Result::Error("process terminated"));
                }

                LOG_FATAL_THREAD("Process handle signaled but process is still alive");
                return finish(Result::Error("wait failed"));
            }

            LOG_FATAL_THREAD("Wait failed");
            return finish(Result::Error("wait failed"));
        }
    }

    Result writeInput(const std::string& input) noexcept override {
        assert(is_initialized_);

        if (!alive()) {
            return Result::Error("process not alive");
        }

        DWORD bytes_written;
        auto res = WriteFile(hChildStdinWrite, input.c_str(), input.length(), &bytes_written, nullptr);

        return res ? Result::OK() : Result::Error("failed to write to stdin");
    }

   private:
    // fastchess-gui: an overlapped read of a pipe, which may stay pending through several waits of readOutput().
    // The read must be over before its buffer and its OVERLAPPED go away: cancel() waits for it.
    class PipeRead {
       public:
        enum class Started { Done, Pending, Failed };

        PipeRead(HANDLE pipe, std::array<char, buffer_size>& data) : pipe_(pipe), data_(data) {
            if (pipe_) overlapped_.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        }

        ~PipeRead() {
            DWORD bytes = 0;
            (void)cancel(bytes);
            if (overlapped_.hEvent) CloseHandle(overlapped_.hEvent);
        }

        PipeRead(const PipeRead&)            = delete;
        PipeRead& operator=(const PipeRead&) = delete;

        [[nodiscard]] bool ready() const noexcept { return overlapped_.hEvent != NULL; }
        [[nodiscard]] HANDLE event() const noexcept { return overlapped_.hEvent; }

        // Done: `bytes` were read at once; Pending: event() is set once complete() can tell; Failed: the pipe is
        // closed or broken.
        Started start(DWORD& bytes) {
            const HANDLE event = overlapped_.hEvent;
            overlapped_        = OVERLAPPED{};
            overlapped_.hEvent = event;
            ResetEvent(event);

            bytes = 0;
            if (ReadFile(pipe_, data_.data(), static_cast<DWORD>(data_.size()), &bytes, &overlapped_)) {
                return Started::Done;
            }
            if (GetLastError() != ERROR_IO_PENDING) return Started::Failed;
            pending = true;
            return Started::Pending;
        }

        // The read is over (event() is set): false when it failed.
        bool complete(DWORD& bytes) {
            pending = false;
            bytes   = 0;
            return GetOverlappedResult(pipe_, &overlapped_, &bytes, FALSE);
        }

        // Ends a pending read and waits until it is over: true when it read `bytes` meanwhile.
        bool cancel(DWORD& bytes) {
            bytes = 0;
            if (!pending) return false;
            pending = false;
            CancelIoEx(pipe_, &overlapped_);
            return GetOverlappedResult(pipe_, &overlapped_, &bytes, TRUE);
        }

        bool pending = false;

       private:
        HANDLE pipe_;
        std::array<char, buffer_size>& data_;
        OVERLAPPED overlapped_{};
    };

    // fastchess-gui: how long the error output is read at most once the engine ended, should a process it started
    // write on there.
    static constexpr std::chrono::milliseconds ERR_END_READ{500};

    // fastchess-gui: reads what waits on the error output, without waiting for more; `err` is not pending. Once the
    // engine `ended`, until nothing is left (for ERR_END_READ at most); otherwise at most what a pipe holds, so that an
    // engine writing there on and on cannot hold the read up.
    void drainErr(PipeRead& err, std::vector<Line>& lines, bool ended) {
        const auto deadline = std::chrono::steady_clock::now() + ERR_END_READ;
        for (int reads = 0; err_open_; ++reads) {
            if (ended ? std::chrono::steady_clock::now() >= deadline : reads >= 16) return;

            DWORD available = 0;
            if (!PeekNamedPipe(hChildStderrRead, NULL, 0, NULL, &available, NULL)) return endErr(lines);
            if (available == 0) return;

            DWORD bytes        = 0;
            const auto started = err.start(bytes);
            if (started == PipeRead::Started::Failed) return endErr(lines);
            if (started == PipeRead::Started::Pending) {
                // The bytes are there, the read ends at once.
                if (WaitForSingleObject(err.event(), 100) != WAIT_OBJECT_0) {
                    if (err.cancel(bytes) && bytes > 0) (void)readBytes(err_buffer_, bytes, lines, "", err_line_, true);
                    return;
                }
                if (!err.complete(bytes)) return endErr(lines);
            }
            (void)readBytes(err_buffer_, bytes, lines, "", err_line_, true);
        }
    }

    // fastchess-gui: the error output ended; a last line without its end of line is what the engine wrote last.
    void endErr(std::vector<Line>& lines) {
        err_open_ = false;
        if (!err_line_.empty()) {
            addLine(lines, err_line_, true);
            err_line_.clear();
        }
    }

    // `line`: the line being read from this pipe, `err`: whether it is the error output.
    [[nodiscard]] bool readBytes(const std::array<char, buffer_size>& buffer, DWORD bytes_read,
                                 std::vector<Line>& lines, std::string_view searchword, std::string& line, bool err) {
        assert(bytes_read <= buffer.size());

        std::string_view data(buffer.data(), static_cast<size_t>(bytes_read));
        size_t start = 0;

        while (start < data.size()) {
            const size_t pos = data.find_first_of("\r\n", start);

            if (pos == std::string_view::npos) {
                line.append(data.substr(start));
                break;
            }

            assert(pos >= start);
            assert(pos < data.size());

            line.append(data.substr(start, pos - start));

            if (!line.empty()) {
                addLine(lines, line, err);

                if (!searchword.empty() && line.rfind(searchword, 0) == 0) {
                    line.clear();
                    return true;
                }

                line.clear();
            }

            // Treat "\r\n" as one delimiter.
            if (data[pos] == '\r' && pos + 1 < data.size() && data[pos + 1] == '\n') {
                start = pos + 2;
            } else {
                start = pos + 1;
            }

            assert(start <= data.size());
        }

        return false;
    }

    [[nodiscard]] bool createProcess(STARTUPINFOA& si) {
        // Unquoted, Windows splits the path at its spaces and starts the first program it finds: "C:\e\gaia 4.4.exe"
        // runs C:\e\gaia.exe with the argument "4.4.exe" when that file exists. A path cannot contain '"', so one
        // that starts with it was quoted by the user.
        const bool quoted = !command_.empty() && command_.front() == '"';
        const auto cmd    = fmt::format("{} {}", quoted ? command_ : fmt::format("\"{}\"", command_), args_);

        // A GUI parent has no console; without CREATE_NO_WINDOW every console engine would open its own window.
        // Suspended until it is in its job, so that nothing it starts escapes the job.
        const DWORD flags =
            CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED | (GetConsoleWindow() == nullptr ? CREATE_NO_WINDOW : 0);

        const auto success = CreateProcessA(      //
            nullptr,                              //
            const_cast<char*>(cmd.c_str()),       //
            nullptr,                              //
            nullptr,                              //
            TRUE,                                 //
            flags,                                //
            nullptr,                              //
            wd_.empty() ? nullptr : wd_.c_str(),  //
            &si,                                  //
            &pi_                                  //
        );

        is_initialized_ = true;

        if (success) {
            assignJob();
            ResumeThread(pi_.hThread);
            CloseHandle(pi_.hThread);
            pi_.hThread = NULL;
        }

        return success;
    }

    // fastchess-gui: the engine and every process it starts go in a job of their own, ended when the job's handle is
    // closed: by terminate(), or by Windows when the GUI ends, even by a crash. TerminateProcess alone would leave the
    // engine of a .bat or a wrapper running.
    void assignJob() {
        job_ = CreateJobObjectA(nullptr, nullptr);
        if (!job_) return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job_, pi_.hProcess)) {
            CloseHandle(job_);
            job_ = NULL;
        }
    }

    void addLine(std::vector<Line>& lines, const std::string& line, bool err) const {
        const auto timestamp = Logger::should_log_ ? time::datetime_precise() : "";

        lines.emplace_back(Line{line, timestamp, err ? Standard::ERR : Standard::OUTPUT});

        if (realtime_logging_) {
            Logger::readFromEngine(line, timestamp, log_name_, err, thread_id);
        }

        const auto& callback = err ? err_line_callback_ : line_callback_;
        if (callback) callback(line);
    }

    // The working directory
    std::string wd_;
    // The command to execute
    std::string command_;
    // The arguments for the engine
    std::string args_;
    // The name in the log file
    std::string log_name_;
    // The current line read from the engine, avoids reallocations
    std::string current_line_;
    // fastchess-gui: the same for the error output, and whether it is read and has not ended
    std::string err_line_;
    bool err_open_ = false;

    // True if the process has been initialized
    bool is_initialized_ = false;

    PROCESS_INFORMATION pi_;
    HANDLE job_ = NULL;

    const std::thread::id thread_id = std::this_thread::get_id();
};

}  // namespace engine::process
}  // namespace fastchess

#endif
