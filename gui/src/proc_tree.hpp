#pragma once

#include <vector>

namespace fcg {

#ifndef _WIN32

// Ends the process `pid`, a child of this program that has not been reaped yet, at once, with every process it started
// and that they started: the engine behind a wrapper script that does not exec, the engine behind the WinBoard
// adapter. Otherwise they would keep the engine's output open (a read of it would wait for them) and go on running.
// They are all frozen first (SIGSTOP), parents before their children, so that none of them can start another one or
// reap one found, then killed (SIGKILL), children first. Nothing is signalled when `pid` is not such a child (reaped
// already: its pid may be another process's now). False then. Windows needs none of this: an engine's job ends what
// it started.
bool killProcessTree(int pid);

// The processes `pid` started that have not ended (Linux and macOS; none elsewhere).
[[nodiscard]] std::vector<int> childProcesses(int pid);

#endif

}  // namespace fcg
