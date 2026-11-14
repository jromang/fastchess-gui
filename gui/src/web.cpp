// The broadcast web page, read-only. The followed game at its centre: the board with the last move, a king in check,
// the coordinates and the arrows of the search; large clocks; each engine's evaluation in large type and its search
// as tiles, with who is thinking; the moves, the book's told apart, to step through; the board turned, the game's PGN
// and the position's FEN copied. Every game in progress beside it; the standings and the crosstable, the charts of
// the followed game, each engine's search, the next games, the statistics and anomalies and the event in tabs. The
// moves in letters or figurines, a light or dark theme, and the PGN file to download. Where the viewers talk, when
// the setup says: a chat link and the lichess round in its header, Discord's widget beside the moves.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

#include "broadcast.hpp"
#include "zip.hpp"

#include <core/crc32.hpp>

namespace fcg {

namespace assets {
extern const std::array<std::string_view, 12> piece_svgs;
}

namespace {

using Clock = std::chrono::steady_clock;

// Requests served at once, downloads included: more are told to come back later. Each request is read within a
// second and answered within ten, so that a client that sends or reads slowly holds a thread a moment only.
constexpr std::size_t MAX_CONNECTIONS = 32;
constexpr std::size_t MAX_REQUEST     = 8192;  // bytes of request line and headers
constexpr int READ_MS                 = 1000;
constexpr int REQUEST_MS              = 10000;
// Downloads at once: more are told to come back later.
constexpr int MAX_DOWNLOADS = 2;
// A download sends 64 KiB at a time, each taken within 10 seconds (6.4 KB/s at least), for 30 minutes at most.
constexpr std::size_t CHUNK      = 64 * 1024;
constexpr int CHUNK_MS           = 10000;
constexpr auto MAX_DOWNLOAD_TIME = std::chrono::minutes(30);

// Every answer: its type taken as given, no address sent on from the page; never kept by a cache or a proxy (a
// tunnel) unless it says otherwise (the page, checked again at each load).
constexpr const char* COMMON_HEADERS =
    "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n";

// The page loads nothing from elsewhere and sends nothing elsewhere: its own script and styles, and state.json.
constexpr const char* PAGE_POLICY =
    "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
    "connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'\r\n";
// The same with Discord's widget: a frame from discord.com, nothing else.
constexpr const char* DISCORD_POLICY =
    "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
    "connect-src 'self'; img-src 'self' data:; frame-src https://discord.com; base-uri 'none'; "
    "form-action 'none'\r\n";

std::string response(const std::string& status, const std::string& type, const std::string& body,
                     const std::string& headers = "Access-Control-Allow-Origin: *\r\n") {
    const auto cache = headers.find("Cache-Control:") == std::string::npos ? "Cache-Control: no-store\r\n" : "";
    return "HTTP/1.0 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n" + headers + cache + COMMON_HEADERS + "\r\n" + body;
}

// The value of a header of the request (its name in lower case), empty when it has none.
std::string_view headerValue(std::string_view request, std::string_view name) {
    for (std::size_t at = request.find("\r\n"); at != std::string_view::npos;) {
        const auto start = at + 2;
        const auto end   = request.find("\r\n", start);
        const auto line  = request.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.size() > name.size() && line[name.size()] == ':' &&
            std::equal(name.begin(), name.end(), line.begin(),
                       [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); })) {
            auto value = line.substr(name.size() + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            return value;
        }
        at = end;
    }
    return {};
}

// Whether the client takes gzip: every browser says so. A "gzip;q=0" refuses it.
bool acceptsGzip(std::string_view request) {
    std::string value(headerValue(request, "accept-encoding"));
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::erase(value, ' ');
    const auto at = value.find("gzip");
    return at != std::string::npos && value.compare(at + 4, 4, ";q=0") != 0 && value.compare(at + 4, 6, ";q=0.0") != 0;
}

// The page, the same until the program changes: sent compressed when the client takes it, and checked again at each
// load by its tag, so that a reload costs a few bytes. Two of them, the plain one and the one with the extras.
struct Page {
    std::string gzip;
    std::string tag;  // its CRC, without the quotes
};

const Page& page(bool extras) {
    static const auto make = [](bool with) {
        const auto& html = webPage(with);
        char tag[16];
        std::snprintf(tag, sizeof(tag), "%08x",
                      static_cast<unsigned>(fastchess::crc::finalize_crc32(
                          fastchess::crc::incremental_crc32(fastchess::crc::initial_crc32(), html))));
        return Page{gzipped(html, 64), tag};  // once: the best stb finds
    };
    static const Page plain = make(false), with = make(true);
    return extras ? with : plain;
}

// The file name a browser saves the download as: plain ASCII for the old ones, UTF-8 for the others (RFC 6266).
std::string disposition(const std::string& name) {
    std::string ascii, encoded;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        ascii += u < 0x20 || u >= 0x7F || c == '"' || c == '\\' ? '_' : c;
        if (std::isalnum(u) || c == '.' || c == '-' || c == '_') {
            encoded += c;
        } else {
            static constexpr char hex[] = "0123456789ABCDEF";
            encoded += '%';
            encoded += hex[u >> 4];
            encoded += hex[u & 15];
        }
    }
    return "attachment; filename=\"" + ascii + "\"; filename*=UTF-8''" + encoded;
}

}  // namespace

WebServer::~WebServer() { stop(); }

std::string WebServer::start(uint16_t port) {
    stop();
    if (auto error = listener_.listen(port); !error.empty()) return error;
    running_ = true;
    thread_  = std::thread([this] { serve(); });
    return {};
}

void WebServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    // The requests and downloads see running_ and end at once.
    for (auto& w : workers_) {
        if (w.thread.joinable()) w.thread.join();
    }
    workers_.clear();
    listener_.close();
    viewers_ = 0;
}

void WebServer::publish(const BroadcastSnapshot& snapshot) {
    auto json = snapshotJson(snapshot);
    // Compressed once for every viewer: a fifth of its size, on a tunnel that limits its speed.
    auto gzip = gzipped(json);
    std::lock_guard<std::mutex> lock(mutex_);
    state_json_ = std::move(json);
    state_gzip_ = std::move(gzip);
    pgn_file_   = snapshot.pgn_file;
    pgn_name_   = snapshot.pgn_name;
    pgn_bytes_  = snapshot.pgn_bytes;
}

void WebServer::setExtras(bool links, bool discord) {
    links_   = links;
    discord_ = discord;
}

void WebServer::serve() {
    while (running_) {
        // The requests that ended.
        for (auto it = workers_.begin(); it != workers_.end();) {
            if (*it->done) {
                it->thread.join();
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }

        const auto fd = listener_.accept(200);
        if (!fd) continue;
        auto connection = std::make_unique<net::TcpConnection>(*fd, &running_);

        // Each request on a thread of its own, so that a slow client holds no other; too many at once are told to
        // come back, their request read first (a moment at most) so that they get the answer.
        if (workers_.size() >= MAX_CONNECTIONS) {
            (void)connection->readUntil("\r\n\r\n", MAX_REQUEST, 100);
            connection->sendAll(response("503 Service Unavailable", "text/plain", "busy, try again in a moment"), 100);
            continue;
        }
        auto done = std::make_shared<std::atomic_bool>(false);
        std::thread thread([this, c = std::move(connection), done]() mutable {
            handle(std::move(c));
            *done = true;
        });
        workers_.push_back({std::move(thread), std::move(done)});
    }
}

void WebServer::handle(std::unique_ptr<net::TcpConnection> connection) {
    const auto start   = Clock::now();
    const auto request = connection->readUntil("\r\n\r\n", MAX_REQUEST, READ_MS);
    const auto left    = [&] {
        return REQUEST_MS -
               static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    };
    const auto send = [&](const std::string& answer) { connection->sendAll(answer, std::max(0, left())); };

    // The request line: a method, a path, the query left out. Only what the page needs is served, from memory or
    // from the tournament's PGN file, never a file named by the request. Behind a proxy or a tunnel every client
    // comes from the same address: nothing here depends on it.
    const auto line   = std::string_view(request).substr(0, request.find_first_of("\r\n"));
    const auto space  = line.find(' ');
    const auto method = line.substr(0, space);
    const auto target = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
    const auto path   = target.substr(0, target.find_first_of(" ?#"));
    // A request larger than any the page makes is not one of its.
    if (request.size() >= MAX_REQUEST && request.find("\r\n\r\n") == std::string::npos) {
        send(response("431 Request Header Fields Too Large", "text/plain", "request too large"));
        return;
    }
    if (path.empty() || path[0] != '/') {
        if (!request.empty()) send(response("400 Bad Request", "text/plain", "bad request"));
        return;
    }
    if (method != "GET") {
        send(response("405 Method Not Allowed", "text/plain", "only GET", "Allow: GET\r\n"));
        return;
    }

    const bool gzip = acceptsGzip(request);
    if (path == "/" || path == "/index.html") {
        // With Discord's widget the policy differs, and so does the tag: a browser that kept the page without it
        // takes it again, with the policy that lets the widget in (and the other way round).
        const bool discord = discord_;
        const bool extras  = links_ || discord;
        const auto& p      = page(extras);
        const auto tag     = "\"" + p.tag + (discord ? "-d" : "") + "\"";
        const auto headers = std::string(discord ? DISCORD_POLICY : PAGE_POLICY) +
                             "Cache-Control: no-cache\r\nETag: " + tag + "\r\nVary: Accept-Encoding\r\n";
        if (headerValue(request, "if-none-match") == tag) {
            send("HTTP/1.0 304 Not Modified\r\n" + headers + COMMON_HEADERS + "\r\n");
        } else if (gzip && !p.gzip.empty()) {
            send(response("200 OK", "text/html; charset=utf-8", p.gzip, headers + "Content-Encoding: gzip\r\n"));
        } else {
            send(response("200 OK", "text/html; charset=utf-8", webPage(extras), headers));
        }
    } else if (path == "/state.json") {
        std::string body;
        bool zipped = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            zipped = gzip && !state_gzip_.empty();
            body   = zipped ? state_gzip_ : state_json_;
            // A viewer polls every second: the requests of the last few seconds count the viewers, whatever their
            // address (all the same behind a tunnel).
            const auto now = Clock::now();
            recent_.push_back(now);
            std::erase_if(recent_, [&](const auto& t) { return now - t > std::chrono::seconds(3); });
            viewers_ = static_cast<int>((recent_.size() + 2) / 3);
        }
        send(response("200 OK", "application/json", body,
                      std::string("Access-Control-Allow-Origin: *\r\nVary: Accept-Encoding\r\n") +
                          (zipped ? "Content-Encoding: gzip\r\n" : "")));
    } else if (path == "/games.pgn") {
        std::filesystem::path file;
        std::string name;
        uint64_t bytes = 0;
        bool busy      = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            file  = pgn_file_;
            name  = pgn_name_;
            bytes = pgn_bytes_;
            busy  = downloads_ >= MAX_DOWNLOADS;
            if (!file.empty() && !busy) ++downloads_;
        }
        if (file.empty()) {
            send(response("404 Not Found", "text/plain", "no game played yet"));
        } else if (busy) {
            send(response("503 Service Unavailable", "text/plain", "busy, try again in a moment"));
        } else {
            download(*connection, file, name, bytes);
            std::lock_guard<std::mutex> lock(mutex_);
            --downloads_;
        }
    } else {
        send(response("404 Not Found", "text/plain", "not found"));
    }
}

void WebServer::download(net::TcpConnection& connection, const std::filesystem::path& file, const std::string& name,
                         uint64_t bytes) {
    // The games recorded, read-only: up to the end of the last one, not into one being written.
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    std::ifstream in(file, std::ios::binary);
    if (ec || !in) {
        connection.sendAll(response("404 Not Found", "text/plain", "no PGN file"), CHUNK_MS);
        return;
    }
    bytes = std::min<uint64_t>(bytes, size);
    const auto head =
        "HTTP/1.0 200 OK\r\nContent-Type: application/x-chess-pgn\r\nContent-Length: " + std::to_string(bytes) +
        "\r\nContent-Disposition: " + disposition(name) + "\r\n" + COMMON_HEADERS + "\r\n";
    if (!connection.sendAll(head, CHUNK_MS)) return;

    const auto start = Clock::now();
    std::vector<char> buffer(CHUNK);
    while (bytes > 0 && running_ && Clock::now() - start < MAX_DOWNLOAD_TIME) {
        in.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(bytes, buffer.size())));
        const auto n = in.gcount();
        if (n <= 0) break;
        // A client that does not take a chunk in time is let go.
        if (!connection.sendAll(std::string_view(buffer.data(), static_cast<std::size_t>(n)), CHUNK_MS)) break;
        bytes -= static_cast<uint64_t>(n);
    }
}

namespace {

// The colours of the dark theme, chosen by the viewer's system or by the page's switch.
constexpr const char* DARK_COLOURS =
    "color-scheme:dark;--bg:#121418;--card:#1a1d23;--text:#e8eaee;--muted:#a3aab6;--line:#2a2f38;--soft:#23272f;"
    "--accent:#5b9dff;--win:#5fd39a;--loss:#ff8f87;--book:#d9b85c";

// The page: its styles (the dark colours and the pieces put in where marked), its script, nothing from elsewhere.
constexpr const char* PAGE = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>fastchess-gui live</title>
<script>try{const t=localStorage.getItem('fcg-theme');if(t==='light'||t==='dark')document.documentElement.dataset.theme=t}catch(e){}</script>
<style>
:root{color-scheme:light;--bg:#f4f5f7;--card:#fff;--text:#1b1e24;--muted:#626976;--line:#e0e3e8;--soft:#eef0f3;--light:#dee3e6;--dark:#8ca2ad;--accent:#2f6fde;--win:#1d7f4e;--loss:#c03b33;--ew:#6ea8de;--eb:#ed994d;--book:#8a6a12}
:root[data-theme=dark]{/*DARK*/}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){/*DARK*/}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
header{padding:10px 20px;border-bottom:1px solid var(--line);background:var(--card);display:flex;justify-content:space-between;align-items:center;gap:8px 16px;flex-wrap:wrap}
h1{margin:0;font-size:18px;line-height:1.3}h2{margin:0 0 8px;font-size:15px;font-weight:600}h3{margin:14px 0 4px;font-size:13px;font-weight:600}a{color:var(--accent)}
.muted{color:var(--muted)}.small{font-size:12px}main{padding:16px 20px;display:flex;flex-direction:column;gap:16px;max-width:1720px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}
.tools,.controls{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
button{font:inherit;font-size:13px;color:var(--text);background:var(--card);border:1px solid var(--line);border-radius:8px;padding:5px 10px;cursor:pointer;min-width:34px}
button:hover:not(:disabled){border-color:var(--accent)}button:disabled{opacity:.4;cursor:default}
button svg{width:14px;height:14px;fill:currentColor;vertical-align:-2px}#theme svg{margin-right:5px}
.stage{display:grid;gap:16px;grid-template-columns:minmax(0,1fr);align-items:start}.stage.none>.side{display:none}
@media (min-width:800px){.stage{grid-template-columns:minmax(0,1.1fr) minmax(0,1fr)}#tabs{grid-column:1/-1}}
@media (min-width:1260px){.stage{grid-template-columns:minmax(380px,540px) minmax(400px,1fr) minmax(340px,1fr)}#tabs{grid-column:auto}.stage.none #tabs{grid-column:1/-1}}
.player{display:flex;align-items:center;gap:10px;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:5px 5px 5px 12px;min-height:46px}
.player.tomove{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}
.pname{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;border:1.5px solid var(--muted);flex:none}.dot.w{background:#fff}.dot.b{background:#15171b}
.clock{font-size:24px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.3;padding:1px 12px;border-radius:7px;background:var(--soft);min-width:92px;text-align:center}
.clock.run{background:var(--accent);color:#fff}.clock.low{color:var(--loss)}.clock.run.low{background:var(--loss);color:#fff}
.bwrap{display:flex;gap:6px;margin:8px 0}.bwrap .board{flex:1}
.ebar{width:14px;border-radius:3px;background:#15171b;position:relative;overflow:hidden;border:1px solid var(--line);flex:none}
.ebar>div{position:absolute;left:0;right:0;bottom:0;background:#e8eaee}.ebar.flip>div{top:0;bottom:auto}
.board{position:relative;display:grid;grid-template-columns:repeat(8,1fr);aspect-ratio:1;border-radius:4px;overflow:hidden;user-select:none}
.sq{position:relative;background-image:var(--pc,none),var(--ov,none);background-size:100% 100%;background-repeat:no-repeat}
.l{background-color:var(--light)}.d{background-color:var(--dark)}.l.lm{background-color:#f3e27c}.d.lm{background-color:#cdbd52}
.ck{--ov:radial-gradient(circle,rgba(235,30,30,.95) 0,rgba(235,30,30,.6) 32%,rgba(235,30,30,0) 72%)}
.co{position:absolute;font-size:clamp(8px,1.5vw,11px);font-weight:700;line-height:1;pointer-events:none}.cr{top:3px;left:3px}.cf{bottom:3px;right:3px}
.l .co{color:#6f8794}.d .co{color:#eef1f3}
.arrows{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.where{margin-left:auto;font-size:12px;color:var(--muted);text-align:right}
.live::before{content:"";display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--loss);margin-right:5px;vertical-align:1px}
.side{display:flex;flex-direction:column;gap:12px;min-width:0}
.engines{display:grid;grid-template-columns:repeat(auto-fit,minmax(165px,1fr));gap:12px}.engines .card{padding:10px}
.eng{border-top:3px solid var(--line)}.eng.thinking{border-top-color:var(--accent)}
.ehead{display:flex;align-items:center;gap:6px;font-size:13px;min-width:0}.ehead b{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1;min-width:0}
.state{font-size:10px;font-weight:600;letter-spacing:.05em;text-transform:uppercase;color:var(--muted);white-space:nowrap}
.thinking .state{color:var(--accent)}.thinking .state::before{content:"";display:inline-block;width:6px;height:6px;border-radius:50%;background:var(--accent);margin-right:4px;vertical-align:1px;animation:pulse 1.2s infinite}
@keyframes pulse{50%{opacity:.25}}@media (prefers-reduced-motion:reduce){.thinking .state::before{animation:none}}
.eval{font-size:30px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.15;margin:6px 0 8px}.eval small{font-size:12px;font-weight:400;color:var(--muted);margin-left:6px}
.tiles{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:4px}.tile{background:var(--soft);border-radius:6px;padding:3px 5px;min-width:0}
.tile span{display:block;font-size:10px;letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
.tile b{display:block;font-weight:600;font-size:13px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pv{font-size:13px;margin-top:8px;color:var(--muted);overflow-wrap:anywhere;line-height:1.5;min-height:1.5em}
.chead{display:flex;justify-content:space-between;align-items:baseline;gap:8px;font-size:13px;margin-bottom:6px}
.moves{position:relative;max-height:17em;overflow-y:auto;line-height:1.85;overflow-wrap:anywhere}@media (min-width:1260px){.moves{max-height:26em}}
.mn{color:var(--muted);margin:0 1px 0 5px;font-variant-numeric:tabular-nums;font-size:12px}
.mv{padding:1px 4px;border-radius:4px;cursor:pointer;white-space:nowrap}.mv:hover{background:var(--soft)}
.mv.bk{color:var(--book);font-style:italic}.mv.cur{background:var(--accent);color:#fff}
.tabbar{display:flex;gap:2px;border-bottom:1px solid var(--line);margin:-6px -4px 10px;overflow-x:auto;scrollbar-width:none}
.tabbar button{border:0;border-bottom:2px solid transparent;border-radius:0;background:none;padding:8px 7px;font-size:13px;font-weight:600;color:var(--muted);white-space:nowrap;min-width:0;flex:none}
.tabbar button[aria-selected=true]{color:var(--text);border-bottom-color:var(--accent)}#panel{overflow-x:auto;min-height:120px}
.games{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:14px}
.games .card{cursor:pointer;padding:10px}.games .card.sel{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}
.games .board{margin:6px 0}
.row{display:flex;justify-content:space-between;gap:8px;font-size:13px}.row b{font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.side2{display:flex;align-items:center;gap:6px;flex:none}
.pbar{display:inline-block;width:40px;height:6px;border-radius:3px;background:#15171b;overflow:hidden;border:1px solid var(--line);flex:none}
.pbar>span{display:block;height:100%;background:#e8eaee}.pbar.none{opacity:.3}
.charts{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;margin-top:8px}
.chart{width:100%;height:96px;display:block;background:var(--soft);border-radius:4px}.chart .axis{stroke:var(--muted);stroke-opacity:.5;stroke-width:1}
.chart .now{stroke:var(--text);stroke-opacity:.35;stroke-width:1;stroke-dasharray:3 3}
.key{display:inline-block;width:10px;height:3px;border-radius:2px;vertical-align:middle;margin:0 3px 0 8px}
.xth{font-size:13px;font-weight:600;margin:14px 0 6px}.xt{overflow-x:auto}.xt th:nth-child(2),.xt td:nth-child(2){position:sticky;left:0;background:var(--card);max-width:9em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.wdl{white-space:nowrap;word-spacing:4px}.tomove{color:var(--accent)}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums;font-size:13px}
th,td{padding:5px 7px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}th{color:var(--muted);font-weight:500}
td.n,th.n{text-align:left}.bad{color:var(--loss);font-weight:600}td.wrap{white-space:normal}
.toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--text);color:var(--bg);padding:8px 14px;border-radius:8px;font-size:13px;opacity:0;transition:opacity .2s;pointer-events:none}.toast.show{opacity:1}
dialog{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:10px;width:min(560px,92vw)}dialog textarea{width:100%;font:12px/1.4 ui-monospace,monospace;background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:6px}
@media (max-width:600px){header,main{padding-left:12px;padding-right:12px}main{padding-top:12px}.games{grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px}.clock{font-size:21px;min-width:80px}.eval{font-size:26px}}
/*PIECES*//*EXTRA_STYLES*/
</style></head><body>
<header><div><h1 id="name">fastchess-gui</h1><div class="muted small" id="progress"></div></div>
<div class="tools"><!--LINKS--><button id="notation" type="button" title="Moves in letters or figurines"></button><button id="theme" type="button" title="Light or dark, or as the system says"></button><span id="download"></span></div></header>
<main><div class="stage none" id="stage">
<section class="side" id="boardcol"><div><div class="player" id="ptop"></div>
<div class="bwrap"><div class="ebar" id="ebar"><div></div></div><div class="board" id="board"></div></div>
<div class="player" id="pbottom"></div></div>
<div class="controls"><button id="first" type="button" title="The start"><svg viewBox="0 0 16 16"><path d="M3 3h2v10H3zM13 3v10L6 8z"/></svg></button><button id="prev" type="button" title="The move before (←)"><svg viewBox="0 0 16 16"><path d="M11 3v10L4 8z"/></svg></button><button id="next" type="button" title="The move after (→)"><svg viewBox="0 0 16 16"><path d="M5 3v10l7-5z"/></svg></button><button id="live" type="button" title="Back to the game as it goes"><svg viewBox="0 0 16 16"><path d="M3 3v10l7-5zM11 3h2v10h-2z"/></svg></button>
<button id="flip" type="button" title="Turn the board (F)"><svg viewBox="0 0 16 16"><path d="M5 1l3.5 4.5H6V15H4V5.5H1.5zM11 15l-3.5-4.5H10V1h2v9.5h2.5z"/></svg></button><button id="cpgn" type="button" title="Copy the game in PGN">PGN</button><button id="cfen" type="button" title="Copy the position in FEN">FEN</button><span class="where" id="where"></span></div></section>
<section class="side"><div class="engines" id="engines"></div><div class="card"><div class="chead" id="mhead"></div><div class="moves" id="moves"></div></div><!--WIDGET--></section>
<section class="card" id="tabs"><nav class="tabbar" id="tabbar" role="tablist"></nav><div id="panel" role="tabpanel"></div></section></div>
<section><h2 id="gameshead">Games in progress</h2><div class="games" id="games"></div></section></main>
<div class="toast" id="toast"></div><dialog id="copybox"><div class="small muted">Copying is not allowed here: select the text and copy it.</div><textarea id="copytext" readonly></textarea><div style="text-align:right;margin-top:6px"><button id="copyclose" type="button">Close</button></div></dialog>
<script>
const PIECE={K:'wK',Q:'wQ',R:'wR',B:'wB',N:'wN',P:'wP',k:'bK',q:'bQ',r:'bR',b:'bB',n:'bN',p:'bP'};
const START='rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
let selected=null,last=null,lastText='',view=null,viewGame=null,toEnd=false,flipped=false,tab='standings',theme='auto';
// What the viewer chose last (letters or figurines, the tab, the theme); the browser may keep nothing.
const FIG={K:'♔',Q:'♕',R:'♖',B:'♗',N:'♘'};let figurines=false;
try{figurines=localStorage.getItem('fcg-figurines')==='1';tab=localStorage.getItem('fcg-tab')||tab;theme=localStorage.getItem('fcg-theme')||theme}catch(e){}
function keep(k,v){try{localStorage.setItem(k,v)}catch(e){}}
function san(m){return esc(figurines?m.replace(/[KQRBN]/g,c=>FIG[c]+'\uFE0E'):m)}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function clock(ms){if(ms<0)ms=0;const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
 return h?`${h}:${String(m%60).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`:`${m}:${String(s%60).padStart(2,'0')}`}
function count(n){return n>=1e9?(n/1e9).toFixed(1)+'G':n>=1e6?(n/1e6).toFixed(1)+'M':n>=1e3?(n/1e3).toFixed(1)+'k':String(n)}
// A count in three figures at most, for the search tiles: 297k, 16M, 9.1M.
function short(n){for(const [v,u] of [[1e9,'G'],[1e6,'M'],[1e3,'k']])if(n>=v){const x=n/v;return (x<10?x.toFixed(1):Math.round(x))+u}return String(n)}
function secs(ms){return ms<60000?(ms/1000).toFixed(1)+' s':clock(ms)}
function score(cp){if(cp===null||cp===undefined)return '–';if(Math.abs(cp)>=90000){const n=100000-Math.abs(cp);return (cp>0?'#':'-#')+n}
 return (cp>=0?'+':'')+(cp/100).toFixed(2)}
function pawns(cp){return Math.max(-8,Math.min(8,Math.abs(cp)>=90000?Math.sign(cp)*8:cp/100))}
function player(name,elo){return esc(name)+(elo?` <span class="muted">${elo}</span>`:'')}
function half(x){return Number.isInteger(x)?String(x):(Math.floor(x)||'')+'½'}

// The positions of a game, from its start position and its moves in UCI: enough to show them and to copy their
// FEN, not a move generator (fastchess checked the moves). Squares from 0 (a8) to 63 (h1), as a FEN lists them.
const sqi=s=>(56-s.charCodeAt(1))*8+s.charCodeAt(0)-97,sqn=i=>String.fromCharCode(97+i%8)+(8-(i>>3));
function parse(fen){const f=fen.split(' '),b=[];for(const c of f[0]){if(c==='/')continue;if(c>='1'&&c<='8')for(let i=0;i<+c;i++)b.push('');else b.push(c)}
 // Each castling right as its rook's square: K and Q the outermost rooks, a file letter (Chess960) that file's.
 const rights=[];for(const c of f[2]&&f[2]!=='-'?f[2]:''){const white=c<'a',row=white?56:0,rook=white?'R':'r',king=b.indexOf(white?'K':'k',row)-row;
  let file=-1;if(c==='K'||c==='k'){for(let x=7;x>king&&file<0;x--)if(b[row+x]===rook)file=x}else if(c==='Q'||c==='q'){for(let x=0;x<king&&file<0;x++)if(b[row+x]===rook)file=x}
  else file=c.toLowerCase().charCodeAt(0)-97;if(file>=0)rights.push({c,white,sq:row+file})}
 return {b,w:(f[1]||'w')==='w',rights,ep:f[3]||'-',h:+f[4]||0,n:+f[5]||1}}
function play(p,u){const b=p.b.slice(),from=sqi(u),to=sqi(u.slice(2)),pc=b[from]||'',white=pc!==''&&pc<'a',kind=pc.toLowerCase(),row=from&56,rook=white?'R':'r';
 let reset=kind==='p'||b[to]!=='',ep='-';
 if(kind==='k'&&(b[to]===rook||Math.abs(to-from)===2)){
  // Castling: the king onto its own rook (Chess960), or two squares aside.
  const r=b[to]===rook?to:to>from?row+7:row,short=r>from;b[from]='';b[r]='';b[row+(short?6:2)]=pc;b[row+(short?5:3)]=rook;reset=false}
 else{if(kind==='p'&&(to-from)%8&&!b[to])b[to+(white?8:-8)]='';
  b[to]=u.length>4?(white?u[4].toUpperCase():u[4]):pc;b[from]='';
  if(kind==='p'&&Math.abs(to-from)===16){const x=to%8,them=white?'p':'P';if(x>0&&b[to-1]===them||x<7&&b[to+1]===them)ep=sqn((from+to)/2)}}
 return {b,w:!p.w,rights:p.rights.filter(r=>r.sq!==from&&r.sq!==to&&!(kind==='k'&&r.white===white)),ep,h:reset?0:p.h+1,n:p.n+(p.w?0:1)}}
function fenOf(p){let out='';for(let r=0;r<8;r++){let e=0;for(let f=0;f<8;f++){const c=p.b[r*8+f];if(!c){e++;continue}if(e)out+=e;e=0;out+=c}if(e)out+=e;if(r<7)out+='/'}
 return `${out} ${p.w?'w':'b'} ${p.rights.map(r=>r.c).join('')||'-'} ${p.ep} ${p.h} ${p.n}`}
let cache={key:'',list:[]};
function positions(g){const uci=g.uci||[],key=g.number+' '+g.start_fen+' '+(uci[0]||'');if(cache.key!==key||cache.list.length>uci.length+1)cache={key,list:[parse(g.start_fen)]};
 const list=cache.list;while(list.length<=uci.length)list.push(play(list[list.length-1],uci[list.length-1]));return list}

function board(b,o){let html='';for(let k=0;k<64;k++){const i=o.flip?63-k:k,r=i>>3,f=i%8,pc=b[i];
 let cls='sq '+((r+f)%2?'d':'l');if(pc)cls+=' p'+PIECE[pc];if(i===o.from||i===o.to)cls+=' lm';if(i===o.check)cls+=' ck';
 let co='';if(o.coords){if(k%8===0)co+=`<span class="co cr">${8-r}</span>`;if(k>=56)co+=`<span class="co cf">${String.fromCharCode(97+f)}</span>`}
 html+=`<div class="${cls}">${co}</div>`}return html}
// The last move and a king in check, after `k` moves of a game; `p` the position.
function marks(g,p,k){const o={};if(k>0&&g.uci&&g.uci[k-1]){o.from=sqi(g.uci[k-1]);o.to=sqi(g.uci[k-1].slice(2))}
 if(k>0&&/[+#]$/.test(g.moves[k-1]||''))o.check=p.b.indexOf(p.w?'K':'k');return o}
// White's evaluation now: the side to move's search, else the last one.
function whiteCp(g){const s=g.white_to_move?g.white_search:g.black_search;if(s&&s.depth)return g.white_to_move?s.score_cp:-s.score_cp;return evalAt(g,g.evals.length)}
// White's evaluation after `k` moves: the last one known.
function evalAt(g,k){for(let i=Math.min(k,g.evals.length)-1;i>=0;i--)if(g.evals[i]!==null)return g.evals[i];return null}
// White's share of a bar, from White's evaluation.
function share(cp){if(cp===null)return 50;cp=Math.max(-1000,Math.min(1000,cp));return 50+50*(2/(1+Math.exp(-cp/250))-1)}
// A player's own evaluation, from its last search, as a small bar of White's share.
function pbar(g,white){const s=white?g.white_search:g.black_search;if(!s||!s.depth)return '<span class="pbar none" title="–"><span style="width:50%"></span></span>';
 const cp=white?s.score_cp:-s.score_cp;return `<span class="pbar" title="${score(cp)}"><span style="width:${share(cp)}%"></span></span>`}
// Squares as board coordinates, a8 at the top left (h1 when the board is turned).
function at(sq,flip){const x=sq.charCodeAt(0)-96.5,y=8.5-(+sq[1]);return flip?[8-x,8-y]:[x,y]}
function arrow(uci,side,opacity,flip){if(!/^[a-h][1-8][a-h][1-8]/.test(uci))return '';const [x1,y1]=at(uci.slice(0,2),flip),[x2,y2]=at(uci.slice(2,4),flip);
 const dx=x2-x1,dy=y2-y1,l=Math.hypot(dx,dy),k=(l-.35)/l;
 return `<g opacity="${opacity}"><line x1="${x1}" y1="${y1}" x2="${x1+dx*k}" y2="${y1+dy*k}" style="stroke:var(--e${side})" stroke-width=".17" marker-end="url(#h${side})"/></g>`}
// The move the side to move thinks of, and the reply it expects, in the colours of the engines that would play them.
function arrows(g,flip){const s=g.white_to_move?g.white_search:g.black_search;if(!g.active||!s||!s.current||!s.pv||!s.pv.length)return '';
 const me=g.white_to_move?'w':'b',other=g.white_to_move?'b':'w';return (s.pv.length>1?arrow(s.pv[1],other,.6,flip):'')+arrow(s.pv[0],me,.85,flip)}
const MARKERS='<defs>'+['w','b'].map(c=>`<marker id="h${c}" viewBox="0 0 10 10" refX="4" refY="5" markerWidth="2.6" markerHeight="2.6" orient="auto"><path d="M0,0L10,5L0,10z" style="fill:var(--e${c})"/></marker>`).join('')+'</defs>';

// A small line chart: series of [ply, value], plies on the x axis; a dashed line at the move shown.
function chart(title,series,n,min,max,zero,unit,now){const W=300,H=96,y=v=>H-2-(v-min)/(max-min||1)*(H-4),x=i=>i/Math.max(20,n)*W;
 let svg=`<svg class="chart" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">`;
 if(zero)svg+=`<line class="axis" x1="0" x2="${W}" y1="${y(0)}" y2="${y(0)}" vector-effect="non-scaling-stroke"/>`;
 if(now!==null)svg+=`<line class="now" x1="${x(now)}" x2="${x(now)}" y1="0" y2="${H}" vector-effect="non-scaling-stroke"/>`;
 for(const s of series)if(s.pts.length)svg+=`<polyline fill="none" style="stroke:${s.color}" stroke-width="1.5" vector-effect="non-scaling-stroke" points="${s.pts.map(([i,v])=>x(i).toFixed(1)+','+y(v).toFixed(1)).join(' ')}"/>`;
 return `<div><div class="row muted small"><span>${title}</span><span>${unit}</span></div>${svg}</svg></div>`}
function charts(g,now){const wf=(g.start_fen.split(' ')[1]||'w')==='w',n=g.moves.length,white=i=>(i%2===0)===wf;
 const ev=[],dw=[],db=[],tw=[],tb=[];
 g.evals.forEach((v,i)=>{if(v!==null)ev.push([i+1,pawns(v)])});
 g.depths.forEach((v,i)=>{if(v!==null)(white(i)?dw:db).push([i+1,v])});
 g.times_ms.forEach((v,i)=>{if(v!==null)(white(i)?tw:tb).push([i+1,v/1000])});
 const top=a=>Math.max(1,...a.map(p=>p[1]));
 const keys=`<span class="key" style="background:var(--ew)"></span>${esc(g.white)}<span class="key" style="background:var(--eb)"></span>${esc(g.black)}`;
 return `<div class="row muted small"><span>Game #${g.number}</span><span>${keys}</span></div><div class="charts">`+
  chart('Evaluation',[{pts:ev,color:'var(--accent)'}],n,-8.5,8.5,true,'±8 pawns, White',now)+
  chart('Depth',[{pts:dw,color:'var(--ew)'},{pts:db,color:'var(--eb)'}],n,0,top(dw.concat(db)),false,'up to '+top(dw.concat(db)),now)+
  chart('Time',[{pts:tw,color:'var(--ew)'},{pts:tb,color:'var(--eb)'}],n,0,top(tw.concat(tb)),false,'up to '+top(tw.concat(tb)).toFixed(1)+' s',now)+'</div>'}

// A player beside the board: its colour, its name, its evaluation and its clock, running while it thinks.
function playerBar(g,white){const ms=white?g.white_ms:g.black_ms,run=g.active&&g.white_to_move===white;
 return `<span class="dot ${white?'w':'b'}"></span><span class="pname"><b>${esc(white?g.white:g.black)}</b>${(white?g.white_elo:g.black_elo)?` <span class="muted">${white?g.white_elo:g.black_elo}</span>`:''}</span>`+
  pbar(g,white)+(g.clocks?`<span class="clock${run?' run':''}${ms<10000&&g.active?' low':''}">${clock(ms)}</span>`:'')}
// An engine's search: its evaluation in large type, what it searched as tiles, and its PV; the current search
// while it thinks, else its last move's.
function engineCard(g,white){const s=white?g.white_search:g.black_search,has=s&&s.depth>0,thinks=g.active&&g.white_to_move===white;
 const state=thinks?'Thinking':g.active?'Waiting':esc(g.result||'');
 const cp=has?(white?s.score_cp:-s.score_cp):null,nps=has?(s.nps||(s.time_ms>0?Math.round(s.nodes*1000/s.time_ms):0)):0;
 const tile=(name,value)=>`<div class="tile"><span>${name}</span><b>${value}</b></div>`;
 const pv=has&&s.pv_san?san(s.pv_san):has&&s.current&&s.pv&&s.pv.length?esc(s.pv.join(' ')):'';
 return `<div class="card eng${thinks?' thinking':''}"><div class="ehead"><span class="dot ${white?'w':'b'}"></span><b title="${esc(white?g.white:g.black)}">${esc(white?g.white:g.black)}</b><span class="state">${state}</span></div>`+
  `<div class="eval" title="White's point of view">${score(cp)}${has&&!s.current?'<small>last move</small>':''}</div><div class="tiles">`+
  tile('Depth',has?s.depth+(s.seldepth?'/'+s.seldepth:''):'–')+tile('Nodes',has&&s.nodes?short(s.nodes):'–')+tile('NPS',nps?short(nps):'–')+
  tile('Time',has?secs(s.time_ms):'–')+tile('TB',has&&s.tbhits!==null&&s.tbhits!==undefined?short(s.tbhits):'–')+
  tile('Hash',has&&s.hashfull!==null&&s.hashfull!==undefined?Math.round(s.hashfull/10)+'%':'–')+`</div><div class="pv">${pv}</div></div>`}
// The moves of a game, numbered from its start position; the move shown marked, the book's told apart.
function moveList(g,cur){const f=g.start_fen.split(' '),book=g.book||[];let white=(f[1]||'w')==='w',n=+f[5]||1,out='';
 g.moves.forEach((m,i)=>{if(white)out+=`<span class="mn">${n}.</span>`;else if(!i)out+=`<span class="mn">${n}…</span>`;
  out+=`<span class="mv${book[i]?' bk':''}${i+1===cur?' cur':''}" data-p="${i+1}"${book[i]?' title="From the opening book"':''}>${san(m)}</span> `;if(!white)n++;white=!white});
 if(!g.active&&g.result)out+=` <b>${esc(g.result)}</b>`;
 return out||'<span class="muted">No move yet.</span>'}
// The move after `k` moves, as "12…Nf6".
function moveName(g,k){const f=g.start_fen.split(' '),w0=(f[1]||'w')==='w',ply=k-1+(w0?0:1);return `${(+f[5]||1)+Math.floor(ply/2)}${ply%2?'…':'.'}${san(g.moves[k-1])}`}

function stats(s){const t=s.stats;if(!t||!t.games)return '<div class="muted">No game played yet.</div>';
 const pc=x=>`${x} <span class="muted">(${Math.round(100*x/t.games)} %)</span>`,row=(a,b)=>`<tr><td class="n">${a}</td><td>${b}</td></tr>`;
 return '<table>'+row('Games',t.games)+row('White wins (1-0)',pc(t.white_wins))+row('Black wins (0-1)',pc(t.black_wins))+row('Draws',pc(t.draws))+
  row('Average length',Math.round(t.plies/t.games/2)+' moves')+row('Average duration',clock(t.duration_ms/t.games))+row('Adjudicated',pc(t.adjudications))+'</table>'}
const KINDS={time:'time loss',crash:'crash',stall:'no answer',illegal:'illegal move'};
function anomalies(s){const t=s.stats||{},n=v=>v?`<span class="bad">${v}</span>`:'0';
 let html=`<div>Time losses ${n(t.time_losses)} · crashes ${n(t.crashes)} · no answer ${n(t.stalls)} · illegal moves ${n(t.illegal_moves)}</div>`;
 if(!s.anomalies.length)return html+'<div class="muted">None so far.</div>';
 return html+'<table>'+s.anomalies.slice(0,15).map(a=>`<tr title="${esc(a.reason)}"><td>#${a.number}</td><td class="n">${esc(a.white)} – ${esc(a.black)}</td><td>${esc(a.result)}</td><td class="n">${KINDS[a.kind]||esc(a.kind)}${a.engine?' · '+esc(a.engine):''}</td></tr>`).join('')+'</table>'}
// Each engine's search per move and the games it lost badly, as the Statistics tab shows them.
function engines(s){const e=s.engines||[],v=(x,f)=>x===null||x===undefined?'<span class="muted">–</span>':f(x);
 if(!e.some(x=>x.moves))return '<div class="muted">No game played yet.</div>';
 const bad=x=>[[x.time_losses,'time loss','time losses'],[x.crashes,'crash','crashes'],[x.stalls,'no answer','no answer'],[x.illegal_moves,'illegal move','illegal moves']]
  .filter(a=>a[0]).map(a=>a[0]+' '+(a[0]>1?a[2]:a[1])).join(' · ');
 return '<div class="muted small">On average per move searched, book moves left out.</div><table><tr><th class="n">Engine</th><th>Depth</th><th>Time</th><th>Nodes</th><th>Speed</th><th class="n">Anomalies</th></tr>'+e.map(x=>{const b=bad(x);
  return `<tr><td class="n">${esc(x.name)}</td><td>${v(x.depth,d=>d.toFixed(1))}</td><td>${v(x.time_ms,t=>(t/1000).toFixed(2)+' s')}</td><td>${v(x.nodes,count)}</td>`+
   `<td>${v(x.nps,n=>count(n)+'/s')}</td><td class="n">${b?`<span class="bad">${b}</span>`:'<span class="muted">none</span>'}</td></tr>`}).join('')+'</table>'}
// The next games of the schedule; a Swiss or a knockout pairs its rounds as it goes.
function upcoming(s){const n=s.next||[],asItGoes=s.format==='swiss'||s.format==='knockout',cycles=!asItGoes&&n.some(x=>x.cycle>1);
 let html=n.length?'<table>'+n.map(x=>`<tr><td>#${x.number}</td><td class="n">${cycles?'Cycle '+x.cycle+' · ':''}Round ${x.round}</td>`+
  `<td class="n wrap">${player(x.white)} – ${player(x.black)}${x.opening?`<div class="muted">${esc(x.opening)}</div>`:''}</td></tr>`).join('')+'</table>':'';
 if(asItGoes)html+=`<div class="muted">${n.length?'The later rounds are':'The next round is'} paired as the tournament goes, from the results: their games are not known in advance.</div>`;
 return html||'<div class="muted">No game left to play.</div>'}
function standings(s){const rows=s.standings;if(!rows.length)return '<div class="muted">No engine yet.</div>';
 let t='<table><tr><th>#</th><th class="n">Engine</th><th>Games</th><th>Points</th><th>Elo</th><th>±</th><th title="Wins, draws, losses">+ = −</th></tr>';
 rows.forEach(r=>{t+=`<tr><td>${r.rank}</td><td class="n">${esc(r.name)}</td><td>${r.games}</td><td>${half(r.points)}</td><td>${r.games?(r.elo>=0?'+':'')+Math.round(r.elo):''}</td><td class="muted">${r.games?Math.round(r.elo_error):''}</td><td class="wdl">${r.wins} ${r.draws} ${r.losses}</td></tr>`});
 t+='</table>';
 // The crosstable has a column per engine: below the standings, scrolled sideways when it is wider, the names kept.
 if(rows.length<2||!rows.some(r=>r.crosstable&&r.crosstable.length))return t;
 t+='<h3 class="xth">Crosstable</h3><div class="xt"><table><tr><th>#</th><th class="n">Engine</th>'+rows.map((_,i)=>`<th>${i+1}</th>`).join('')+'</tr>';
 rows.forEach(r=>{t+=`<tr><td>${r.rank}</td><td class="n">${esc(r.name)}</td>`+
  r.crosstable.map(c=>c?`<td title="+${c.wins} =${c.draws} -${c.losses}">${c.wins+c.draws+c.losses?half(c.wins+c.draws/2):''}</td>`:'<td class="muted">–</td>').join('')+'</tr>'});
 return t+'</table></div>'}
const FORMATS={match:'Match',round_robin:'Round robin',gauntlet:'Gauntlet',swiss:'Swiss',knockout:'Knockout'};
function eventInfo(s){const row=(a,b)=>`<tr><td class="n muted">${a}</td><td class="n wrap">${b}</td></tr>`;
 return '<table>'+row('Tournament',esc(s.name||'–'))+row('Format',FORMATS[s.format]||esc(s.format||'–'))+(s.time_control?row('Time control',esc(s.time_control)):'')+
  row('Variant',s.chess960?'Chess960':'Standard chess')+row('Games',`${s.played}${s.total?' of '+s.total:''} played${s.finished?', finished':''}`)+
  (s.pgn?row('Games file',`<a href="games.pgn" download="${esc(s.pgn)}">${esc(s.pgn)}</a>`):'')+'</table>'+
  `<h3>Latest results</h3>${s.recent.slice(0,12).map(x=>`<div>${esc(x)}</div>`).join('')||'<div class="muted">No game played yet.</div>'}`}
const TABS=[['standings','Standings'],['charts','Charts'],['engines','Engines'],['next','Next'],['stats','Statistics'],['event','Event']];
if(!TABS.some(t=>t[0]===tab))tab='standings';

function follow(s){return s.games.find(x=>x.number===selected)||s.games.find(x=>x.active)||s.games[0]}
// The move shown in the followed game: its last one while the viewer follows the game as it goes.
function shown(g){return view===null?g.moves.length:Math.min(view,g.moves.length)}
function render(s){last=s;
 document.getElementById('notation').textContent='Moves: '+(figurines?'♘\uFE0Ef3':'Nf3');
 document.getElementById('name').textContent=s.name||'fastchess-gui';document.title=s.name||'fastchess-gui';
 document.getElementById('progress').textContent=[`${s.played}${s.total?' / '+s.total:''} games${s.finished?' · finished':''}`,FORMATS[s.format],s.time_control].filter(x=>x).join(' · ');
 document.getElementById('download').innerHTML=s.pgn?`<a href="games.pgn" download="${esc(s.pgn)}">Download the games (PGN)</a>`:'';
 const g=follow(s);if(!g||g.number!==viewGame){view=null;viewGame=g?g.number:null}
 document.getElementById('stage').classList.toggle('none',!g);
 if(g)game(g);
 document.getElementById('gameshead').textContent=s.games.length?'Games in progress':'';
 document.getElementById('games').innerHTML=s.games.map(x=>{const p=parse(x.fen);return `<div class="card${g&&x.number===g.number?' sel':''}" data-n="${x.number}">
  <div class="row"><b class="${!x.white_to_move&&x.active?'tomove':''}">${player(x.black,x.black_elo)}</b><span class="side2">${pbar(x,false)}<span class="muted">${x.clocks?clock(x.black_ms):''}</span></span></div>
  <div class="board">${board(p.b,marks(x,p,x.moves.length))}</div>
  <div class="row"><b class="${x.white_to_move&&x.active?'tomove':''}">${player(x.white,x.white_elo)}</b><span class="side2">${pbar(x,true)}<span class="muted">${x.clocks?clock(x.white_ms):''}</span></span></div>
  <div class="row muted"><span>#${x.number} · ${Math.ceil(x.moves.length/2)} moves</span><span>${x.active?'':esc(x.result)}</span></div></div>`}).join('')||'<div class="muted">No game in progress.</div>';
 panel(s,g)/*RENDER*/}
// The followed game: the board at the move shown, the players and their clocks, the engines' searches, the moves.
function game(g){const n=g.moves.length,list=positions(g),ok=list.length===n+1,k=ok?shown(g):n,live=k===n;
 const p=live?parse(g.fen):list[k];
 document.getElementById('board').innerHTML=board(p.b,Object.assign({flip:flipped,coords:true},marks(g,p,k)))+
  `<svg class="arrows" viewBox="0 0 8 8">${MARKERS}${live?arrows(g,flipped):''}</svg>`;
 const cp=live?whiteCp(g):evalAt(g,k),bar=document.getElementById('ebar');bar.classList.toggle('flip',flipped);
 bar.title=score(cp);bar.firstChild.style.height=share(cp)+'%';
 for(const [id,white] of [['ptop',flipped],['pbottom',!flipped]]){const e=document.getElementById(id);
  e.className='player'+(g.active&&g.white_to_move===white?' tomove':'');e.innerHTML=playerBar(g,white)}
 document.getElementById('engines').innerHTML=engineCard(g,true)+engineCard(g,false);
 document.getElementById('first').disabled=document.getElementById('prev').disabled=!ok||k===0;
 document.getElementById('next').disabled=document.getElementById('live').disabled=live;
 document.getElementById('where').innerHTML=live?(g.active?'<span class="live">Live</span>':esc(g.result||'')):k?`After ${moveName(g,k)}`:'Start position';
 document.getElementById('mhead').innerHTML=`<b>Game #${g.number} · ${Math.ceil(n/2)} moves</b>${(g.book||[]).some(x=>x)?'<span class="mv bk small">book moves</span>':''}`;
 const moves=document.getElementById('moves'),atEnd=moves.dataset.n!==String(g.number)||moves.scrollTop+moves.clientHeight>=moves.scrollHeight-4;
 moves.dataset.n=g.number;moves.innerHTML=moveList(g,k);
 // The move list follows the game at its end, and the move shown elsewhere.
 const cur=moves.querySelector('.cur');
 if(live&&(atEnd||toEnd))moves.scrollTop=moves.scrollHeight;
 else if(!live&&cur&&(cur.offsetTop<moves.scrollTop||cur.offsetTop+cur.offsetHeight>moves.scrollTop+moves.clientHeight))moves.scrollTop=cur.offsetTop-moves.clientHeight/2}
function panel(s,g){document.getElementById('tabbar').innerHTML=TABS.map(([id,name])=>`<button type="button" role="tab" data-tab="${id}" aria-selected="${id===tab}">${name}</button>`).join('');
 document.getElementById('panel').innerHTML=tab==='standings'?standings(s):tab==='charts'?(g?charts(g,view===null?null:shown(g)):'<div class="muted">No game in progress.</div>'):
  tab==='engines'?engines(s):tab==='next'?upcoming(s):tab==='stats'?`<h3 style="margin-top:0">Results</h3>${stats(s)}<h3>Anomalies</h3>${anomalies(s)}`:eventInfo(s)}

function redraw(){if(last)render(last)}
// Stepping through the followed game's moves; past its last move, back to the game as it goes.
function step(a){const g=last&&follow(last);if(!g)return;const n=g.moves.length;let k=shown(g);
 k=a==='first'?0:a==='prev'?Math.max(0,k-1):a==='next'?k+1:n;view=k>=n?null:k;toEnd=view===null;redraw();toEnd=false}
function toast(text){const t=document.getElementById('toast');t.textContent=text;t.classList.add('show');clearTimeout(toast.t);toast.t=setTimeout(()=>t.classList.remove('show'),1600)}
// The clipboard, where the browser allows it (HTTPS or localhost), else the old way, else the text to copy by hand.
async function copy(text,what){try{await navigator.clipboard.writeText(text);toast(what+' copied');return}catch(e){}
 const t=document.createElement('textarea');t.value=text;t.setAttribute('readonly','');t.style.position='fixed';t.style.opacity='0';document.body.appendChild(t);t.select();
 let ok=false;try{ok=document.execCommand('copy')}catch(e){}t.remove();if(ok){toast(what+' copied');return}
 const box=document.getElementById('copybox');const area=document.getElementById('copytext');area.value=text;area.style.height=text.includes('\n')?'240px':'4em';box.showModal();area.select()}
function pgn(s,g){const tag=(k,v)=>`[${k} "${String(v).replace(/[\\"]/g,c=>'\\'+c)}"]\n`,result=g.active?'*':g.result||'*';
 let out=tag('Event',s.name||'')+tag('Round',g.number)+tag('White',g.white)+tag('Black',g.black)+tag('Result',result);
 if(g.white_elo)out+=tag('WhiteElo',g.white_elo);if(g.black_elo)out+=tag('BlackElo',g.black_elo);
 if(s.chess960)out+=tag('Variant','Chess960');if(s.chess960||g.start_fen!==START)out+=tag('SetUp','1')+tag('FEN',g.start_fen);
 const f=g.start_fen.split(' ');let white=(f[1]||'w')==='w',n=+f[5]||1,line='',text='';
 g.moves.forEach((m,i)=>{const t=white?`${n}. ${m}`:i?m:`${n}... ${m}`;if(!white)n++;white=!white;
  if(line&&line.length+1+t.length>79){text+=line+'\n';line=''}line+=(line?' ':'')+t});
 return out+'\n'+text+line+(line?' ':'')+result+'\n'}
/*EXTRAS*/function applyTheme(){if(theme==='light'||theme==='dark')document.documentElement.dataset.theme=theme;else{theme='auto';delete document.documentElement.dataset.theme}
 document.getElementById('theme').innerHTML='<svg viewBox="0 0 16 16"><path d="M8 1a7 7 0 1 0 0 14A7 7 0 0 0 8 1zm0 1.5v11a5.5 5.5 0 0 1 0-11z"/></svg>'+{auto:'Auto',light:'Light',dark:'Dark'}[theme]/*RENDER*/}
applyTheme();
document.getElementById('theme').addEventListener('click',()=>{theme={auto:'light',light:'dark',dark:'auto'}[theme];keep('fcg-theme',theme);applyTheme()});
document.getElementById('notation').addEventListener('click',()=>{figurines=!figurines;keep('fcg-figurines',figurines?'1':'0');redraw()});
document.getElementById('games').addEventListener('click',e=>{const c=e.target.closest('[data-n]');if(c){selected=+c.dataset.n;view=null;redraw()}});
document.getElementById('moves').addEventListener('click',e=>{const m=e.target.closest('[data-p]'),g=last&&follow(last);if(m&&g){const k=+m.dataset.p;view=k>=g.moves.length?null:k;redraw()}});
document.getElementById('tabbar').addEventListener('click',e=>{const b=e.target.closest('[data-tab]');if(b){tab=b.dataset.tab;keep('fcg-tab',tab);redraw()}});
for(const a of ['first','prev','next','live'])document.getElementById(a).addEventListener('click',()=>step(a));
document.getElementById('flip').addEventListener('click',()=>{flipped=!flipped;redraw()});
document.getElementById('cpgn').addEventListener('click',()=>{const g=last&&follow(last);if(g)copy(pgn(last,g),'PGN')});
document.getElementById('cfen').addEventListener('click',()=>{const g=last&&follow(last);if(!g)return;const list=positions(g),k=shown(g);
 copy(k===g.moves.length||list.length!==g.moves.length+1?g.fen:fenOf(list[k]),'FEN')});
document.getElementById('copyclose').addEventListener('click',()=>document.getElementById('copybox').close());
document.addEventListener('keydown',e=>{if(e.altKey||e.ctrlKey||e.metaKey||/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)||document.getElementById('copybox').open)return;
 const a={ArrowLeft:'prev',ArrowRight:'next',f:'flip',F:'flip'}[e.key];if(!a)return;e.preventDefault();if(a==='flip'){flipped=!flipped;redraw()}else step(a)});
async function refresh(){try{const text=await (await fetch('state.json',{cache:'no-store'})).text();if(text!==lastText){lastText=text;render(JSON.parse(text))}}
 catch(e){document.getElementById('progress').textContent='Waiting for fastchess-gui…'}}
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";

// The pieces, the GUI's own, as images in the page's styles: a class per piece ("pwK"), its picture in --pc.
std::string pieceStyles() {
    static constexpr const char* names[] = {"wK", "wQ", "wR", "wB", "wN", "wP", "bK", "bQ", "bR", "bB", "bN", "bP"};
    std::string css;
    for (std::size_t i = 0; i < assets::piece_svgs.size(); ++i) {
        css += std::string(".p") + names[i] + "{--pc:url(\"data:image/svg+xml,";
        for (const char c : assets::piece_svgs[i]) {
            switch (c) {
                case '"':
                    css += '\'';
                    break;
                case '#':
                    css += "%23";
                    break;
                case '%':
                    css += "%25";
                    break;
                case '<':
                    css += "%3C";
                    break;
                case '>':
                    css += "%3E";
                    break;
                case '\r':
                case '\n':
                    css += ' ';
                    break;
                default:
                    css += c;
            }
        }
        css += "\")}\n";
    }
    return css;
}

void replaceAll(std::string& text, std::string_view mark, std::string_view by) {
    for (auto at = text.find(mark); at != std::string::npos; at = text.find(mark, at + by.size())) {
        text.replace(at, mark.size(), by);
    }
}

// The extras, put in where marked: links styled as the page's buttons, the chat's in the accent colour; the widget's
// card, no taller than the side column needs.
constexpr const char* EXTRA_STYLES =
    ".btn{display:inline-block;font-size:13px;color:var(--text);background:var(--card);border:1px solid var(--line);"
    "border-radius:8px;padding:5px 10px;text-decoration:none}.btn:hover{border-color:var(--accent)}"
    ".btn[hidden]{display:none}.btn svg{width:14px;height:14px;fill:currentColor;vertical-align:-2px;margin-right:5px}"
    "#chat{background:var(--accent);border-color:var(--accent);color:#fff}#discord{padding:0;overflow:hidden}"
    "#discord iframe{display:block;width:100%;height:400px;border:0}"
    "@media (max-width:600px){#discord iframe{height:340px}}";
// The links open in a new tab, which learns nothing of the page (rel), their address set from state.json.
constexpr const char* LINKS =
    "<a class=\"btn\" id=\"chat\" hidden target=\"_blank\" rel=\"noopener noreferrer\" title=\"Where the "
    "viewers talk, in a new tab\"><svg viewBox=\"0 0 16 16\"><path d=\"M2 2h12a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1H6l-3 "
    "3v-3H2a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1z\"/></svg>Chat</a><a class=\"btn\" id=\"lichessround\" hidden "
    "target=\"_blank\" rel=\"noopener noreferrer\" title=\"The games on lichess, with its chat, in a new "
    "tab\">lichess</a>";
// Discord's widget, with what it needs only: its scripts on its own origin, and the Join button opening Discord in a
// new tab. Loaded once state.json gives the server, so that without one Discord is never asked.
constexpr const char* WIDGET =
    "<div class=\"card\" id=\"discord\" hidden><iframe id=\"discordframe\" title=\"Discord\" sandbox=\"allow-scripts "
    "allow-same-origin allow-popups allow-popups-to-escape-sandbox\" referrerpolicy=\"no-referrer\" "
    "loading=\"lazy\"></iframe></div>";
// Where the viewers talk, from state.json, its values checked again here: links set as addresses, never as HTML;
// Discord's widget in the page's theme, followed when the viewer or the system changes it.
constexpr const char* EXTRAS = R"JS(let discordSrc='';
function extras(){const s=last||{};
 for(const [id,url] of [['chat',s.chat],['lichessround',s.lichess]]){const a=document.getElementById(id),ok=typeof url==='string'&&/^https:\/\/[^\s"'<>\\`]+$/i.test(url);
  a.hidden=!ok;if(ok&&a.getAttribute('href')!==url)a.href=url}
 const id=typeof s.discord==='string'&&/^[0-9]{17,20}$/.test(s.discord)?s.discord:'';document.getElementById('discord').hidden=!id;
 let dark=theme==='dark';try{dark=dark||theme!=='light'&&matchMedia('(prefers-color-scheme: dark)').matches}catch(e){}
 const src=id?`https://discord.com/widget?id=${id}&theme=${dark?'dark':'light'}`:'';
 if(src!==discordSrc){discordSrc=src;const f=document.getElementById('discordframe');if(src)f.src=src;else f.removeAttribute('src')}}
try{matchMedia('(prefers-color-scheme: dark)').addEventListener('change',extras)}catch(e){}
)JS";

}  // namespace

const std::string& webPage(bool extras) {
    static const auto make = [](bool with) {
        std::string html = PAGE;
        replaceAll(html, "/*DARK*/", DARK_COLOURS);
        replaceAll(html, "/*PIECES*/", pieceStyles());
        // The plain page has nothing where the extras go: the page as it was before them.
        replaceAll(html, "/*EXTRA_STYLES*/", with ? EXTRA_STYLES : "");
        replaceAll(html, "<!--LINKS-->", with ? LINKS : "");
        replaceAll(html, "<!--WIDGET-->", with ? WIDGET : "");
        replaceAll(html, "/*EXTRAS*/", with ? EXTRAS : "");
        replaceAll(html, "/*RENDER*/", with ? ";extras()" : "");
        return html;
    };
    static const std::string plain = make(false), with = make(true);
    return extras ? with : plain;
}

}  // namespace fcg
