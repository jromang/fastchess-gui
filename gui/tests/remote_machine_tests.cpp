// The remote machines: quoting for their shells, the reading of what their commands print, the machines' file, the
// comparison of books and tablebases, the user's keys (RSA and Ed25519, read and signing), and the SSH client against
// the in-process server (ssh_server.hpp): commands, programs talked to, SFTP with a copy resumed after a cut, and the
// jobs of the Settings' machines.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <libssh2.h>  // before windows.h: it includes winsock2.h

#include "ed25519.h"
#include "free_ports.hpp"
#include "libssh2_backend.hpp"
#include "machine_jobs.hpp"
#include "machine_run.hpp"
#include "machines.hpp"
#include "program_format.hpp"
#include "remote_run.hpp"
#include "run.hpp"
#include "ssh_client.hpp"
#include "ssh_server.hpp"
#include "ssh_tunnel.hpp"
#include "ssh_wire.hpp"

using namespace fcg;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// The mock SSH server's port, free when the process first asks (each test case may run in a process of its own).
uint16_t machinePort() {
    static const uint16_t port = fcg::test::freePort();
    return port;
}

// Throwaway keys made for these tests by ssh-keygen: an Ed25519 key and an RSA key in OpenSSH's format, encrypted
// with the passphrase "chess" (bcrypt and aes256-ctr), an RSA key in PKCS #1's PEM, and an ECDSA key, which the
// program does not take.
constexpr const char* ED25519_KEY = R"(-----BEGIN OPENSSH PRIVATE KEY-----
b3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHIAAAAGYmNyeXB0AAAAGAAAABDsgupVSq
xKKNL05lAbIX1FAAAAIAAAAAEAAAAzAAAAC3NzaC1lZDI1NTE5AAAAILTbv98l/g6Zdigq
B2icqabFYz90jMKYY+cpvvQzguy9AAAAoDmDe3L+tM3eB/7h2IYKpJ1wpuhckhMuVjptOY
SLfov0LheL26xqPLGaxDtujtyKMJAW3eSSqnqGa5M3M978PqSzoRtt/CjH0xCC9kCxowc4
hC2HRRAL+444T0sAIVgfkXzdNkTLEjsNZosRrecnMZ1wMu9SkVBfyGXGCc4atQ3PxOYAFy
mYrn74GlSuuyaAUZClcmj8IDKKB8fEyBHSAE8=
-----END OPENSSH PRIVATE KEY-----
)";
constexpr const char* ED25519_PUB =
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILTbv98l/g6ZdigqB2icqabFYz90jMKYY+cpvvQzguy9 test-ed25519-pass";

constexpr const char* RSA_KEY = R"(-----BEGIN OPENSSH PRIVATE KEY-----
b3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHIAAAAGYmNyeXB0AAAAGAAAABAefm7RV1
tXk0vIk4MPt+TgAAAAIAAAAAEAAAEXAAAAB3NzaC1yc2EAAAADAQABAAABAQDQu3IMoAOH
ARpJ+IdzZqkqar/PysEUgJUIT2VTuOV/XljVI8H1+9vQV2lx58q6FdAD2Qcc4WUyxj5l9l
wpki5m64zyyewTzkWawapqWCnDDaHUGFateY7RRafXTL1FbccuX/tJh4L7802ONTfgO4Pm
qAMrSntgIj7cuAKPvjRR4H3V3xkmr+r0VR8wRsayNPsSEqDUaIxoGEL6Sbp3j2kfWsoIDS
tqKmKgMRrRdAF+5hMKSrcU1IaYU6EvKQWxyPeKQtw072p19mGyDnjHYybrQRc8yWTQT6c6
/ebIanFXNkE9jJWdoiZmdlPQ7IyPjL+vkrYz6cbs7dxr1+VITlefAAAD0CQRSasWK0W3vx
R48MweBxczd2HY8yBPHpELJyxA12rl4zBI0qmDJyB5hEjZY0Wvq+LEfOYCV/4/sbPlt7w8
fy0a6QKaIgrMHGDGsqz8Y355sYGhrvGYCMIdVvv2EdJn9IH1N+60t4LKh0XHjAItQBwV/K
c7e/tyFwxrLPIHmAQ4VV6KfP7kT41LOrWLtwxkfsRCXxplSicN8L1OnPf2ubcmJVqXHWoK
eViKXdv3hB76fPEONdG5Ui8JaREgKkMDkKOTPQybNNrC4DlXnt63lFbXUd3y9cQQ1srgj8
ojrrqG8cKwlkLX5yE4RfI1ja2nTxCNmjxJ8I6K+oEU+whYE3pSKs8xbwH+IeMHl9iMuUxz
IPk0lOHizWf0gyo27KJbxo28RfGBmQL99l4BPdPz8yDMidtJrLfFmh+iRq0clpLsKI/fKv
Qws1VKdXdAyvhRLJbeQzDWdvhVgFtjbErDLTXL03Taszs/qqhdpTy9l5drejw3DlExmx3X
V3GoCYyX9E5MPuXx7vV+sEqlj6/D3C9ZESYwGRHRJypL1sCqaCL976blycKjr8ubSY0hGp
aqx1mPuJ3ss0GHg+iJN8EXcWO+s+GkK5lWKccTcL9z7EZYvj2lfSv/DtUbHCR31jEvgm7A
A1+OORGlWvxfzcIAdI/z0Sx3sHIrJeUDONFHOC6QLV9ewXqV7L3rYU4PyRVXUK+2+NOLcp
M0CoLcNlsVpF1fjw85NigxegOWW11mmHbjSwZvmVj7/YX8fzE/wz+yqU2iwSddE5HjUJkx
NC1dWI+dUrQANnkqsFGiWrk/H4vCLk8NZabHGv+OXRbKm8RVibWpoRSS2yPWX/W/BflvPv
yQS1fhk9zICYzQrWaQ/8DnPZSvb9VzYRM8Jr5+1WEft91G17Cb0FSZShemWwFWVz45equV
TqEn9JxAmzfZheHlaNMMUdo+soWebISTuorEKAjAH/q3t+ReBwaH5Q8Oc0V9UtWLLciiKY
e9AXkPPPcdm2R8XjzkRAdUOp8P6jc+z9HDjxuHXnsoEl/EhC2VbRdCGWjZVz+zCNrfmwmG
JGD3GErsy/Sl+lmHd2A6Uj+VTUuEOCP1vW8BNwa0HXCAkry6mEiX4NGiOCxe4U2jK9dQEe
9xKw5uErN8+YtAVVnPZKMFrDJpiEdS4nnUAtkcLcNki02HqMpBBD8COq8O8fxNllj6Serq
lpt9fWJBErair6NlQtt3mQ66Xtf6pbZhlkTCfWrWo2+byEzKHPW28USEVUZ7VZoc0F7Nif
5g8O9rA6SylPqWoJC9m/4KzZSNtOA=
-----END OPENSSH PRIVATE KEY-----
)";
constexpr const char* RSA_PUB =
    "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDQu3IMoAOHARpJ+IdzZqkqar/PysEUgJUIT2VTuOV/XljVI8H1+9vQV2lx58q6FdAD2Qcc4WUy"
    "xj5l9lwpki5m64zyyewTzkWawapqWCnDDaHUGFateY7RRafXTL1FbccuX/tJh4L7802ONTfgO4PmqAMrSntgIj7cuAKPvjRR4H3V3xkmr+r0VR8"
    "wRsayNPsSEqDUaIxoGEL6Sbp3j2kfWsoIDStqKmKgMRrRdAF+5hMKSrcU1IaYU6EvKQWxyPeKQtw072p19mGyDnjHYybrQRc8yWTQT6c6/ebIan"
    "FXNkE9jJWdoiZmdlPQ7IyPjL+vkrYz6cbs7dxr1+VITlef test-rsa-pass";

constexpr const char* RSA_PEM_KEY = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAsW4eHxLN16D37UEfVN8ZYoZk/UJK8aEVGRdmn1vQOH1YnyQ4
FA6FikC2CJ2wtRhbO50aKJ4BXpsQYUnjQLDPl+2DgeGjI8wZZ94uGfcVcN6cZ8Pr
9yD+MlsuvEvYOo1yZr18j2n+Zv9pyhND+u1sG4lCVkbVDvCVMR+Iv7LDhQ3C67Af
09874yFTL/FMqYdUm3r+5yu9wxtNmcjBp96Lwzk1VocYkQQyhr5aq7RjGF/1kdSP
ar4zIIYS5XUNgvsRWro9hUfDV0AptHB4paYNOrUJBCtkhgDjYg2BksQQVyve0sqF
jVuPe9DAmB7r6ncU/FLGnDk59jKIQfxu/3atjQIDAQABAoIBAALbbRdprBrkIq9A
CeB0qen7lAtn/TZK4RYYWbSzO8p3v5eVyQ65n842yzee98YoGuqo1aUAdlvZpLfh
rWEl97x7PKTjcfG/oEvF43l+OxV9it3jj8cuUX7qF6SR7Bd1SPmn5NDtD1sFPeTd
I+PEiFB2iLTu9btZ2xfWvtL8w/KT2+C21TetogB6BngJUJ9lDYbCa4IrKXxP+lDH
4zkT1iw6BDBy2t9LmprhwUwaqxk7xT3vDOomiP6vg4aFL7rx5MEuaubiNaUVZg1N
wQuCtddHiQ0gnzItRe4ascdl3mtwgPcduRaIYconHJO8PEqEsLON2nx/EMizaLpT
EpHJ1ZkCgYEA95v+p5sHC/e8srFI25QB1HD7PfrNktAIzpthsQcNPoMA78T29rZs
ymnOht49sWGEK5Xf5iNfliXNAdqPAlC1C1EKEH8xEM4VFeVVgPoxCO5GpI5XrDt3
VQ4UKPSuJRb2Dv9eA14+WgK42mI7/WBkO9JzLiUuA1T/AVEinm3mYA8CgYEAt3FR
6SOdZjrzrhBrJwG1eCApficSFRVTMVjA+rlNrunQ/p+KeaFjrle3BVKQ7JqCPlTU
b2pvJ3AByeDXREBFxeX8z2WOZwXsYPCgyRyOub+51an6xvm54j/E+lA5DlmJpTl2
fEzWTE6qj/iPUSqVYer9/SFxG9dUfpiZ4YB8PKMCgYEAqwqIC5FMedjk8W8DjCKe
bPiqpPLWBL9OBiJRhm7/mBn4kKg3jM8RMXWLY6CrbxeUrGL4akovIlH0Xn5yYUAM
/8c4M5MY+qnQ6PIlcVXif2/RPqTRLTlawldaV7xFe3tz94IxmBQG8DiHJtvK4sku
FCDYzZyDP2v8RxWb22LBS8MCgYA/ynJXhoEHudxfXWpFqDx4x4ZmTiPlnQLPGDQT
fDjdaIW65bP70/mb+Pi2Kx4miRxdnJY7IGIC7tpQX5W8MDWTOgUOAhzhCphGpabk
CMBBK8Dt4ixI4TKY0OAow5jzuoM5J6BiXT3wTzLU2fTMnuhPoq4sRmLU8a56T4rD
djC9zQKBgDs9TyKw3KQK2oOUImK/zAdnzzVSkX4eJ7bIg3OSgv4GnTOQBzXnqXkc
Me7ZA3YNkQUYR/RmSw0+qMSsY3Wz0LVVKJ/bACSSy107wokArE5Fb31ySH9zyo3J
nz8rvGPcFFmU7OfZ5frjo9aZpY7jFG1tL90m6wCuMJgSJr40p3Ap
-----END RSA PRIVATE KEY-----
)";
constexpr const char* RSA_PEM_PUB =
    "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQCxbh4fEs3XoPftQR9U3xlihmT9QkrxoRUZF2afW9A4fVifJDgUDoWKQLYInbC1GFs7nRoongFe"
    "mxBhSeNAsM+X7YOB4aMjzBln3i4Z9xVw3pxnw+v3IP4yWy68S9g6jXJmvXyPaf5m/2nKE0P67WwbiUJWRtUO8JUxH4i/ssOFDcLrsB/T3zvjIVM"
    "v8Uyph1Sbev7nK73DG02ZyMGn3ovDOTVWhxiRBDKGvlqrtGMYX/WR1I9qvjMghhLldQ2C+xFauj2FR8NXQCm0cHilpg06tQkEK2SGAONiDYGSxB"
    "BXK97SyoWNW4970MCYHuvqdxT8UsacOTn2MohB/G7/dq2N test-rsa-pem";

constexpr const char* ECDSA_KEY = R"(-----BEGIN OPENSSH PRIVATE KEY-----
b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAaAAAABNlY2RzYS
1zaGEyLW5pc3RwMjU2AAAACG5pc3RwMjU2AAAAQQSzoXZKhY7Q1FJaW+kdngYGLFvDAft5
IcclLnFuxlzyv+o6JgCQtrrjepZccn3IBnjbYGmz3gUx5dL7QJqKejcCAAAAqGEIc9RhCH
PUAAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBLOhdkqFjtDUUlpb
6R2eBgYsW8MB+3khxyUucW7GXPK/6jomAJC2uuN6llxyfcgGeNtgabPeBTHl0vtAmop6Nw
IAAAAgRuafaTCT3X0bc+hPkgTuU4a4dw0bvPLmKhlcDGpk6tsAAAAKdGVzdC1lY2RzYQEC
AwQFBg==
-----END OPENSSH PRIVATE KEY-----
)";

// A folder of its own for a test, empty.
fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-machines-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// The command `sh -c '...'` runs, as sh would read the quoted part (viaSh's quoting undone); the command as it is
// when it does not go through sh.
std::string unSh(const std::string& command) {
    const std::string prefix = "sh -c ";
    if (command.rfind(prefix, 0) != 0) return command;
    std::string out;
    bool quoted = false;
    for (std::size_t i = prefix.size(); i < command.size(); ++i) {
        const char c = command[i];
        if (c == '\'') {
            quoted = !quoted;
        } else if (!quoted && c == '\\' && i + 1 < command.size()) {
            out += command[++i];
        } else {
            out += c;
        }
    }
    return out;
}

// The first single-quoted argument after `word` in a POSIX command.
std::string quotedAfter(const std::string& command, const std::string& word) {
    const auto at = command.find(word + " '");
    if (at == std::string::npos) return {};
    const auto start = at + word.size() + 2;
    return command.substr(start, command.find('\'', start) - start);
}

SshTarget loopbackTarget(const fs::path& dir) {
    SshTarget t;
    t.name        = "test-box";
    t.host        = "127.0.0.1";
    t.port        = machinePort();
    t.user        = "tester";
    t.auth        = SshAuth::KeyFile;
    t.known_hosts = dir / "known_hosts.json";
    t.timeout     = 10s;
    return t;
}

// A machine as a Linux box answers: the probe, uname, sha256sum of the files under `root`, and a UCI engine.
ssh::MockServer::Command linuxBox(const fs::path& root, const std::string& command) {
    ssh::MockServer::Command reply;
    const auto inner = unSh(command);
    if (command == "echo %OS%") {
        reply.out = "%OS%\n";
    } else if (inner == "uname -s") {
        reply.out = "Linux\n";
    } else if (inner.rfind("echo os=", 0) == 0) {
        reply.out =
            "os=Linux\narch=x86_64\nrelease=6.8.0\nthreads=16\nmemory_kb=65536000\ndisk_kb=104857600\n"
            "home=/home/tester\n";
        if (fs::exists(root / "fastchess-gui/fastchess-gui")) reply.out += "fastchess-gui 0.5.9\nprotocol 1\n";
    } else if (inner.find("sha256sum") != std::string::npos) {
        const auto file = quotedAfter(inner, "sha256sum");
        const auto hash = sha256File(root / fs::path(file).relative_path());
        if (hash.empty()) {
            reply.err    = "sha256sum: " + file + ": No such file or directory\n";
            reply.status = 1;
        } else {
            reply.out = hash + "  " + file + "\n";
        }
    } else if (inner.rfind("chmod 755 ", 0) == 0) {
        const auto file = root / fs::path(quotedAfter(inner, "chmod 755")).relative_path();
        std::error_code ec;
        fs::permissions(file,
                        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        ec);
        if (ec) {
            reply.err    = "chmod: cannot access the file\n";
            reply.status = 1;
        }
    } else if (inner.rfind("command -v ", 0) == 0) {
        // The interpreters this box has.
        const auto name = quotedAfter(inner, "command -v");
        if (name == "/bin/sh" || name == "python3") {
            reply.out = name[0] == '/' ? name + "\n" : "/usr/bin/" + name + "\n";
        } else {
            reply.status = 1;
        }
    } else if (inner.find("--version") != std::string::npos) {
        reply.out = "fastchess-gui 0.5.9\nprotocol 1\n";
    } else if (inner.find("exec '/engines/mock'") != std::string::npos) {
        reply.talk = [](const std::string& input, bool closed, bool& ended) {
            std::string out;
            if (input.find("uci") != std::string::npos) out = "id name Mock 1.0\nid author Tests\nuciok\n";
            ended = closed || input.find("quit") != std::string::npos;
            return out;
        };
    } else if (inner.find("exec '/engines/mute'") != std::string::npos) {
        reply.talk = [](const std::string&, bool closed, bool& ended) {
            ended = closed;
            return std::string();
        };
    } else {
        reply.err    = "sh: not found\n";
        reply.status = 127;
    }
    return reply;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Quoting and the commands' answers

TEST_CASE("Machines: arguments quoted for a POSIX shell, cmd and PowerShell") {
    CHECK(shQuote("plain") == "'plain'");
    CHECK(shQuote("it's") == "'it'\\''s'");
    CHECK(shQuote("$HOME `x` \"y\"") == "'$HOME `x` \"y\"'");
    CHECK(viaSh("echo 'a b'") == "sh -c 'echo '\\''a b'\\'''");
    CHECK(unSh(viaSh("echo 'a b' && cd 'it'\\''s'")) == "echo 'a b' && cd 'it'\\''s'");

    CHECK(cmdQuote("C:\\Program Files\\Engine\\sf.exe") == "\"C:\\Program Files\\Engine\\sf.exe\"");
    CHECK(cmdQuote("a & b | c > d ^ (e)") == "\"a & b | c > d ^ (e)\"");
    // A backslash before the closing quote would escape it for the program: doubled.
    CHECK(cmdQuote("C:\\engines\\") == "\"C:\\engines\\\\\"");
    CHECK_FALSE(cmdQuote("say \"hi\"").has_value());
    CHECK_FALSE(cmdQuote("100%").has_value());
    CHECK_FALSE(cmdQuote("two\nlines").has_value());
    CHECK(quoteFor(RemoteShell::Cmd, "x y") == "\"x y\"");
    CHECK(quoteFor(RemoteShell::Posix, "x y") == "'x y'");
    CHECK(psQuote("it's") == "it''s");
}

TEST_CASE("Machines: the shell told by echo %OS%, key=value lines, fastchess-gui's --version") {
    CHECK(shellFromEcho("Windows_NT\r\n") == RemoteShell::Cmd);
    CHECK(shellFromEcho("%OS%\n") == RemoteShell::Posix);
    CHECK(shellFromEcho("") == RemoteShell::Unknown);

    const auto v = keyValues("os=Linux\r\n  threads = 16\nnot a pair\n=x\nbad key=1\nos=Darwin\n");
    CHECK(v.at("os") == "Darwin");
    CHECK(v.at("threads") == "16");
    CHECK(v.count("bad key") == 0);

    CHECK(parseVersion("fastchess-gui 0.5.9\nprotocol 1\n") == std::pair<std::string, int>("0.5.9", 1));
    CHECK(parseVersion("fastchess-gui 0.6.0\r\n") == std::pair<std::string, int>("0.6.0", 0));
    // Something else answering, or the window of an old version that does not know --version.
    CHECK(parseVersion("bash: fastchess-gui: command not found\nprotocol 1").first.empty());
    CHECK(parseVersion("fastchess-gui [--profile DIR]\n").first.empty());
}

TEST_CASE("Machines: the probes' answers read, for Linux, macOS and Windows") {
    RemoteSystem lin;
    readPosixProbe(
        "os=Linux\narch=x86_64\nrelease=6.8.0-45\nthreads=32\nmemory_kb=131072000\ndisk_kb=2097152\n"
        "home=/home/me\n",
        lin);
    CHECK(lin.os == "Linux");
    CHECK(lin.arch == "x86_64");
    CHECK(lin.threads == 32);
    CHECK(lin.memory_mb == 128000);
    CHECK(lin.disk_free_mb == 2048);
    CHECK(lin.home == "/home/me");

    RemoteSystem mac;
    readPosixProbe("os=Darwin\narch=arm64\nthreads=10\nmemory_bytes=17179869184\ndisk_kb=1048576\n", mac);
    CHECK(mac.memory_mb == 16384);
    CHECK(normalizePlatform(mac.os, mac.arch) == Platform{"Darwin", "arm64"});

    RemoteSystem win;
    readWindowsProbe(
        "os=Windows\r\narch=AMD64\r\nthreads=24\r\nhome=C:\\Users\\me\r\n\r\n"
        "Microsoft Windows [Version 10.0.22631.4317]\r\nmemory_bytes=68719476736\r\n"
        "disk_bytes=536870912000\r\n",
        win);
    CHECK(win.os == "Windows");
    CHECK(win.threads == 24);
    CHECK(win.memory_mb == 65536);
    CHECK(win.disk_free_mb == 512000);
    CHECK(win.release == "10.0.22631.4317");
    CHECK(normalizePlatform(win.os, win.arch) == Platform{"Windows", "x86_64"});
    CHECK(normalizePlatform("MINGW64_NT-10.0-19045", "x86_64").os == "Windows");
    CHECK(normalizePlatform("Linux", "aarch64").arch == "arm64");

    // The probes quote the folder for their shell; cmd cannot take some names at all.
    CHECK(posixProbe("my folder").find("'\\''my folder'\\''") != std::string::npos);
    REQUIRE(windowsProbe("D:\\fcg").has_value());
    CHECK(windowsProbe("D:\\fcg")->find("if exist \"D:\\fcg\\fastchess-gui.exe\"") != std::string::npos);
    CHECK_FALSE(windowsProbe("100%").has_value());
}

TEST_CASE("Machines: SHA-256 as sha256sum, shasum and certutil print it; starting an engine from its folder") {
    const std::string hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    CHECK(readHash(hash + "  book.epd\n") == hash);
    CHECK(readHash("SHA256 hash of book.epd:\r\n" + hash + "\r\nCertUtil: -hashfile command completed successfully.") ==
          hash);
    // Older Windows: the bytes separated by spaces, in capitals.
    std::string spaced;
    for (std::size_t i = 0; i < hash.size(); i += 2) spaced += (i ? " " : "") + hash.substr(i, 2);
    for (auto& c : spaced) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(readHash("SHA256 hash of file x:\n" + spaced + "\n") == hash);
    CHECK(readHash("sha256sum: x: No such file or directory\n").empty());

    CHECK(hashCommand(RemoteShell::Cmd, "C:\\books\\a.bin") == "certutil -hashfile \"C:\\books\\a.bin\" SHA256");
    CHECK(unSh(*hashCommand(RemoteShell::Posix, "books/a b.bin")) ==
          "sha256sum 'books/a b.bin' 2>/dev/null || shasum -a 256 'books/a b.bin'");

    CHECK(unSh(*startCommand(RemoteShell::Posix, "/opt/sf/stockfish")) == "cd '/opt/sf' && exec '/opt/sf/stockfish'");
    CHECK(startCommand(RemoteShell::Cmd, "C:\\Engines\\sf.exe") == "cd /d \"C:\\Engines\" && \"C:\\Engines\\sf.exe\"");
    CHECK(startCommand(RemoteShell::Cmd, "sf.exe") == "\"sf.exe\"");
    CHECK_FALSE(startCommand(RemoteShell::Cmd, "C:\\100%\\sf.exe").has_value());
}

TEST_CASE("Machines: SFTP's paths, a remote path's folder and name") {
    CHECK(sftpPath("C:\\Engines\\sf.exe") == "/C:/Engines/sf.exe");
    CHECK(sftpPath("/opt/sf") == "/opt/sf");
    CHECK(sftpPath("fastchess-gui/books") == "fastchess-gui/books");
    CHECK(remoteParent("/opt/sf/stockfish") == "/opt/sf");
    CHECK(remoteParent("C:\\Engines\\sf.exe") == "C:\\Engines");
    CHECK(remoteParent("/stockfish") == "/");
    CHECK(remoteParent("stockfish").empty());
    CHECK(remoteName("C:\\Engines\\sf.exe") == "sf.exe");
    CHECK(remoteJoin("fastchess-gui", "books") == "fastchess-gui/books");
    CHECK(remoteJoin("/", "x") == "/x");
    CHECK(remoteJoin("", "x") == "x");
}

// ---------------------------------------------------------------------------------------------------------------
// The machines' file, budgets, what to install

TEST_CASE("Machines: kept in machines.json without secrets, read back as written") {
    const auto dir = freshDir("file");
    Machine m;
    m.id         = "m00000001";
    m.name       = "test-box";
    m.host       = "box.example.org";
    m.port       = 2222;
    m.user       = "chess";
    m.auth       = SshAuth::KeyFile;
    m.key_file   = (dir / "id_ed25519").string();
    m.folder     = "/srv/fcg";
    m.syzygy_dir = "/data/syzygy";
    m.cores      = 30;
    m.engines.push_back({"Stockfish 17", "/opt/sf/stockfish", 1, "Answers uci as Stockfish 17.", "Stockfish 17"});
    m.system.shell     = RemoteShell::Posix;
    m.system.os        = "Linux";
    m.system.arch      = "x86_64";
    m.system.threads   = 32;
    m.system.memory_mb = 65536;
    m.system.checked   = "2026-11-15 10:00";
    REQUIRE(saveMachines(dir, {m}));
    const auto text = readFile(dir / "machines.json");
    CHECK(text.find("passphrase") == std::string::npos);

    const auto back = loadMachines(dir);
    REQUIRE(back.size() == 1);
    const auto& b = back[0];
    CHECK(b.name == "test-box");
    CHECK(b.port == 2222);
    CHECK(b.auth == SshAuth::KeyFile);
    CHECK(b.key_file == m.key_file);
    CHECK(b.engines == m.engines);
    CHECK(b.system.shell == RemoteShell::Posix);
    CHECK(b.system.threads == 32);
    CHECK(b.address() == "chess@box.example.org:2222");
    CHECK(b.booksDir() == "/srv/fcg/books");
    CHECK(b.syzygyDir() == "/data/syzygy");
    CHECK(b.programPath() == "/srv/fcg/fastchess-gui");
    CHECK(b.coreBudget() == 30);
    CHECK(b.memoryBudget() == 65536 * 8 / 10);  // none chosen: 80 % of what the test found

    Machine fresh;
    CHECK(fresh.coreBudget() == 1);
    fresh.system.threads = 16;
    CHECK(fresh.coreBudget() == 15);
    fresh.system.shell = RemoteShell::Cmd;
    CHECK(fresh.programPath() == "fastchess-gui\\fastchess-gui.exe");
    CHECK_FALSE(incompleteMachine(fresh).empty());
    fresh.host = "h";
    fresh.user = "u";
    CHECK(incompleteMachine(fresh).empty());
    CHECK(newMachineId({m}) != m.id);

    // A file written by hand, broken: no machine rather than a crash.
    writeFile(dir / "machines.json", "[{\"name\": 5}, 3, {\"host\": \"ok\"}]");
    const auto loose = loadMachines(dir);
    REQUIRE(loose.size() == 1);
    CHECK(loose[0].host == "ok");
    fs::remove_all(dir);
}

TEST_CASE("Machines: the program installed is this one for the same system, the one chosen for another") {
    const auto dir  = freshDir("install-choice");
    const auto self = dir / "fastchess-gui";
    Machine m;
    std::string error;
    CHECK(programToInstall(m, self, error).empty());  // not tested yet
    CHECK_FALSE(error.empty());

    const auto here = thisPlatform();
    m.system.os     = here.os;
    m.system.arch   = here.arch == "x86_64" ? "amd64" : here.arch;
    error.clear();
    CHECK(programToInstall(m, self, error) == self);

    m.system.os   = here.os == "Windows" ? "Linux" : "Windows";
    m.system.arch = "AMD64";
    error.clear();
    CHECK(programToInstall(m, self, error).empty());
    CHECK(error.find("fastchess-gui") != std::string::npos);
    m.program = (dir / "fastchess-gui.exe").string();
    error.clear();
    CHECK(programToInstall(m, self, error).empty());  // chosen, but not there
    writeFile(m.program, "MZ");
    error.clear();
    CHECK(programToInstall(m, self, error) == fs::path(m.program));
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// Books and tablebases

TEST_CASE("Machines: what a machine's folder lacks, by name and size, and by hash for the books") {
    const std::vector<SyncFile> local = {
        {"KQvK.rtbw", 100, "", {}}, {"KRvK.rtbw", 200, "", {}}, {"KPvK.rtbz", 300, "", {}}, {"book.bin", 50, "aa", {}}};
    const std::vector<SyncFile> remote = {
        {"KQvK.rtbw", 100, "", {}}, {"KRvK.rtbw", 150, "", {}}, {"book.bin", 50, "bb", {}}};
    const auto plan = planSync(local, remote, false);
    CHECK(plan.same == 2);
    REQUIRE(plan.missing.size() == 1);
    CHECK(plan.missing[0].name == "KPvK.rtbz");
    REQUIRE(plan.different.size() == 1);
    CHECK(plan.different[0].name == "KRvK.rtbw");
    CHECK(plan.bytes == 500);
    // A book of the same name and size but another content is not the same book.
    const auto by_hash = planSync({local[3]}, {remote[2]}, true);
    CHECK(by_hash.different.size() == 1);
    CHECK_FALSE(by_hash.complete());
    CHECK(planSync({}, remote, true).complete());
}

TEST_CASE("Machines: the Syzygy files offered, 6 pieces at most, each name once") {
    CHECK(syzygyPieces("KQvK.rtbw") == 3);
    CHECK(syzygyPieces("KQRPvKRP.rtbz") == 7);
    CHECK(syzygyPieces("KRPPvKR.rtbz") == 6);
    CHECK(syzygyPieces("KQRvKR.rtbw") == 5);
    CHECK(syzygyPieces("KQvK.txt") == 0);
    CHECK(syzygyPieces("readme.rtbw") == 0);
    CHECK(syzygyPieces("KvK.rtbw") == 0);

    const auto a = freshDir("syzygy-a"), b = freshDir("syzygy-b");
    writeFile(a / "KQvK.rtbw", std::string(80, 'x'));
    writeFile(a / "KQvK.rtbz", std::string(144, 'x'));
    writeFile(a / "KQRvKRP.rtbw", std::string(208, 'x'));
    writeFile(a / "KQRPvKRP.rtbw", std::string(272, 'x'));  // 7 pieces: not offered
    writeFile(a / "notes.txt", "x");
    writeFile(b / "KQvK.rtbw", std::string(16, 'x'));  // the first folder's is the one
    writeFile(b / "KRvK.rtbw", std::string(80, 'x'));
#ifdef _WIN32
    const auto paths = a.string() + ";" + b.string();
#else
    const auto paths = a.string() + ":" + b.string();
#endif
    const auto files = localSyzygyFiles(paths, 0);
    std::vector<std::string> names;
    for (const auto& f : files) names.push_back(f.name);
    CHECK(names == std::vector<std::string>{"KQRvKRP.rtbw", "KQvK.rtbw", "KQvK.rtbz", "KRvK.rtbw"});
    CHECK(files[1].size == 80);
    CHECK(localSyzygyFiles(paths, 3).size() == 3);  // the adjudication's 3 pieces

    const auto listing = std::vector<RemoteEntry>{{"KQvK.rtbw", 80, false, true, 0644, 0},
                                                  {"KQRPvKRP.rtbw", 272, false, true, 0644, 0},
                                                  {"sub", 0, true, false, 0755, 0}};
    CHECK(remoteSyzygyFiles(listing, 0).size() == 1);
    fs::remove_all(a);
    fs::remove_all(b);
}

TEST_CASE("Machines: SHA-256 of a file, as FIPS 180-2 gives it") {
    const auto dir = freshDir("sha");
    writeFile(dir / "abc", "abc");
    CHECK(sha256File(dir / "abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(dir / "missing").empty());
    fs::remove_all(dir);
}

TEST_CASE("Machines: sizes for the window") {
    CHECK(formatBytes(512) == "512 bytes");
    CHECK(formatBytes(12 * 1024 * 1024) == "12 MB");
    CHECK(formatBytes(uint64_t(9800) * 1024 * 1024) == "9.6 GB");
}

// ---------------------------------------------------------------------------------------------------------------
// The user's keys

TEST_CASE("Ed25519: RFC 8032's signatures, and public keys from their seeds") {
    struct Vector {
        const char *seed, *pub, *message, *signature;
    };
    const Vector vectors[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bb"
         "e"
         "24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302"
         "a"
         "eeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed"
         "2"
         "8dc027beceea1ec40a"},
    };
    const auto unhex = [](std::string_view hex) {
        std::string out;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            out += static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16));
        }
        return out;
    };
    const auto hex = [](const uint8_t* data, std::size_t n) {
        std::string out;
        constexpr char DIGITS[] = "0123456789abcdef";
        for (std::size_t i = 0; i < n; ++i) {
            out += DIGITS[data[i] >> 4];
            out += DIGITS[data[i] & 15];
        }
        return out;
    };
    for (const auto& v : vectors) {
        const auto seed    = unhex(v.seed);
        const auto message = unhex(v.message);
        uint8_t pub[32], sig[64];
        REQUIRE(fcg_ed25519_public(pub, reinterpret_cast<const uint8_t*>(seed.data())) == 0);
        CHECK(hex(pub, 32) == v.pub);
        REQUIRE(fcg_ed25519_sign(sig, reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                                 reinterpret_cast<const uint8_t*>(seed.data()), pub) == 0);
        CHECK(hex(sig, 64) == v.signature);
    }
}

TEST_CASE("Machines: key files looked at before connecting, their passphrase told") {
    const auto dir = freshDir("keyfiles");
    writeFile(dir / "ed", ED25519_KEY);
    writeFile(dir / "pem", RSA_PEM_KEY);
    writeFile(dir / "ecdsa", ECDSA_KEY);
    writeFile(dir / "ed.pub", ED25519_PUB);

    const auto ed = inspectKeyFile(dir / "ed");
    CHECK(ed.readable);
    CHECK(ed.encrypted);
    CHECK(ed.type == "ssh-ed25519");
    CHECK(ed.error.empty());

    const auto pem = inspectKeyFile(dir / "pem");
    CHECK_FALSE(pem.encrypted);
    CHECK(pem.type == "ssh-rsa");
    CHECK(pem.error.empty());

    const auto ecdsa = inspectKeyFile(dir / "ecdsa");
    CHECK(ecdsa.type == "ecdsa-sha2-nistp256");
    CHECK(ecdsa.error.find("Ed25519") != std::string::npos);

    CHECK_FALSE(inspectKeyFile(dir / "ed.pub").error.empty());  // the public key, not the private one
    CHECK_FALSE(inspectKeyFile(dir / "none").readable);
    fs::remove_all(dir);
}

TEST_CASE("libssh2 backend: the public key of RSA and Ed25519 key files, a passphrase missing or wrong refused") {
    REQUIRE(libssh2_init(0) == 0);
    LIBSSH2_SESSION* session = libssh2_session_init();
    REQUIRE(session != nullptr);
    struct Case {
        const char *key, *pub, *passphrase, *method;
    };
    const Case cases[] = {{ED25519_KEY, ED25519_PUB, "chess", "ssh-ed25519"},
                          {RSA_KEY, RSA_PUB, "chess", "ssh-rsa"},
                          {RSA_PEM_KEY, RSA_PEM_PUB, nullptr, "ssh-rsa"}};
    for (const auto& c : cases) {
        char* method          = nullptr;
        unsigned char* blob   = nullptr;
        std::size_t blob_len  = 0;
        const std::string key = c.key;
        REQUIRE(ssh2_pub_privkey(session, &method, &blob, &blob_len, nullptr, key.data(), key.size(), c.passphrase) ==
                0);
        CHECK(std::string(method) == c.method);
        CHECK(std::string(reinterpret_cast<char*>(blob), blob_len) == ssh::publicKeyBlob(c.pub));
        libssh2_free(session, method);
        libssh2_free(session, blob);
    }
    char* method         = nullptr;
    unsigned char* blob  = nullptr;
    std::size_t len      = 0;
    const std::string ed = ED25519_KEY;
    CHECK(ssh2_pub_privkey(session, &method, &blob, &len, nullptr, ed.data(), ed.size(), nullptr) ==
          LIBSSH2_ERROR_KEYFILE_AUTH_FAILED);
    CHECK(ssh2_pub_privkey(session, &method, &blob, &len, nullptr, ed.data(), ed.size(), "checkers") ==
          LIBSSH2_ERROR_KEYFILE_AUTH_FAILED);
    const std::string ecdsa = ECDSA_KEY;
    CHECK(ssh2_pub_privkey(session, &method, &blob, &len, nullptr, ecdsa.data(), ecdsa.size(), nullptr) != 0);
    libssh2_session_free(session);
}

TEST_CASE("libssh2 backend: an RSA key file signs, the signature checked with its public key") {
    REQUIRE(libssh2_init(0) == 0);
    LIBSSH2_SESSION* session = libssh2_session_init();
    REQUIRE(session != nullptr);
    // OpenSSH's format leaves out d mod (p - 1) and d mod (q - 1), which PSA checks: computed right, or no key.
    for (const auto& [key, pub, passphrase] :
         {std::tuple{RSA_KEY, RSA_PUB, "chess"}, std::tuple{RSA_PEM_KEY, RSA_PEM_PUB, (const char*)nullptr}}) {
        const std::string text = key;
        ssh2_rsa_ctx* priv     = nullptr;
        REQUIRE(ssh2_rsa_new_priv(&priv, session, nullptr, text.data(), text.size(), passphrase) == 0);
        const std::string message = "the exchange hash, and what else SSH signs";
        uint8_t hash[32];
        std::size_t n = 0;
        REQUIRE(psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(message.data()), message.size(),
                                 hash, sizeof hash, &n) == PSA_SUCCESS);
        unsigned char* sig  = nullptr;
        std::size_t sig_len = 0;
        REQUIRE(ssh2_rsa_sha2_sign(priv, session, hash, sizeof hash, &sig, &sig_len) == 0);
        CHECK(sig_len == 256);

        const auto blob = ssh::publicKeyBlob(pub);
        ssh::Reader r(blob);
        r.string();
        const std::string e(r.string()), nn(r.string());
        ssh2_rsa_ctx* public_key = nullptr;
        REQUIRE(ssh2_rsa_new(&public_key, session, reinterpret_cast<const unsigned char*>(e.data()), e.size(),
                             reinterpret_cast<const unsigned char*>(nn.data()), nn.size(), nullptr, 0, nullptr, 0,
                             nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0) == 0);
        CHECK(ssh2_rsa_sha2_verify(public_key, session, 32, sig, sig_len,
                                   reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 0);
        libssh2_free(session, sig);
        ssh2_rsa_free(public_key, session);
        ssh2_rsa_free(priv, session);
    }
    libssh2_session_free(session);
}

// ---------------------------------------------------------------------------------------------------------------
// The client against the in-process server

TEST_CASE("SSH client: logs in with an Ed25519 or RSA key file, a passphrase missing or wrong told as such") {
    const auto dir = freshDir("login");
    writeFile(dir / "ed", ED25519_KEY);
    writeFile(dir / "rsa", RSA_KEY);
    writeFile(dir / "pem", RSA_PEM_KEY);
    ssh::MockServer::Options options;
    options.keys = {ssh::publicKeyBlob(ED25519_PUB), ssh::publicKeyBlob(RSA_PUB)};
    options.root = dir;
    ssh::MockServer server(machinePort(), options);

    auto target       = loopbackTarget(dir);
    target.key_file   = dir / "ed";
    target.passphrase = "chess";
    {
        SshClient client;
        REQUIRE_MESSAGE(client.connect(target), client.error().message);
        CHECK(client.hostKeyNew());  // trusted on first use, and kept
        CHECK(client.hostKey().rfind("SHA256:", 0) == 0);
    }
    CHECK(server.ed25519_logins == 1);
    {
        target.key_file = dir / "rsa";
        SshClient client;
        REQUIRE_MESSAGE(client.connect(target), client.error().message);
        CHECK_FALSE(client.hostKeyNew());
    }
    CHECK(server.rsa_logins == 1);  // its signature checked by the server
    {
        target.passphrase.clear();
        SshClient client;
        CHECK_FALSE(client.connect(target));
        CHECK(client.error().kind == SshError::Kind::Passphrase);
        target.passphrase = "checkers";
        CHECK_FALSE(client.connect(target));
        CHECK(client.error().kind == SshError::Kind::Passphrase);
        CHECK(client.error().message.find("not the right one") != std::string::npos);
    }
    {
        // A key the server does not know.
        target.key_file = dir / "pem";
        target.passphrase.clear();
        SshClient client;
        CHECK_FALSE(client.connect(target));
        CHECK(client.error().kind == SshError::Kind::Auth);
        CHECK(client.error().message.find("authorized_keys") != std::string::npos);
    }
    CHECK(server.error().empty());
    fs::remove_all(dir);
}

TEST_CASE("SSH client: a host key that changed refused, the new one offered to the user") {
    const auto dir = freshDir("hostkey");
    ssh::MockServer::Options options;
    options.root = dir;
    ssh::MockServer server(machinePort(), options);
    auto target = loopbackTarget(dir);
    REQUIRE(rememberHostKey(target.known_hosts, target.host, target.port, "SHA256:another"));
    SshClient client;
    CHECK_FALSE(client.connect(target));
    CHECK(client.error().kind == SshError::Kind::HostKey);
    CHECK(client.error().new_key.rfind("SHA256:", 0) == 0);
    CHECK(client.error().message.find("changed") != std::string::npos);
    CHECK(server.logins == 0);  // nothing sent past the key
    // Trusted by the user: the next connection goes.
    REQUIRE(rememberHostKey(target.known_hosts, target.host, target.port, client.error().new_key));
    SshClient again;
    CHECK(again.connect(target));
    fs::remove_all(dir);
}

TEST_CASE("SSH client: commands with their output, error output and exit code; a program talked to") {
    const auto dir = freshDir("exec");
    ssh::MockServer::Options options;
    options.root = dir;
    options.exec = [](const std::string& command) {
        ssh::MockServer::Command reply;
        if (command == "hello") {
            reply.out = "hello\n";
            reply.err = "a warning\n";
        } else if (command == "big") {
            reply.out = std::string(3 << 20, 'x');  // more than the windows, and than what is kept
        } else if (command == "cat") {
            reply.talk = [](const std::string& input, bool closed, bool& ended) {
                ended = closed;
                return input;
            };
        } else {
            reply.err    = "not found\n";
            reply.status = 127;
        }
        return reply;
    };
    ssh::MockServer server(machinePort(), options);
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    const auto hello = client.run("hello", 5s);
    REQUIRE(hello);
    CHECK(hello->out == "hello\n");
    CHECK(hello->err == "a warning\n");
    CHECK(hello->exit_code == 0);
    const auto missing = client.run("nothing", 5s);
    REQUIRE(missing);
    CHECK(missing->exit_code == 127);
    const auto big = client.run("big", 10s, {}, 1 << 20);
    REQUIRE(big);
    CHECK(big->out.size() == std::size_t(1) << 20);
    const auto echoed = client.run("cat", 5s, "some input");
    REQUIRE(echoed);
    CHECK(echoed->out == "some input");

    auto program = client.start("cat");
    REQUIRE(program);
    CHECK(program->write("uci\n"));
    std::string seen;
    for (int i = 0; i < 20 && seen.find("uci\n") == std::string::npos; ++i) {
        if (auto chunk = program->read(200ms)) seen += *chunk;
    }
    CHECK(seen == "uci\n");
    program->closeInput();
    std::optional<std::string> end = std::string();
    for (int i = 0; i < 20 && end; ++i) end = program->read(100ms);
    CHECK_FALSE(end.has_value());  // it ended
    program.reset();
    CHECK(client.run("hello", 5s).has_value());  // the connection goes on
    fs::remove_all(dir);
}

TEST_CASE("SSH client: SFTP folders, files, renames over a file, a copy each way") {
    const auto dir  = freshDir("sftp");
    const auto root = dir / "home";
    fs::create_directories(root);
    ssh::MockServer::Options options;
    options.root = root;
    ssh::MockServer server(machinePort(), options);
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));

    CHECK(client.makeDirs("fastchess-gui/books"));
    CHECK(client.makeDirs("fastchess-gui/books"));  // there already
    CHECK(fs::is_directory(root / "fastchess-gui/books"));
    CHECK_FALSE(client.stat("nothing").has_value());
    CHECK(client.error().kind == SshError::Kind::NotFound);

    std::string content;
    for (int i = 0; i < 200000; ++i) content += static_cast<char>('a' + i % 26);
    writeFile(dir / "book.epd", content);
    uint64_t last = 0;
    CHECK(client.upload(dir / "book.epd", "fastchess-gui/books/book.epd", [&](uint64_t done, uint64_t total) {
        CHECK(total == content.size());
        last = done;
        return true;
    }));
    CHECK(last == content.size());
    CHECK(readFile(root / "fastchess-gui/books/book.epd") == content);
    CHECK_FALSE(fs::exists(root / "fastchess-gui/books/book.epd.part"));
    const auto st = client.stat("fastchess-gui/books/book.epd");
    REQUIRE(st);
    CHECK(st->size == content.size());
    CHECK(st->regular);

    // Again, over the file there: replaced.
    writeFile(dir / "book.epd", "short");
    CHECK(client.upload(dir / "book.epd", "fastchess-gui/books/book.epd"));
    CHECK(readFile(root / "fastchess-gui/books/book.epd") == "short");

    const auto listing = client.list("fastchess-gui/books");
    REQUIRE(listing);
    REQUIRE(listing->size() == 1);
    CHECK((*listing)[0].name == "book.epd");

    CHECK(client.download("fastchess-gui/books/book.epd", dir / "back.epd"));
    CHECK(readFile(dir / "back.epd") == "short");
    CHECK(client.setPermissions("fastchess-gui/books/book.epd", 0755));
    CHECK(client.remove("fastchess-gui/books/book.epd"));
    CHECK(client.remove("fastchess-gui/books/book.epd"));  // gone already
    CHECK(client.realPath(".").has_value());
    CHECK(server.error().empty());
    fs::remove_all(dir);
}

TEST_CASE("SSH client: a copy cut by the network resumes where it stopped") {
    const auto dir  = freshDir("resume");
    const auto root = dir / "home";
    fs::create_directories(root);
    std::string content;
    for (int i = 0; i < 3 * 1024 * 1024; ++i) content += static_cast<char>(i * 7 % 251);
    writeFile(dir / "KRPvKR.rtbw", content);
    {
        ssh::MockServer::Options options;
        options.root      = root;
        options.cut_after = 1024 * 1024;  // a third of the way
        ssh::MockServer server(machinePort(), options);
        SshClient client;
        REQUIRE(client.connect(loopbackTarget(dir)));
        CHECK_FALSE(client.upload(dir / "KRPvKR.rtbw", "KRPvKR.rtbw"));
        CHECK(client.error().kind == SshError::Kind::Network);
    }
    const auto left = fs::file_size(root / "KRPvKR.rtbw.part");
    CHECK(left >= 1024 * 1024);
    CHECK(left < content.size());
    ssh::MockServer::Options options;
    options.root = root;
    ssh::MockServer server(machinePort(), options);
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    uint64_t first = 0;
    CHECK(client.upload(dir / "KRPvKR.rtbw", "KRPvKR.rtbw", [&](uint64_t done, uint64_t) {
        if (first == 0) first = done;
        return true;
    }));
    CHECK(first == left);  // started from what was there
    CHECK(server.written == content.size() - left);
    CHECK(readFile(root / "KRPvKR.rtbw") == content);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// The jobs

TEST_CASE("Machine jobs: the test finds the system, Install copies the program and checks it answers") {
    const auto dir  = freshDir("jobs-test");
    const auto root = dir / "home";
    fs::create_directories(root);
    ssh::MockServer::Options options;
    options.root = root;
    options.exec = [root](const std::string& command) { return linuxBox(root, command); };
    ssh::MockServer server(machinePort(), options);

    Machine m;
    m.name = "test-box";
    m.host = "127.0.0.1";
    m.port = machinePort();
    m.user = "tester";
    {
        MachineJob job(MachineJob::Kind::Test, targetOf(m, dir, ""),
                       [&m](SshClient& c, JobState& s, JobResult& r) { testMachine(c, m, s, r); });
        for (int i = 0; i < 500 && !job.finished(); ++i) std::this_thread::sleep_for(20ms);
        REQUIRE(job.finished());
        auto result = job.take();
        REQUIRE_MESSAGE(result.ok, result.error.message);
        CHECK(result.host_key_new);
        REQUIRE(result.system);
        CHECK(result.system->shell == RemoteShell::Posix);
        CHECK(result.system->threads == 16);
        CHECK(result.system->memory_mb == 64000);
        CHECK(result.system->fcg_version.empty());
        CHECK(result.lines.back().find("Install") != std::string::npos);
        m.system = *result.system;
    }
    // Install: this program for Linux x86-64, any file standing for it here.
    writeFile(dir / "fastchess-gui", "#!/bin/sh\necho program\n");
    JobState state;
    JobResult result;
    SshClient client;
    REQUIRE(client.connect(targetOf(m, dir, "")));
    installProgram(client, m, dir / "fastchess-gui", state, result);
    REQUIRE_MESSAGE(result.ok, result.error.message);
    CHECK(readFile(root / "fastchess-gui/fastchess-gui") == "#!/bin/sh\necho program\n");
    REQUIRE(result.system);
    CHECK(result.system->fcg_version == "0.5.9");
    CHECK(result.system->fcg_protocol == 1);
#ifndef _WIN32
    CHECK((fs::status(root / "fastchess-gui/fastchess-gui").permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    CHECK(server.error().empty());
    fs::remove_all(dir);
}

TEST_CASE("Machine jobs: a Windows machine's cmd told, its probe and engines' commands written for cmd") {
    const auto dir = freshDir("jobs-windows");
    ssh::MockServer::Options options;
    options.root = dir;
    options.exec = [](const std::string& command) {
        ssh::MockServer::Command reply;
        if (command == "echo %OS%") {
            reply.out = "Windows_NT\r\n";
        } else if (command.rfind("echo os=Windows& ", 0) == 0) {
            reply.out =
                "os=Windows\r\narch=AMD64\r\nthreads=12\r\nhome=C:\\Users\\tester\r\n\r\nMicrosoft Windows "
                "[Version 10.0.26100.2033]\r\nmemory_bytes=34359738368\r\ndisk_bytes=107374182400\r\n";
        } else {
            reply.err    = "'" + command + "' is not recognized as an internal or external command\r\n";
            reply.status = 1;
        }
        return reply;
    };
    ssh::MockServer server(machinePort(), options);
    Machine m;
    m.name = "win-box";
    m.host = "127.0.0.1";
    m.port = machinePort();
    m.user = "tester";
    SshClient client;
    REQUIRE(client.connect(targetOf(m, dir, "")));
    JobState state;
    JobResult result;
    testMachine(client, m, state, result);
    REQUIRE_MESSAGE(result.ok, result.error.message);
    CHECK(result.system->shell == RemoteShell::Cmd);
    CHECK(result.system->os == "Windows");
    CHECK(result.system->release == "10.0.26100.2033");
    CHECK(result.system->memory_mb == 32768);
    CHECK(result.system->disk_free_mb == 102400);
    // Nothing went through sh.
    for (const auto& c : server.commands()) CHECK(c.rfind("sh -c", 0) != 0);
    fs::remove_all(dir);
}

TEST_CASE("Machine jobs: engines' paths checked there: missing, a folder, mute, answering uci") {
    const auto dir  = freshDir("jobs-engines");
    const auto root = dir / "home";
    fs::create_directories(root / "engines/folder");
    writeFile(root / "engines/mock", "binary");
    writeFile(root / "engines/mute", "binary");
#ifndef _WIN32
    fs::permissions(root / "engines/mock", fs::perms::owner_all);
    fs::permissions(root / "engines/mute", fs::perms::owner_all);
#endif
    ssh::MockServer::Options options;
    options.root = root;
    options.exec = [root](const std::string& command) { return linuxBox(root, command); };
    ssh::MockServer server(machinePort(), options);
    Machine m;
    m.name         = "test-box";
    m.host         = "127.0.0.1";
    m.port         = machinePort();
    m.user         = "tester";
    m.system.shell = RemoteShell::Posix;
    m.engines      = {{"Mock", "/engines/mock", -1, "", ""},
                      {"Gone", "/engines/gone", -1, "", ""},
                      {"Folder", "/engines/folder", -1, "", ""},
                      {"Mute", "/engines/mute", -1, "", ""},
                      {"Unset", "", -1, "", ""}};
    SshClient client;
    REQUIRE(client.connect(targetOf(m, dir, "")));
    JobState state;
    JobResult result;
    checkEngines(client, m, {}, 1500ms, state, result);
    CHECK_FALSE(result.ok);
    REQUIRE(result.engines.size() == 5);
    CAPTURE(result.engines[0].check_message);
    CAPTURE(result.engines[3].check_message);
    CHECK(result.engines[0].check_ok == 1);
    CHECK(result.engines[0].id_name == "Mock 1.0");
    CHECK(result.engines[1].check_ok == 0);
    CHECK(result.engines[1].check_message.find("not on test-box") != std::string::npos);
    CHECK(result.engines[2].check_message.find("folder") != std::string::npos);
    CHECK(result.engines[3].check_ok == 0);
    CHECK(result.engines[3].check_message.find("said nothing") != std::string::npos);
    CHECK(result.engines[4].check_ok == -1);
    CHECK(result.error.message.find("3 engines") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Machine jobs: books and tablebases compared, the missing ones pushed, then found the same") {
    const auto dir  = freshDir("jobs-files");
    const auto root = dir / "home";
    fs::create_directories(root);
    const auto here = dir / "here";
    writeFile(here / "books/UHO.epd", std::string(5000, 'e'));
    writeFile(here / "syzygy/KQvK.rtbw", std::string(80, 'w'));
    writeFile(here / "syzygy/KQvK.rtbz", std::string(144, 'z'));
    writeFile(here / "syzygy/KRvK.rtbw", std::string(80, 'r'));
    // On the machine already: one table the same, one cut short, and a book of the same name and size but another
    // content.
    writeFile(root / "fastchess-gui/syzygy/KQvK.rtbw", std::string(80, 'w'));
    writeFile(root / "fastchess-gui/syzygy/KRvK.rtbw", std::string(16, 'r'));
    writeFile(root / "fastchess-gui/books/UHO.epd", std::string(5000, 'f'));

    ssh::MockServer::Options options;
    options.root = root;
    options.exec = [root](const std::string& command) { return linuxBox(root, command); };
    ssh::MockServer server(machinePort(), options);
    Machine m;
    m.name         = "test-box";
    m.host         = "127.0.0.1";
    m.port         = machinePort();
    m.user         = "tester";
    m.system.shell = RemoteShell::Posix;
    FilesRequest request;
    request.books        = {here / "books/UHO.epd"};
    request.syzygy_paths = (here / "syzygy").string();

    SshClient client;
    REQUIRE(client.connect(targetOf(m, dir, "")));
    JobState state;
    JobResult checked;
    checkFiles(client, m, request, state, checked);
    REQUIRE_MESSAGE(checked.ok, checked.error.message);
    REQUIRE(checked.files);
    const auto& files = *checked.files;
    REQUIRE(files.books.size() == 1);
    CHECK(files.books[0].state == BookCheck::State::Different);  // same size, another hash
    CHECK(files.booksMissing());
    CHECK(files.syzygy_local == 3);
    CHECK(files.syzygy.same == 1);
    CHECK(files.syzygy.missing.size() == 1);
    CHECK(files.syzygy.different.size() == 1);
    CHECK(files.bytesToPush() == 5000 + 144 + 80);

    JobResult pushed;
    pushFiles(client, m, files, state, pushed);
    REQUIRE_MESSAGE(pushed.ok, pushed.error.message);
    CHECK(state.bytes_done == files.bytesToPush());
    CHECK(readFile(root / "fastchess-gui/books/UHO.epd") == std::string(5000, 'e'));
    CHECK(fs::file_size(root / "fastchess-gui/syzygy/KRvK.rtbw") == 80);

    JobResult again;
    checkFiles(client, m, request, state, again);
    REQUIRE(again.ok);
    CHECK_FALSE(again.files->booksMissing());
    CHECK(again.files->syzygy.complete());
    CHECK(again.files->bytesToPush() == 0);

    // A book missing on the machine, then pushed.
    writeFile(here / "books/new.bin", "polyglot");
    request.books = {here / "books/new.bin"};
    JobResult missing;
    checkFiles(client, m, request, state, missing);
    REQUIRE(missing.ok);
    CHECK(missing.files->books[0].state == BookCheck::State::Missing);
    CHECK(server.error().empty());
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// Tournaments on a machine

namespace {

tour::Participant participant(int id, const std::string& name, const std::string& cmd) {
    tour::Participant p;
    p.id          = id;
    p.engine.name = name;
    p.engine.cmd  = cmd;
    p.engine.dir  = "/home/me/engines";
    p.engine.options.push_back({"SyzygyPath", "/home/me/syzygy"});
    p.engine.options.push_back({"Hash", "64"});
    return p;
}

Machine linuxMachine() {
    Machine m;
    m.id             = "m1";
    m.name           = "test-box";
    m.host           = "127.0.0.1";
    m.port           = machinePort();
    m.user           = "tester";
    m.folder         = "fcg";
    m.system.shell   = RemoteShell::Posix;
    m.system.home    = "/home/tester";
    m.system.os      = "Linux";
    m.system.arch    = "x86_64";
    m.system.threads = 8;
    m.system.checked = "2026-11-15 10:00";
    return m;
}

}  // namespace

TEST_CASE("Machine runs: the tournament as the machine plays it, its engines, books and tablebases there") {
    const auto dir = freshDir("spec");
    tour::TournamentSpec spec;
    spec.name = "Gauntlet";
    spec.participants.push_back(participant(1, "Alpha 1.0", "/home/me/engines/alpha"));
    spec.participants.push_back(participant(2, "Beta (own book)", "/home/me/engines/beta"));
    spec.participants[1].book                   = "/home/me/books/beta.bin";
    spec.conditions.opening.file                = "/home/me/books/UHO.epd";
    spec.conditions.tb_adjudication.syzygy_dirs = "/home/me/syzygy";
    spec.forced_options                         = {{"SyzygyPath", "/home/me/syzygy"}};
    spec.pgn_out                                = "/home/me/all.pgn";
    spec.participants[0].program.size           = 1234;

    auto m           = linuxMachine();
    m.engines        = {{"Alpha 1.0", "/opt/alpha", -1, "", ""}};
    const auto there = remoteTournamentDir(m, dir);
    CHECK(machineFolder(m) == "/home/tester/fcg");
    CHECK(there == "/home/tester/fcg/tournaments/" + dir.filename().string());

    // Beta has no path there: named. Its library engine, by its program, has one: found.
    tour::TournamentSpec out;
    const auto error = remoteSpec(spec, m, {}, dir, there, out);
    CHECK(error.find("Beta (own book)") != std::string::npos);
    EngineEntry beta;
    beta.name = "Beta";
    beta.cmd  = "/home/me/engines/beta";
    m.engines.push_back({"Beta", "/opt/beta", -1, "", ""});
    REQUIRE(remoteSpec(spec, m, {beta}, dir, there, out).empty());

    CHECK(out.participants[0].engine.cmd == "/opt/alpha");
    CHECK(out.participants[0].engine.dir.empty());
    CHECK(out.participants[0].program.size == 0);
    CHECK(out.participants[0].engine.options[0].second == "/home/tester/fcg/syzygy");
    CHECK(out.participants[0].engine.options[1].second == "64");
    CHECK(out.participants[1].engine.cmd == "/opt/beta");
    CHECK(out.participants[1].book == "/home/tester/fcg/books/beta.bin");
    CHECK(out.conditions.opening.file == "/home/tester/fcg/books/UHO.epd");
    CHECK(out.conditions.tb_adjudication.syzygy_dirs == "/home/tester/fcg/syzygy");
    CHECK(out.forced_options[0].second == "/home/tester/fcg/syzygy");
    CHECK(out.pgn_out.empty());
    CHECK(requiredBooks(out, there) ==
          std::vector<std::string>{"/home/tester/fcg/books/UHO.epd", "/home/tester/fcg/books/beta.bin"});

    // A file of the tournament's folder (the positions of a Polyglot book) goes in its folder there, not required.
    spec.conditions.opening.file = (dir / "openings.epd").string();
    m.books_dir                  = "/data/books";
    REQUIRE(remoteSpec(spec, m, {beta}, dir, there, out).empty());
    CHECK(out.conditions.opening.file == there + "/openings.epd");
    CHECK(out.participants[1].book == "/data/books/beta.bin");
    CHECK(requiredBooks(out, there) == std::vector<std::string>{"/data/books/beta.bin"});

    // A withdrawn participant without a path does not keep the tournament from playing there.
    spec.participants.push_back(participant(3, "Gone", "/home/me/engines/gone"));
    spec.participants.back().active = false;
    CHECK(remoteSpec(spec, m, {beta}, dir, there, out).empty());
    fs::remove_all(dir);
}

TEST_CASE("Machine runs: the commands that start a tournament there and reach its process, for sh and cmd") {
    auto m = linuxMachine();
    CHECK(unSh(*remoteStartCommand(m, "/home/tester/fcg/tournaments/t")) ==
          "'/home/tester/fcg/fastchess-gui' --profile '/home/tester/fcg/profile' --run "
          "'/home/tester/fcg/tournaments/t' --detach");
    CHECK(unSh(*remoteAttachCommand(m, "/home/tester/fcg/tournaments/t")) ==
          "exec '/home/tester/fcg/fastchess-gui' --attach '/home/tester/fcg/tournaments/t'");
    m.system.shell = RemoteShell::Cmd;
    m.system.home  = "C:\\Users\\tester";
    CHECK(machineFolder(m) == "C:\\Users\\tester\\fcg");
    CHECK(*remoteAttachCommand(m, "C:\\Users\\tester\\fcg/tournaments/t") ==
          "\"C:\\Users\\tester\\fcg\\fastchess-gui.exe\" --attach \"C:\\Users\\tester\\fcg\\tournaments\\t\"");
    CHECK_FALSE(remoteStartCommand(m, "C:\\100%").has_value());
}

TEST_CASE("Machine runs: where a tournament plays, kept in its folder here") {
    const auto dir = freshDir("place");
    CHECK_FALSE(readMachinePlace(dir).has_value());
    REQUIRE(writeMachinePlace(dir, {"m1", "test-box", "/srv/fcg/tournaments/t"}));
    const auto place = readMachinePlace(dir);
    REQUIRE(place);
    CHECK(place->machine == "m1");
    CHECK(place->name == "test-box");
    CHECK(place->dir == "/srv/fcg/tournaments/t");
    fs::remove_all(dir);
}

TEST_CASE("Machine runs: prepared and started there, refused without fastchess-gui, an engine or a book") {
    const auto dir  = freshDir("start");
    const auto root = dir / "home";
    const auto here = dir / "here";
    fs::create_directories(here);
    writeFile(here / "tournament.json", "{}");
    writeFile(here / "openings.epd", "positions");
    writeFile(here / "tournament.lock", "1");
    writeFile(root / "opt/alpha", "binary");
    writeFile(root / "home/tester/fcg/fastchess-gui", "program");
    std::vector<std::string> commands;
    ssh::MockServer::Options options;
    options.root = root;
    options.exec = [&root](const std::string& command) {
        if (unSh(command).find("--detach") != std::string::npos) {
            ssh::MockServer::Command reply;
            reply.out = "plays in the background\n";
            return reply;
        }
        return linuxBox(root, command);
    };
    ssh::MockServer server(machinePort(), options);
    auto m = linuxMachine();

    tour::TournamentSpec spec;
    spec.participants.push_back(participant(1, "Alpha", "/opt/alpha"));
    spec.participants.push_back(participant(2, "Alpha too", "/opt/alpha"));
    spec.conditions.opening.file = "/home/tester/fcg/books/UHO.epd";
    const auto there             = std::string("/home/tester/fcg/tournaments/t");
    const auto json              = nlohmann::json(spec).dump();

    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    std::string error;
    std::vector<std::string> notes;
    // The book is not there yet.
    CHECK_FALSE(startOnMachine(client, m, here, there, json, error, notes));
    CHECK(error.find("UHO.epd") != std::string::npos);
    writeFile(root / "home/tester/fcg/books/UHO.epd", "book");
    error.clear();
    REQUIRE_MESSAGE(startOnMachine(client, m, here, there, json, error, notes), error);
    CHECK(readFile(root / "home/tester/fcg/tournaments/t/tournament.json") == json);
    CHECK(readFile(root / "home/tester/fcg/tournaments/t/openings.epd") == "positions");
    CHECK_FALSE(fs::exists(root / "home/tester/fcg/tournaments/t/tournament.lock"));  // this computer's
    CHECK_FALSE(fs::exists(here / ".tournament-remote.json"));
    const auto ran = server.commands();
    CHECK(std::any_of(ran.begin(), ran.end(),
                      [](const std::string& c) { return unSh(c).find("--detach") != std::string::npos; }));

    // An engine whose program is not there.
    spec.participants[1].engine.cmd = "/opt/missing";
    error.clear();
    CHECK_FALSE(startOnMachine(client, m, here, there, nlohmann::json(spec).dump(), error, notes));
    CHECK(error.find("Alpha too") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Machine runs: the tournament's files copied back as they grow, written afresh when shorter there") {
    const auto dir   = freshDir("sync");
    const auto root  = dir / "home";
    const auto local = dir / "here";
    fs::create_directories(local);
    const auto there = root / "fcg/tournaments/t";
    writeFile(there / "journal.jsonl", "{\"t\":\"created\"}\n");
    writeFile(there / "games.pgn", "[Event \"1\"]\n");
    writeFile(there / "logs/game-1.log", "W> uci\n");
    writeFile(there / "tournament.json", "{\"remote\":true}");
    writeFile(local / "tournament.json", "{\"local\":true}");
    ssh::MockServer::Options options;
    options.root = root;
    ssh::MockServer server(machinePort(), options);
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    std::string error;
    auto copied = MachineSync::pass(client, "/fcg/tournaments/t", local, error);
    REQUIRE_MESSAGE(copied, error);
    CHECK(readFile(local / "journal.jsonl") == "{\"t\":\"created\"}\n");
    CHECK(readFile(local / "games.pgn") == "[Event \"1\"]\n");
    CHECK(readFile(local / "logs/game-1.log") == "W> uci\n");
    CHECK(readFile(local / "tournament.json") == "{\"local\":true}");  // this computer's paths stay
    // Grown there: only the rest comes.
    std::ofstream(there / "journal.jsonl", std::ios::app | std::ios::binary) << "{\"t\":\"game\"}\n";
    copied = MachineSync::pass(client, "/fcg/tournaments/t", local, error);
    REQUIRE(copied);
    CHECK(*copied == std::string("{\"t\":\"game\"}\n").size());
    CHECK(readFile(local / "journal.jsonl") == "{\"t\":\"created\"}\n{\"t\":\"game\"}\n");
    // Nothing new: nothing copied.
    CHECK(*MachineSync::pass(client, "/fcg/tournaments/t", local, error) == 0);
    // Shorter there (written afresh): all of it.
    writeFile(there / "games.pgn", "[A]\n");
    REQUIRE(MachineSync::pass(client, "/fcg/tournaments/t", local, error));
    CHECK(readFile(local / "games.pgn") == "[A]\n");
    fs::remove_all(dir);
}

TEST_CASE("Machine runs: the control channel through SSH, and a process that is not there any more") {
    const auto dir = freshDir("link");
    ssh::MockServer::Options options;
    options.root = dir;
    options.exec = [](const std::string& command) {
        ssh::MockServer::Command reply;
        if (unSh(command).find("--attach 'playing'") != std::string::npos) {
            // fastchess-gui --attach: the tournament's hello once the window's came, then an echo of each line.
            reply.talk = [](const std::string& input, bool closed, bool& ended) {
                ended = closed;
                std::string out;
                if (input.find("hello") != std::string::npos) out += "{\"hello\":{\"protocol\":1}}\n";
                if (input.find("pause") != std::string::npos) out += "{\"e\":\"reply\"}\n";
                return out;
            };
        } else {
            reply.err    = "error: no tournament process plays gone (connection refused)\n";
            reply.status = 2;
        }
        return reply;
    };
    ssh::MockServer server(machinePort(), options);
    auto target = loopbackTarget(dir);
    auto status = std::make_shared<LinkStatus>();
    {
        SshLink link(target, viaSh("exec fcg --attach 'playing'"), nullptr, status, std::chrono::seconds(2));
        CHECK(link.write("{\"hello\":{\"protocol\":1,\"journal\":0}}\n"));
        std::string got;
        char buffer[256];
        while (got.find('\n') == std::string::npos) {
            const long n = link.read(buffer, sizeof buffer);
            REQUIRE(n > 0);
            got.append(buffer, static_cast<std::size_t>(n));
        }
        CHECK(got == "{\"hello\":{\"protocol\":1}}\n");
        CHECK(status->attached);
        CHECK(link.write("{\"c\":\"pause\"}\n"));
        got.clear();
        while (got.find('\n') == std::string::npos) {
            const long n = link.read(buffer, sizeof buffer);
            REQUIRE(n > 0);
            got.append(buffer, static_cast<std::size_t>(n));
        }
        CHECK(got == "{\"e\":\"reply\"}\n");
    }
    CHECK_FALSE(status->gone);

    // Nothing listens there: tried while it may be starting, then said gone.
    auto gone_status = std::make_shared<LinkStatus>();
    SshLink gone(target, viaSh("exec fcg --attach 'gone'"), nullptr, gone_status, std::chrono::seconds(1));
    char buffer[64];
    CHECK(gone.read(buffer, sizeof buffer) == 0);
    CHECK(gone_status->gone);
    CHECK(gone_status->message().find("does not play on test-box any more") != std::string::npos);
    fs::remove_all(dir);
}

// Against a real SSH server, when FCG_SSH_TEST names one: "user@host:port:key_file[:passphrase]". Off by default.
// ---------------------------------------------------------------------------------------------------------------
// Engines copied to a machine

namespace {

// The first bytes of programs, as their linkers write them.
std::string peHeader(uint16_t machine, std::size_t at = 0x80) {
    std::string h(at + 24, '\0');
    h[0] = 'M';
    h[1] = 'Z';
    for (int i = 0; i < 4; ++i) h[0x3c + i] = static_cast<char>((at >> (8 * i)) & 0xff);
    h.replace(at, 4, std::string("PE\0\0", 4));
    h[at + 4] = static_cast<char>(machine & 0xff);
    h[at + 5] = static_cast<char>(machine >> 8);
    return h;
}

std::string elfHeader(uint16_t machine, char osabi = 0) {
    std::string h(64, '\0');
    h.replace(0, 4,
              "\x7f"
              "ELF");
    h[4]  = 2;  // 64-bit
    h[5]  = 1;  // little-endian
    h[6]  = 1;
    h[7]  = osabi;
    h[16] = 2;  // an executable
    h[18] = static_cast<char>(machine & 0xff);
    h[19] = static_cast<char>(machine >> 8);
    return h;
}

std::string machoHeader(uint32_t cpu) {
    std::string h(32, '\0');
    const uint32_t magic = 0xfeedfacf;  // 64-bit, little-endian
    for (int i = 0; i < 4; ++i) {
        h[i]     = static_cast<char>((magic >> (8 * i)) & 0xff);
        h[4 + i] = static_cast<char>((cpu >> (8 * i)) & 0xff);
    }
    return h;
}

std::string fatHeader(const std::vector<uint32_t>& cpus) {
    std::string h;
    const auto be = [&](uint32_t v) {
        for (int i = 3; i >= 0; --i) h += static_cast<char>((v >> (8 * i)) & 0xff);
    };
    be(0xcafebabe);
    be(static_cast<uint32_t>(cpus.size()));
    for (const auto cpu : cpus) {
        be(cpu);
        be(3);
        be(4096);
        be(1000);
        be(12);
    }
    return h;
}

constexpr uint32_t CPU_X86_64 = 0x01000007, CPU_ARM64 = 0x0100000c;

RemoteSystem systemOf(const std::string& os, const std::string& arch) {
    RemoteSystem s;
    s.os    = os;
    s.arch  = arch;
    s.shell = os == "Windows" ? RemoteShell::Cmd : RemoteShell::Posix;
    return s;
}

}  // namespace

TEST_CASE("Machines: a program's system and architecture read from its first bytes") {
    auto f = programFormat(peHeader(0x8664));
    CHECK(f.kind == ProgramFormat::Kind::Windows);
    CHECK(f.os == "Windows");
    CHECK(f.archs == std::vector<std::string>{"x86_64"});
    CHECK(programFormat(peHeader(0xaa64)).archs == std::vector<std::string>{"arm64"});
    CHECK(programFormat(peHeader(0x14c)).archs == std::vector<std::string>{"x86"});
    // MZ without a PE header within the bytes read: Windows, its architecture not told.
    f = programFormat(peHeader(0x8664).substr(0, 0x60));
    CHECK(f.kind == ProgramFormat::Kind::Windows);
    CHECK(f.archs.empty());

    f = programFormat(elfHeader(62));
    CHECK(f.kind == ProgramFormat::Kind::Elf);
    CHECK(f.os == "Linux");
    CHECK(f.archs == std::vector<std::string>{"x86_64"});
    CHECK(programFormat(elfHeader(183)).archs == std::vector<std::string>{"arm64"});
    CHECK(programFormat(elfHeader(62, 9)).os == "FreeBSD");

    f = programFormat(machoHeader(CPU_X86_64));
    CHECK(f.kind == ProgramFormat::Kind::MachO);
    CHECK(f.os == "Darwin");
    CHECK(f.archs == std::vector<std::string>{"x86_64"});
    CHECK(programFormat(machoHeader(CPU_ARM64)).archs == std::vector<std::string>{"arm64"});
    f = programFormat(fatHeader({CPU_X86_64, CPU_ARM64}));
    CHECK(f.kind == ProgramFormat::Kind::MachO);
    CHECK(f.archs == std::vector<std::string>{"x86_64", "arm64"});
    CHECK(describeFormat(f) == "a macOS program (universal: x86_64, arm64)");
    // A Java class file begins as a universal program does; its version follows, not a count of architectures.
    CHECK(programFormat(std::string("\xca\xfe\xba\xbe\x00\x00\x00\x34", 8)).kind == ProgramFormat::Kind::Unknown);

    f = programFormat("#!/usr/bin/env python3\nprint('uciok')\n");
    CHECK(f.kind == ProgramFormat::Kind::Script);
    CHECK(f.interpreter == "python3");
    CHECK(f.shebang == "/usr/bin/env python3");
    CHECK(programFormat("#!/bin/sh\r\nexec ./engine\n").interpreter == "/bin/sh");
    CHECK(programFormat("#! /usr/bin/env -S PYTHONUNBUFFERED=1 python3 -u\n").interpreter == "python3");
    CHECK(programFormat("hello").kind == ProgramFormat::Kind::Unknown);
    CHECK(programFormat("").kind == ProgramFormat::Kind::Unknown);

    // A file, its PE header beyond the first 4 KiB read too.
    const auto dir = freshDir("format");
    writeFile(dir / "far.exe", peHeader(0xaa64, 6000) + std::string(100, 'x'));
    CHECK(readProgramFormat(dir / "far.exe").archs == std::vector<std::string>{"arm64"});
    CHECK(readProgramFormat(dir / "missing").kind == ProgramFormat::Kind::Unknown);
    fs::remove_all(dir);
}

TEST_CASE("Machines: a program suits the machine's system and architecture, or the build to choose is said") {
    using L         = Suitability::Level;
    const auto suit = [](const std::string& head, const RemoteSystem& s) {
        return programSuits(programFormat(head), s, "Alpha", "tc-box");
    };
    const auto linux_x64 = systemOf("Linux", "x86_64"), linux_arm = systemOf("Linux", "aarch64"),
               mac_arm = systemOf("Darwin", "arm64"), mac_x64 = systemOf("Darwin", "x86_64"),
               win_x64 = systemOf("Windows", "AMD64"), win_arm = systemOf("Windows", "ARM64");

    auto s = suit(peHeader(0x8664), linux_x64);
    CHECK(s.level == L::Refused);
    CHECK(s.message == "Alpha is a Windows program; tc-box runs Linux: choose the Linux build of Alpha.");
    CHECK(suit(elfHeader(62), linux_x64).level == L::Fits);
    s = suit(elfHeader(183), linux_x64);
    CHECK(s.level == L::Refused);
    CHECK(s.message == "Alpha is built for arm64; tc-box runs Linux x86_64: choose the Linux x86_64 build of Alpha.");
    CHECK(suit(elfHeader(183), linux_arm).level == L::Fits);
    CHECK(suit(elfHeader(62), mac_arm).level == L::Refused);
    CHECK(suit(machoHeader(CPU_ARM64), linux_arm).message.find("macOS program") != std::string::npos);
    CHECK(suit(machoHeader(CPU_ARM64), mac_arm).level == L::Fits);
    CHECK(suit(machoHeader(CPU_ARM64), mac_x64).level == L::Refused);
    // x86-64 on an arm64 Mac: Rosetta, if it is installed.
    s = suit(machoHeader(CPU_X86_64), mac_arm);
    CHECK(s.level == L::Warning);
    CHECK(s.message.find("Rosetta") != std::string::npos);
    // A universal program suits both Macs.
    CHECK(suit(fatHeader({CPU_X86_64, CPU_ARM64}), mac_arm).level == L::Fits);
    CHECK(suit(fatHeader({CPU_X86_64, CPU_ARM64}), mac_x64).level == L::Fits);
    CHECK(suit(peHeader(0x8664), win_x64).level == L::Fits);
    CHECK(suit(peHeader(0x14c), win_x64).level == L::Fits);  // 32-bit on 64-bit Windows
    CHECK(suit(peHeader(0x8664), win_arm).level == L::Warning);
    CHECK(suit(peHeader(0xaa64), win_x64).level == L::Refused);
    CHECK(suit(elfHeader(3), linux_x64).level == L::Warning);  // 32-bit: its libraries
    // Scripts: on a POSIX system only.
    CHECK(suit("#!/bin/sh\n", linux_x64).level == L::Fits);
    s = suit("#!/usr/bin/env python3\n", win_x64);
    CHECK(s.level == L::Refused);
    CHECK(s.message.find("choose the Windows build of Alpha") != std::string::npos);
    // A format not known is copied all the same, with a warning; an untested machine tells no system.
    CHECK(suit("binary", linux_x64).level == L::Warning);
    CHECK(suit(elfHeader(62), RemoteSystem{}).level == L::Refused);
}

TEST_CASE("Machines: the names Windows cannot hold, engines' folder names, paths inside a folder there") {
    CHECK(windowsNameProblem("nn-1234.nnue").empty());
    CHECK(windowsNameProblem("com10").empty());
    CHECK_FALSE(windowsNameProblem("a:b").empty());
    CHECK_FALSE(windowsNameProblem("what?").empty());
    CHECK_FALSE(windowsNameProblem("x|y").empty());
    CHECK_FALSE(windowsNameProblem("notes.").empty());
    CHECK_FALSE(windowsNameProblem("trailing ").empty());
    CHECK_FALSE(windowsNameProblem("CON").empty());
    CHECK_FALSE(windowsNameProblem("nul.txt").empty());
    CHECK_FALSE(windowsNameProblem("Com1").empty());
    CHECK(windowsNameProblem("aux.txt").find("aux.txt") != std::string::npos);

    CHECK(safeFolderName("Stockfish 17 (NNUE)") == "Stockfish 17 (NNUE)");
    CHECK(safeFolderName("A/B:C*") == "A_B_C_");
    CHECK(safeFolderName("CON") == "CON_");
    CHECK(safeFolderName("Engine. ") == "Engine");
    CHECK(safeFolderName("...") == "engine");

    CHECK(insideRemoteFolder("/home/u/fcg/engines/Alpha/alpha", "/home/u/fcg"));
    CHECK_FALSE(insideRemoteFolder("/home/u/fcg", "/home/u/fcg"));
    CHECK_FALSE(insideRemoteFolder("/home/u/fcg/../.ssh/authorized_keys", "/home/u/fcg"));
    CHECK_FALSE(insideRemoteFolder("/home/u/fcg-other/alpha", "/home/u/fcg"));
    CHECK_FALSE(insideRemoteFolder("/opt/alpha", "/home/u/fcg"));
    CHECK(insideRemoteFolder("c:\\users\\u\\FCG/engines/a.exe", "C:\\Users\\u\\fcg"));
    CHECK_FALSE(insideRemoteFolder("D:\\fcg\\a.exe", "C:\\fcg"));

    auto m = linuxMachine();
    CHECK(engineFolderThere(m, "Alpha 1.0") == "/home/tester/fcg/engines/Alpha 1.0");
    m.system      = systemOf("Windows", "AMD64");
    m.system.home = "C:\\Users\\tester";
    CHECK(engineFolderThere(m, "Alpha: dev") == "C:\\Users\\tester\\fcg\\engines\\Alpha_ dev");
}

TEST_CASE("Machines: an engine's copy planned here: its format, its folder counted, the options found in it") {
    const auto dir = freshDir("copy-plan");
    writeFile(dir / "alpha/alpha", elfHeader(62) + std::string(1000, 'a'));
    writeFile(dir / "alpha/nn-test.nnue", std::string(3000, 'n'));
    writeFile(dir / "alpha/books/own.bin", std::string(500, 'b'));
    writeFile(dir / "alpha/.git/HEAD", "ref: refs/heads/master\n");
    writeFile(dir / "windows/alpha.exe", peHeader(0x8664) + std::string(100, 'w'));
    EngineEntry alpha;
    alpha.name    = "Alpha";
    alpha.cmd     = (dir / "alpha/alpha").string();
    alpha.options = {{"EvalFile", (dir / "alpha/nn-test.nnue").string()}, {"Hash", "64"}};
    alpha.uci_options.push_back({"BookFile", "string", "books/own.bin", "", "", {}});

    auto m    = linuxMachine();
    auto plan = planEngineCopy(alpha, m);
    REQUIRE_MESSAGE(plan.error.empty(), plan.error);
    CHECK(plan.format.kind == ProgramFormat::Kind::Elf);
    CHECK(plan.suits.level == Suitability::Level::Fits);
    CHECK(plan.folder == dir / "alpha");
    CHECK(plan.files.size() == 3);  // its history left out
    CHECK(plan.folder_bytes == 1064 + 3000 + 500);
    CHECK(plan.bytes() == plan.folder_bytes);  // the program is one of them
    REQUIRE(plan.options_inside.size() == 2);
    CHECK(plan.options_inside[0] == std::pair<std::string, std::string>{"EvalFile", "nn-test.nnue"});
    CHECK(plan.options_inside[1] == std::pair<std::string, std::string>{"BookFile", "books/own.bin"});
    CHECK(plan.with_folder);  // it needs them
    CHECK(plan.remote_name == "alpha");

    // For a Windows machine: refused, until its Windows build is chosen, named with .exe there.
    m.system      = systemOf("Windows", "AMD64");
    m.system.home = "C:\\Users\\tester";
    plan          = planEngineCopy(alpha, m);
    CHECK(plan.suits.level == Suitability::Level::Refused);
    CHECK(plan.remote_name == "alpha.exe");
    plan = planEngineCopy(alpha, m, dir / "windows/alpha.exe");
    CHECK(plan.own_build);
    CHECK(plan.suits.level == Suitability::Level::Fits);
    CHECK(plan.folder == dir / "windows");
    CHECK(plan.remote_name == "alpha.exe");
#ifndef _WIN32
    // A name Windows cannot hold in the folder: said, naming the file (written where it can be).
    writeFile(dir / "windows/aux.txt", "x");
    plan = planEngineCopy(alpha, m, dir / "windows/alpha.exe");
    CHECK(plan.name_problem.find("aux.txt") != std::string::npos);
#endif

    // The program missing here.
    alpha.cmd = (dir / "gone").string();
    CHECK(planEngineCopy(alpha, linuxMachine()).error.find("not on this computer") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Machine jobs: an engine copied with its folder, each file's hash checked, its path set and checked") {
    const auto dir  = freshDir("copy");
    const auto root = dir / "root";
    fs::create_directories(root / "home/tester");
    writeFile(root / "opt/alpha", "the build set there by hand");
    const auto here = dir / "here";
    writeFile(here / "alpha/alpha", elfHeader(62) + std::string(70000, 'a'));
    writeFile(here / "alpha/nn-test.nnue", std::string(200000, 'n'));
    writeFile(here / "alpha/sub/notes.txt", "line one\r\nline two\n");  // as it is, its line ends too
    std::atomic_bool corrupt{false};
    ssh::MockServer::Options options;
    options.root = root;
    options.exec = [&root, &corrupt](const std::string& command) {
        const auto inner = unSh(command);
        ssh::MockServer::Command reply;
        if (inner.find("exec '/home/tester/fcg/engines/") != std::string::npos ||
            inner.find("exec '/opt/alpha'") != std::string::npos) {
            reply.talk = [](const std::string& input, bool closed, bool& ended) {
                std::string out;
                if (input.find("uci") != std::string::npos) out = "id name Alpha 2.0\nuciok\n";
                ended = closed || input.find("quit") != std::string::npos;
                return out;
            };
            return reply;
        }
        if (corrupt && inner.find("sha256sum") != std::string::npos) {
            reply.out = std::string(64, '0') + "  file\n";
            return reply;
        }
        return linuxBox(root, command);
    };
    ssh::MockServer server(machinePort(), options);

    EngineEntry alpha;
    alpha.name    = "Alpha";
    alpha.cmd     = (here / "alpha/alpha").string();
    alpha.options = {{"EvalFile", (here / "alpha/nn-test.nnue").string()}};
    auto m        = linuxMachine();
    MachineEngine by_hand;
    by_hand.engine   = "Alpha";
    by_hand.path     = "/opt/alpha";
    by_hand.check_ok = 1;
    m.engines        = {by_hand};
    const std::map<std::string, std::string> protocols{{"Alpha", "uci"}};
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    JobState state;

    // A different build there, at a path outside the machine's folder.
    JobResult compared;
    compareBuilds(client, m, {{"Alpha", here / "alpha/alpha"}}, state, compared);
    REQUIRE_MESSAGE(compared.ok, compared.error.message);
    REQUIRE(compared.engines.size() == 1);
    CHECK(buildMatch(compared.engines[0], fileStamp(here / "alpha/alpha")) == BuildMatch::Different);

    // Replaced with this computer's, with its folder: into the machine's folder, /opt/alpha left as it was.
    const auto plan = planEngineCopy(alpha, m);
    REQUIRE(plan.with_folder);
    JobResult copied;
    copyEngine(client, m, plan, protocols, state, copied);
    REQUIRE_MESSAGE(copied.ok, copied.error.message);
    const auto there = root / "home/tester/fcg/engines/Alpha";
    CHECK(readFile(there / "alpha") == readFile(here / "alpha/alpha"));
    CHECK(readFile(there / "nn-test.nnue") == readFile(here / "alpha/nn-test.nnue"));
    CHECK(readFile(there / "sub/notes.txt") == "line one\r\nline two\n");
    CHECK(readFile(root / "opt/alpha") == "the build set there by hand");
    CHECK_FALSE(fs::exists(there / "alpha.part"));
#ifndef _WIN32
    CHECK((fs::status(there / "alpha").permissions() & fs::perms::others_exec) != fs::perms::none);
#endif
    CHECK(state.bytes_done == plan.bytes());
    REQUIRE(copied.engines.size() == 1);
    auto e = copied.engines[0];
    CHECK(e.path == "/home/tester/fcg/engines/Alpha/alpha");
    CHECK(e.copied_dir == "/home/tester/fcg/engines/Alpha");
    CHECK(e.local_dir == (here / "alpha").string());
    CHECK(e.local_build.empty());
    CHECK(e.check_ok == 1);
    CHECK(e.id_name == "Alpha 2.0");
    CHECK(buildMatch(e, fileStamp(here / "alpha/alpha")) == BuildMatch::Same);
    const auto joined = [](const std::vector<std::string>& lines) {
        std::string out;
        for (const auto& l : lines) out += l + "\n";
        return out;
    };
    CHECK(joined(copied.lines).find("/opt/alpha, is left as it was") != std::string::npos);
    const auto ran = server.commands();
    CHECK(std::any_of(ran.begin(), ran.end(), [](const std::string& c) {
        return unSh(c) == "chmod 755 '/home/tester/fcg/engines/Alpha/alpha'";
    }));

    // Played there: its option names the network's copy, its folder is its working directory.
    m.engines = {e};
    tour::TournamentSpec spec, out;
    spec.participants.push_back(participant(1, "Alpha", alpha.cmd));
    spec.participants[0].engine.options.push_back({"EvalFile", (here / "alpha/nn-test.nnue").string()});
    spec.participants[0].engine.options.push_back({"Book", "/elsewhere/book.bin"});
    REQUIRE(remoteSpec(spec, m, {alpha}, dir, "/home/tester/fcg/tournaments/t", out).empty());
    CHECK(out.participants[0].engine.cmd == e.path);
    CHECK(out.participants[0].engine.dir == e.copied_dir);
    CHECK(out.participants[0].engine.options[2].second == "/home/tester/fcg/engines/Alpha/nn-test.nnue");
    CHECK(out.participants[0].engine.options[3].second == "/elsewhere/book.bin");

    // Copied again: the files there are the same, nothing is sent.
    const auto written = server.written.load();
    JobResult again;
    copyEngine(client, m, planEngineCopy(alpha, m), protocols, state, again);
    REQUIRE_MESSAGE(again.ok, again.error.message);
    CHECK(server.written == written);
    CHECK(joined(again.lines).find("there already") != std::string::npos);

    // Rebuilt here: changed since the comparison, then a different build. Its size changes too: a file's time has
    // whole seconds only with MinGW's library, the same time as the first build's here.
    writeFile(here / "alpha/alpha", elfHeader(62) + std::string(70001, 'b'));
    CHECK(buildMatch(e, fileStamp(here / "alpha/alpha")) == BuildMatch::Changed);
    JobResult rebuilt;
    compareBuilds(client, m, {{"Alpha", here / "alpha/alpha"}}, state, rebuilt);
    REQUIRE(rebuilt.ok);
    CHECK(buildMatch(rebuilt.engines[0], fileStamp(here / "alpha/alpha")) == BuildMatch::Different);

    // A copy that is not the same there: removed, said.
    corrupt = true;
    JobResult bad;
    auto alone        = planEngineCopy(alpha, m);
    alone.with_folder = false;
    copyEngine(client, m, alone, protocols, state, bad);
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.message.find("not the same as here") != std::string::npos);
    CHECK(bad.engines.empty());
    CHECK_FALSE(fs::exists(there / "alpha"));
    corrupt = false;

    // A Windows build for this Linux box: refused before anything is sent.
    writeFile(here / "win/alpha.exe", peHeader(0x8664) + std::string(500, 'w'));
    const auto before = server.written.load();
    JobResult refused;
    copyEngine(client, m, planEngineCopy(alpha, m, here / "win/alpha.exe"), protocols, state, refused);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.message == "Alpha is a Windows program; test-box runs Linux: choose the Linux build of Alpha.");
    CHECK(server.written == before);

    // A script: its interpreter must be there.
    writeFile(here / "script/alpha.py", "#!/usr/bin/env nosuchpython\nprint('uciok')\n");
    JobResult script;
    copyEngine(client, m, planEngineCopy(alpha, m, here / "script/alpha.py"), protocols, state, script);
    CHECK_FALSE(script.ok);
    CHECK(script.error.message.find("nosuchpython, which test-box does not have") != std::string::npos);
    writeFile(here / "script/alpha.py", "#!/usr/bin/env python3\nprint('uciok')\n");
    JobResult python;
    copyEngine(client, m, planEngineCopy(alpha, m, here / "script/alpha.py"), protocols, state, python);
    REQUIRE_MESSAGE(python.ok, python.error.message);
    CHECK(python.engines[0].local_build == (here / "script/alpha.py").string());
    CHECK(server.error().empty());
    fs::remove_all(dir);
}

TEST_CASE("Machine jobs: an engine's copy cut by the network resumes; a Windows machine refuses names it cannot hold") {
    const auto dir  = freshDir("copy-cut");
    const auto root = dir / "root";
    fs::create_directories(root / "home/tester");
    const auto here    = dir / "here";
    const auto content = elfHeader(62) + std::string(400000, 'c');
    writeFile(here / "alpha/alpha", content);
    ssh::MockServer::Options options;
    options.root      = root;
    options.cut_after = 250000;
    options.exec      = [&root](const std::string& command) {
        if (unSh(command).find("exec '/home/tester/fcg/engines/") != std::string::npos) {
            ssh::MockServer::Command reply;
            reply.talk = [](const std::string& input, bool closed, bool& ended) {
                ended = closed || input.find("quit") != std::string::npos;
                return input.find("uci") != std::string::npos ? std::string("uciok\n") : std::string();
            };
            return reply;
        }
        return linuxBox(root, command);
    };
    ssh::MockServer server(machinePort(), options);
    EngineEntry alpha;
    alpha.name = "Alpha";
    alpha.cmd  = (here / "alpha/alpha").string();
    auto m     = linuxMachine();
    {
        SshClient client;
        REQUIRE(client.connect(loopbackTarget(dir)));
        JobState state;
        JobResult cut;
        copyEngine(client, m, planEngineCopy(alpha, m), {}, state, cut);
        CHECK_FALSE(cut.ok);
        CHECK(cut.engines.empty());
    }
    CHECK(fs::exists(root / "home/tester/fcg/engines/Alpha/alpha.part"));
    {
        SshClient client;
        REQUIRE(client.connect(loopbackTarget(dir)));
        JobState state;
        JobResult resumed;
        copyEngine(client, m, planEngineCopy(alpha, m), {}, state, resumed);
        REQUIRE_MESSAGE(resumed.ok, resumed.error.message);
        CHECK(state.bytes_sent < content.size());  // from where the cut left it
    }
    CHECK(readFile(root / "home/tester/fcg/engines/Alpha/alpha") == content);

#ifndef _WIN32
    // A Windows machine: a folder with a name Windows cannot hold is refused, naming it, before anything is sent
    // (such a file written where it can be).
    writeFile(here / "win/alpha.exe", peHeader(0x8664) + std::string(500, 'w'));
    writeFile(here / "win/net:v2.nnue", "n");
    m.system         = systemOf("Windows", "AMD64");
    m.system.home    = "C:\\Users\\tester";
    auto plan        = planEngineCopy(alpha, m, here / "win/alpha.exe");
    plan.with_folder = true;
    SshClient client;
    REQUIRE(client.connect(loopbackTarget(dir)));
    JobState state;
    JobResult refused;
    const auto before = server.written.load();
    copyEngine(client, m, plan, {}, state, refused);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.message.find("net:v2.nnue") != std::string::npos);
    CHECK(server.written == before);
#endif
    fs::remove_all(dir);
}

TEST_CASE("Machines: an engine's copy and builds kept in machines.json, its local paths portable") {
    Machine m;
    m.id   = "m1";
    m.name = "box";
    MachineEngine e;
    e.engine            = "Alpha";
    e.path              = "/srv/fcg/engines/Alpha/alpha";
    e.local_build       = "/home/me/builds/alpha-linux";
    e.local_dir         = "/home/me/builds";
    e.copied_dir        = "/srv/fcg/engines/Alpha";
    e.remote_hash       = std::string(64, 'a');
    e.local_hash        = std::string(64, 'a');
    e.local_stamp.size  = 1234;
    e.local_stamp.mtime = 5678;
    m.engines.push_back(e);
    const auto back = nlohmann::json(m).get<Machine>();
    REQUIRE(back.engines.size() == 1);
    CHECK(back.engines[0] == e);
    CHECK(buildMatch(back.engines[0], e.local_stamp) == BuildMatch::Same);
}

TEST_CASE("SSH client: a real server, when FCG_SSH_TEST names one") {
    const char* spec = std::getenv("FCG_SSH_TEST");
    if (!spec || !*spec) return;
    std::string s = spec;
    const auto at = s.find('@');
    REQUIRE(at != std::string::npos);
    std::vector<std::string> parts;
    std::size_t start = at + 1;
    while (true) {
        const auto colon = s.find(':', start);
        parts.push_back(s.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    REQUIRE(parts.size() >= 3);
    const auto dir = freshDir("real");
    Machine m;
    m.name                = "real";
    m.user                = s.substr(0, at);
    m.host                = parts[0];
    m.port                = std::stoi(parts[1]);
    m.auth                = parts[2] == "agent" ? SshAuth::Agent : SshAuth::KeyFile;
    m.key_file            = parts[2];
    m.folder              = (dir / "remote").string();
    const auto passphrase = parts.size() > 3 ? parts[3] : std::string();
    SshClient client;
    {
        // As the window runs it: a job on a thread of its own.
        MachineJob job(MachineJob::Kind::Test, targetOf(m, dir, passphrase),
                       [&m](SshClient& c, JobState& s, JobResult& r) { testMachine(c, m, s, r); });
        for (int i = 0; i < 1000 && !job.finished(); ++i) std::this_thread::sleep_for(20ms);
        REQUIRE(job.finished());
        const auto result = job.take();
        REQUIRE_MESSAGE(result.ok, result.error.message);
    }
    REQUIRE_MESSAGE(client.connect(targetOf(m, dir, passphrase)), client.error().message);
    JobState state;
    JobResult result;
    testMachine(client, m, state, result);
    REQUIRE_MESSAGE(result.ok, result.error.message);
    for (const auto& line : result.lines) MESSAGE(line);
    m.system = *result.system;
    writeFile(dir / "data.bin", std::string(5 << 20, 'd'));
    REQUIRE_MESSAGE(client.makeDirs(m.folder), client.error().message);
    REQUIRE_MESSAGE(client.upload(dir / "data.bin", remoteJoin(m.folder, "data.bin")), client.error().message);
    CHECK(fs::file_size(dir / "remote/data.bin") == 5u << 20);
    const auto hash = client.run(*hashCommand(m.system.shell, remoteJoin(m.folder, "data.bin")), 30s);
    REQUIRE(hash);
    CHECK(readHash(hash->out) == sha256File(dir / "data.bin"));
    fs::remove_all(dir);
}

// A real machine from end to end, when FCG_MACHINE_TEST names one: "user@host:port:key_file", with FCG_MACHINE_PROGRAM
// the fastchess-gui built for its system and FCG_MACHINE_ENGINE the mock engine built for it (those of the Windows
// build, from Linux). As the window does it: the machine tested, fastchess-gui installed there, the engine copied
// there by Copy to the machine and found the same build, a match started there in the background and followed, the
// window gone and back, the files copied back.
// Off by default; its folder there is left for a look (fcg-e2e-<time> in the user's home folder).
TEST_CASE("Machines: a real machine from end to end, when FCG_MACHINE_TEST names one") {
    const char* spec    = std::getenv("FCG_MACHINE_TEST");
    const char* program = std::getenv("FCG_MACHINE_PROGRAM");
    const char* mock    = std::getenv("FCG_MACHINE_ENGINE");
    if (!spec || !*spec || !program || !mock) return;
    const std::string s = spec;
    const auto at       = s.find('@');
    const auto colon1   = s.find(':', at);
    const auto colon2   = s.find(':', colon1 + 1);
    REQUIRE((at != std::string::npos && colon1 != std::string::npos && colon2 != std::string::npos));

    const auto dir = freshDir("e2e");
    Machine m;
    m.id       = "e2e";
    m.name     = "e2e-box";
    m.user     = s.substr(0, at);
    m.host     = s.substr(at + 1, colon1 - at - 1);
    m.port     = std::stoi(s.substr(colon1 + 1, colon2 - colon1 - 1));
    m.auth     = SshAuth::KeyFile;
    m.key_file = s.substr(colon2 + 1);
    m.folder   = "fcg-e2e-" + std::to_string(std::time(nullptr));
    m.program  = program;
    const auto target = targetOf(m, dir, "");

    SshClient client;
    REQUIRE_MESSAGE(client.connect(target), client.error().message);
    JobState state;
    JobResult result;
    testMachine(client, m, state, result);
    REQUIRE_MESSAGE(result.ok, result.error.message);
    for (const auto& line : result.lines) MESSAGE(line);
    m.system = *result.system;

    result = {};
    installProgram(client, m, program, state, result);
    REQUIRE_MESSAGE(result.ok, result.error.message);

    // The mock engine there, through Copy to the machine: its format checked against the machine's system, copied to
    // the engines folder, its hash compared, its path set and checked.
    std::vector<EngineEntry> library(2);
    library[0].name = "Alpha";
    library[1].name = "Bravo";
    for (auto& e : library) e.cmd = mock;
    for (const auto& e : library) {
        const auto plan = planEngineCopy(e, m);
        REQUIRE_MESSAGE(plan.error.empty(), plan.error);
        REQUIRE_MESSAGE(plan.suits.level != Suitability::Level::Refused, plan.suits.message);
        result = {};
        copyEngine(client, m, plan, {}, state, result);
        for (const auto& line : result.lines) MESSAGE(line);
        REQUIRE_MESSAGE(result.ok, result.error.message);
        REQUIRE(result.engines.size() == 1);
        CHECK(insideRemoteFolder(result.engines[0].path, machineFolder(m)));
        m.engines.push_back(result.engines[0]);
    }
    // The same build there, as the engines' check finds it.
    result = {};
    checkEngines(client, m, {}, std::chrono::seconds(20), state, result, {{"Alpha", mock}, {"Bravo", mock}});
    REQUIRE_MESSAGE(result.ok, result.error.message);
    for (const auto& e : result.engines) CHECK(buildMatch(e, fileStamp(mock)) == BuildMatch::Same);
    // A build for another system is refused before anything is sent.
    {
        const bool windows = m.system.shell == RemoteShell::Cmd;
        writeFile(dir / "other" / "alpha", windows ? std::string("\x7f"
                                                                 "ELF\x02\x01\x01",
                                                                 7) +
                                                         std::string(100, '\0')
                                                   : std::string("MZ") + std::string(100, '\0'));
        const auto plan = planEngineCopy(library[0], m, dir / "other" / "alpha");
        CHECK(plan.suits.level == Suitability::Level::Refused);
        result = {};
        copyEngine(client, m, plan, {}, state, result);
        CHECK_FALSE(result.ok);
        MESSAGE(result.error.message);
        CHECK(result.error.message.find("choose the") != std::string::npos);
    }
    // An engine with its folder: a network in a folder beside the program, an option naming it, found in its copy.
    {
        const auto folder = dir / "charlie";
        const auto exe    = folder / fs::path(mock).filename();
        fs::create_directories(folder / "nets");
        fs::copy_file(mock, exe);
        writeFile(folder / "nets" / "charlie.nnue", std::string(300000, 'n'));
        EngineEntry charlie;
        charlie.name    = "Charlie";
        charlie.cmd     = exe.string();
        charlie.options = {{"EvalFile", (folder / "nets" / "charlie.nnue").string()}};
        const auto plan = planEngineCopy(charlie, m);
        REQUIRE(plan.with_folder);
        CHECK(plan.files.size() == 2);
        result = {};
        copyEngine(client, m, plan, {}, state, result);
        for (const auto& line : result.lines) MESSAGE(line);
        REQUIRE_MESSAGE(result.ok, result.error.message);
        const auto& e = result.engines[0];
        CHECK(e.copied_dir == engineFolderThere(m, "Charlie"));
        const auto net = optionThere(e, m.system.shell, charlie.options[0].second);
        MESSAGE("EvalFile there: " << net);
        CHECK(net.rfind(e.copied_dir, 0) == 0);
        const auto hash = client.run(*hashCommand(m.system.shell, net), 30s);
        REQUIRE(hash);
        CHECK(readHash(hash->out) == sha256File(folder / "nets" / "charlie.nnue"));
    }

    // A match of this computer, played there.
    MatchSetup setup;
    setup.format      = TournamentFormat::Match;
    setup.name        = "e2e";
    setup.engine_a    = "Alpha";
    setup.engine_b    = "Bravo";
    setup.mode        = MatchMode::Fixed;
    setup.fixed_games = 16;
    setup.limit       = LimitKind::Nodes;
    setup.nodes       = 1000;
    setup.concurrency = 2;
    setup.recover     = true;
    const auto local_dir = dir / "runs" / "e2e";
    const auto local     = buildRun(setup, library, topologyFromCores({{0, 0}, {0, 1}}), local_dir).spec;
    (void)tour::TournamentState::create(local_dir, local);
    const auto remote_dir = remoteTournamentDir(m, local_dir);
    tour::TournamentSpec there;
    const auto spec_error = remoteSpec(local, m, library, local_dir, remote_dir, there);
    REQUIRE_MESSAGE(spec_error.empty(), spec_error);
    const auto spec_json = nlohmann::json(there).dump() + "\n";
    const auto attach    = remoteAttachCommand(m, remote_dir);
    REQUIRE(attach);

    // Follows the match until `games` are in the copy here (the journal copied back), or it ended.
    const auto follow = [&](RemoteRun& run, MachineSync& sync, std::size_t games, std::chrono::seconds limit) {
        const auto until = std::chrono::steady_clock::now() + limit;
        auto next_pass   = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < until) {
            run.update();
            if (std::chrono::steady_clock::now() >= next_pass) {
                sync.now();
                next_pass = std::chrono::steady_clock::now() + 1s;
            }
            const auto* t = run.tournament();
            if (t && t->results().size() >= games) return true;
            std::this_thread::sleep_for(50ms);
        }
        return false;
    };
    {
        auto status              = std::make_shared<LinkStatus>();
        SshLink::Prepare prepare = [m, local_dir, remote_dir, spec_json](SshClient& c, std::string& error,
                                                                         std::vector<std::string>& notes) {
            return startOnMachine(c, m, local_dir, remote_dir, spec_json, error, notes);
        };
        RemoteRun run;
        const auto error = run.attach(local_dir, std::make_unique<SshLink>(target, *attach, prepare, status, 30s));
        REQUIRE_MESSAGE(error.empty(), error);
        MachineSync sync(target, remote_dir, local_dir);
        REQUIRE_MESSAGE(follow(run, sync, 4, 300s), status->message());
        MESSAGE("started there, 4 games copied back");
    }  // the window goes away: the match plays on there

    std::this_thread::sleep_for(3s);
    {
        auto status = std::make_shared<LinkStatus>();
        RemoteRun run;
        const auto error = run.attach(local_dir, std::make_unique<SshLink>(target, *attach, nullptr, status, 10s));
        REQUIRE_MESSAGE(error.empty(), error);
        MachineSync sync(target, remote_dir, local_dir);
        REQUIRE_MESSAGE(follow(run, sync, 16, 600s), status->message());
        // Its process ends there once the match is over: the files copied back a last time after.
        const auto until = std::chrono::steady_clock::now() + 120s;
        while (run.running() && std::chrono::steady_clock::now() < until) {
            run.update();
            std::this_thread::sleep_for(50ms);
        }
        CHECK_FALSE(run.running());
        sync.finish();
        for (int i = 0; i < 600 && !sync.finished(); ++i) std::this_thread::sleep_for(50ms);
        CHECK_MESSAGE(sync.error().empty(), sync.error());
    }
    const auto copy = tour::TournamentState::open(local_dir, true);
    std::size_t counted = 0;
    for (const auto& r : copy->results()) counted += r.voided ? 0 : 1;
    CHECK(counted == 16);
    CHECK(copy->finished());
    fs::remove_all(dir);
}

