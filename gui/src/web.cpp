// The broadcast web page: every game in progress, an evaluation bar beside each player, the standings and the
// crosstable, read-only; the followed game's evaluation bar, moves, arrows and charts; the moves in letters or
// figurines; the next games of the schedule; the statistics and anomalies of the games played, and each engine's
// search; their PGN file to download.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <vector>

#include "broadcast.hpp"

namespace fcg {

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

// Every answer: never kept by a cache or a proxy (a tunnel), its type taken as given, no address sent on from the
// page.
constexpr const char* COMMON_HEADERS =
    "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
    "Connection: close\r\n";

// The page loads nothing from elsewhere and sends nothing elsewhere: its own script and styles, and state.json.
constexpr const char* PAGE_POLICY =
    "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
    "connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'none'\r\n";

std::string response(const std::string& status, const std::string& type, const std::string& body,
                     const std::string& headers = "Access-Control-Allow-Origin: *\r\n") {
    return "HTTP/1.0 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n" + headers + COMMON_HEADERS + "\r\n" + body;
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
    std::lock_guard<std::mutex> lock(mutex_);
    state_json_ = std::move(json);
    pgn_file_   = snapshot.pgn_file;
    pgn_name_   = snapshot.pgn_name;
    pgn_bytes_  = snapshot.pgn_bytes;
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

    if (path == "/" || path == "/index.html") {
        send(response("200 OK", "text/html; charset=utf-8", webPage(), PAGE_POLICY));
    } else if (path == "/state.json") {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            body = state_json_;
            // A viewer polls every second: the requests of the last few seconds count the viewers, whatever their
            // address (all the same behind a tunnel).
            const auto now = Clock::now();
            recent_.push_back(now);
            std::erase_if(recent_, [&](const auto& t) { return now - t > std::chrono::seconds(3); });
            viewers_ = static_cast<int>((recent_.size() + 2) / 3);
        }
        send(response("200 OK", "application/json", body));
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

const char* webPage() {
    return R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>fastchess-gui live</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#1b1e24;--muted:#626976;--line:#e0e3e8;--light:#dee3e6;--dark:#8ca2ad;--accent:#2f6fde;--win:#1d7f4e;--loss:#c03b33;--ew:#6ea8de;--eb:#ed994d}
@media (prefers-color-scheme:dark){:root{--bg:#121418;--card:#1a1d23;--text:#e8eaee;--muted:#a3aab6;--line:#262a32;--accent:#5b9dff;--win:#5fd39a;--loss:#ff8f87}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,sans-serif}
header{padding:16px 24px;border-bottom:1px solid var(--line);background:var(--card);display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}
h1{margin:0;font-size:20px}h2{margin:0 0 8px;font-size:15px;font-weight:600}a{color:var(--accent)}
.muted{color:var(--muted)}main{padding:20px 24px;display:flex;flex-direction:column;gap:20px}
.games{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px}
.games .card{cursor:pointer}.games .card.sel{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent)}
.row{display:flex;justify-content:space-between;gap:8px;font-size:13px}.row b{font-weight:600}
.board{position:relative;display:grid;grid-template-columns:repeat(8,1fr);aspect-ratio:1;border-radius:4px;overflow:hidden;margin:6px 0}
.sq{display:flex;align-items:center;justify-content:center;font-size:min(4.2vw,22px);line-height:1;font-family:"Segoe UI Symbol","DejaVu Sans",sans-serif}
.big .sq{font-size:min(8.5vw,44px)}
.l{background:var(--light)}.d{background:var(--dark)}.w{color:#fff;text-shadow:0 0 1px #000,0 0 1px #000,0 0 2px #000}.b{color:#15171b}
.arrows{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.side{display:flex;align-items:center;gap:6px;flex:none}
.pbar{display:inline-block;width:40px;height:6px;border-radius:3px;background:#15171b;overflow:hidden;border:1px solid var(--line)}
.pbar>span{display:block;height:100%;background:#e8eaee}.pbar.none{opacity:.3}
.moves{max-height:7.6em;overflow-y:auto;font-size:13px;margin:4px 0 10px;overflow-wrap:anywhere}.mn{color:var(--muted);margin-right:3px}
.tools{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
button{font:inherit;font-size:13px;color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:4px 10px;cursor:pointer}
.focus{display:grid;grid-template-columns:minmax(240px,460px) minmax(0,1fr);gap:20px}
.bwrap{display:flex;gap:8px;align-items:stretch}.bwrap .board{flex:1;margin:0}
.ebar{width:16px;border-radius:3px;background:#15171b;position:relative;overflow:hidden;border:1px solid var(--line)}
.ebar>div{position:absolute;left:0;right:0;bottom:0;background:#e8eaee}
.search{font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums;overflow-wrap:anywhere;margin:2px 0 8px}
.charts{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px;margin-top:10px}
.chart{width:100%;height:70px;display:block;background:var(--bg);border-radius:4px}.chart .axis{stroke:var(--muted);stroke-opacity:.5;stroke-width:1}
.key{display:inline-block;width:10px;height:3px;border-radius:2px;vertical-align:middle;margin:0 3px 0 8px}
.tomove{color:var(--accent)}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{padding:6px 8px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}th{color:var(--muted);font-weight:500}
td.n,th.n{text-align:left}.scroll{overflow-x:auto}.two{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:20px}
.bad{color:var(--loss);font-weight:600}td.wrap{white-space:normal}#next table{width:auto}
@media (max-width:700px){.focus{grid-template-columns:1fr}header,main{padding-left:16px;padding-right:16px}}
</style></head><body>
<header><div><h1 id="name">fastchess-gui</h1><div class="muted" id="progress"></div></div><div class="tools"><button id="notation" type="button"></button><span id="download"></span></div></header>
<main><section class="card" id="focus" hidden></section>
<section><div class="games" id="games"></div></section>
<section class="card scroll" id="nextcard" hidden><h2>Next games</h2><div id="next"></div></section>
<section class="card scroll"><table id="table"></table></section>
<section class="card scroll"><h2>Search per engine</h2><div class="muted">On average per move searched, book moves left out.</div><div id="engines"></div></section>
<section class="two"><div class="card"><h2>Statistics</h2><div id="stats"></div></div>
<div class="card"><h2>Anomalies</h2><div id="anomalies"></div></div></section>
<section class="card"><div class="muted">Latest results</div><div id="recent"></div></section></main>
<script>
const P={k:'♚',q:'♛',r:'♜',b:'♝',n:'♞',p:'♟'};let selected=null,last=null;
// Moves in letters (Nf3) or figurines (♘f3), as the viewer last chose; the browser may keep nothing.
const FIG={K:'♔',Q:'♕',R:'♖',B:'♗',N:'♘'};let figurines=false;try{figurines=localStorage.getItem('fcg-figurines')==='1'}catch(e){}
function san(m){return esc(figurines?m.replace(/[KQRBN]/g,c=>FIG[c]+'\uFE0E'):m)}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function clock(ms){if(ms<0)ms=0;const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
 return h?`${h}:${String(m%60).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`:`${m}:${String(s%60).padStart(2,'0')}`}
function count(n){return n>=1e9?(n/1e9).toFixed(1)+'G':n>=1e6?(n/1e6).toFixed(1)+'M':n>=1e3?(n/1e3).toFixed(1)+'k':String(n)}
function board(fen){let html='',r=0,f=0;for(const ch of fen.split(' ')[0]){if(ch==='/'){r++;f=0;continue}
 if(/\d/.test(ch)){for(let i=0;i<+ch;i++){html+=`<div class="sq ${(r+f)%2?'d':'l'}"></div>`;f++}continue}
 const white=ch===ch.toUpperCase();html+=`<div class="sq ${(r+f)%2?'d':'l'} ${white?'w':'b'}">${P[ch.toLowerCase()]||''}&#xFE0E;</div>`;f++}return html}
// White's evaluation now: the side to move's search, else the last one.
function whiteCp(g){const s=g.white_to_move?g.white_search:g.black_search;if(s&&s.depth)return g.white_to_move?s.score_cp:-s.score_cp;
 for(let i=g.evals.length-1;i>=0;i--)if(g.evals[i]!==null)return g.evals[i];return null}
// White's share of a bar, from White's evaluation.
function share(cp){if(cp===null)return 50;cp=Math.max(-1000,Math.min(1000,cp));return 50+50*(2/(1+Math.exp(-cp/250))-1)}
function evalBar(g){return share(whiteCp(g))}
// A player's own evaluation, from its last search, as a small bar of White's share.
function pbar(g,white){const s=white?g.white_search:g.black_search;if(!s||!s.depth)return '<span class="pbar none" title="–"><span style="width:50%"></span></span>';
 const cp=white?s.score_cp:-s.score_cp;return `<span class="pbar" title="${score(cp)}"><span style="width:${share(cp)}%"></span></span>`}
// The moves of a game, numbered from its start position.
function moveList(g){const f=g.start_fen.split(' ');let white=(f[1]||'w')==='w',n=+f[5]||1,out='';
 g.moves.forEach((m,i)=>{if(white)out+=`<span class="mn">${n}.</span>`;else if(!i)out+=`<span class="mn">${n}…</span>`;out+=san(m)+' ';if(!white)n++;white=!white});
 return out||'<span class="muted">No move yet.</span>'}
function score(cp){if(cp===null||cp===undefined)return '–';if(Math.abs(cp)>=90000){const n=100000-Math.abs(cp);return (cp>0?'#':'-#')+n}
 return (cp>=0?'+':'')+(cp/100).toFixed(2)}
function pawns(cp){return Math.max(-8,Math.min(8,Math.abs(cp)>=90000?Math.sign(cp)*8:cp/100))}
function player(name,elo){return esc(name)+(elo?` <span class="muted">${elo}</span>`:'')}
function half(x){return Number.isInteger(x)?String(x):Math.floor(x)+'½'}
// Squares as board coordinates, a8 at the top left.
function at(sq){return [sq.charCodeAt(0)-96.5,8.5-(+sq[1])]}
function arrow(uci,side,opacity){if(!/^[a-h][1-8][a-h][1-8]/.test(uci))return '';const [x1,y1]=at(uci.slice(0,2)),[x2,y2]=at(uci.slice(2,4));
 const dx=x2-x1,dy=y2-y1,l=Math.hypot(dx,dy),k=(l-.35)/l;
 return `<g opacity="${opacity}"><line x1="${x1}" y1="${y1}" x2="${x1+dx*k}" y2="${y1+dy*k}" style="stroke:var(--e${side})" stroke-width=".17" marker-end="url(#h${side})"/></g>`}
// The move the side to move thinks of, and the reply it expects, in the colours of the engines that would play them.
function arrows(g){const s=g.white_to_move?g.white_search:g.black_search;if(!g.active||!s||!s.pv||!s.pv.length)return '';
 const me=g.white_to_move?'w':'b',other=g.white_to_move?'b':'w';return (s.pv.length>1?arrow(s.pv[1],other,.6):'')+arrow(s.pv[0],me,.85)}
const MARKERS='<defs>'+['w','b'].map(c=>`<marker id="h${c}" viewBox="0 0 10 10" refX="4" refY="5" markerWidth="2.6" markerHeight="2.6" orient="auto"><path d="M0,0L10,5L0,10z" style="fill:var(--e${c})"/></marker>`).join('')+'</defs>';
// A small line chart: series of [ply, value], plies on the x axis.
function chart(title,series,n,min,max,zero,unit){const W=300,H=70,y=v=>H-2-(v-min)/(max-min||1)*(H-4),x=i=>i/Math.max(20,n)*W;
 let svg=`<svg class="chart" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">`;
 if(zero)svg+=`<line class="axis" x1="0" x2="${W}" y1="${y(0)}" y2="${y(0)}" vector-effect="non-scaling-stroke"/>`;
 for(const s of series)if(s.pts.length)svg+=`<polyline fill="none" style="stroke:${s.color}" stroke-width="1.5" vector-effect="non-scaling-stroke" points="${s.pts.map(([i,v])=>x(i).toFixed(1)+','+y(v).toFixed(1)).join(' ')}"/>`;
 return `<div><div class="row muted"><span>${title}</span><span>${unit}</span></div>${svg}</svg></div>`}
function charts(g){const wf=(g.start_fen.split(' ')[1]||'w')==='w',n=g.moves.length,white=i=>(i%2===0)===wf;
 const ev=[],dw=[],db=[],tw=[],tb=[];
 g.evals.forEach((v,i)=>{if(v!==null)ev.push([i+1,pawns(v)])});
 g.depths.forEach((v,i)=>{if(v!==null)(white(i)?dw:db).push([i+1,v])});
 g.times_ms.forEach((v,i)=>{if(v!==null)(white(i)?tw:tb).push([i+1,v/1000])});
 const top=a=>Math.max(1,...a.map(p=>p[1]));
 const keys=`<span class="key" style="background:var(--ew)"></span>${esc(g.white)}<span class="key" style="background:var(--eb)"></span>${esc(g.black)}`;
 return `<div class="row muted" style="margin-top:10px"><span>Charts of the game</span><span>${keys}</span></div><div class="charts">`+
  chart('Evaluation',[{pts:ev,color:'var(--accent)'}],n,-8.5,8.5,true,'±8 pawns, White')+
  chart('Depth',[{pts:dw,color:'var(--ew)'},{pts:db,color:'var(--eb)'}],n,0,top(dw.concat(db)),false,'up to '+top(dw.concat(db)))+
  chart('Time',[{pts:tw,color:'var(--ew)'},{pts:tb,color:'var(--eb)'}],n,0,top(tw.concat(tb)),false,'up to '+top(tw.concat(tb)).toFixed(1)+' s')+'</div>'}
function search(g,white){const s=white?g.white_search:g.black_search;if(!s||!s.depth)return '<div class="search">–</div>';
 const cp=white?s.score_cp:-s.score_cp,thinks=g.active&&g.white_to_move===white;
 return `<div class="search">d${s.depth} · ${score(cp)} · ${count(s.nodes)} nodes · ${(s.time_ms/1000).toFixed(1)} s${thinks&&s.pv.length?'<br>'+(s.pv_san?san(s.pv_san):esc(s.pv.slice(0,12).join(' '))):''}</div>`}
function focus(g){const side=(white)=>`<div class="row"><b class="${g.active&&g.white_to_move===white?'tomove':''}">${player(white?g.white:g.black,white?g.white_elo:g.black_elo)}</b><span class="side">${pbar(g,white)}<span>${g.clocks?clock(white?g.white_ms:g.black_ms):''}</span></span></div>${search(g,white)}`;
 return `<div class="focus"><div class="bwrap"><div class="ebar" title="${score(whiteCp(g))}"><div style="height:${evalBar(g)}%"></div></div>
  <div class="board big">${board(g.fen)}<svg class="arrows" viewBox="0 0 8 8">${MARKERS}${arrows(g)}</svg></div></div>
  <div>${side(false)}${side(true)}<div class="row"><span>Game #${g.number} · ${Math.ceil(g.moves.length/2)} moves · ${score(whiteCp(g))}</span><span>${g.active?'':esc(g.result)}</span></div><div class="moves" id="moves">${moveList(g)}</div>${charts(g)}</div></div>`}
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
 return '<table><tr><th class="n">Engine</th><th>Depth</th><th>Time</th><th>Nodes</th><th>Speed</th><th class="n">Anomalies</th></tr>'+e.map(x=>{const b=bad(x);
  return `<tr><td class="n">${esc(x.name)}</td><td>${v(x.depth,d=>d.toFixed(1))}</td><td>${v(x.time_ms,t=>(t/1000).toFixed(2)+' s')}</td><td>${v(x.nodes,count)}</td>`+
   `<td>${v(x.nps,n=>count(n)+'/s')}</td><td class="n">${b?`<span class="bad">${b}</span>`:'<span class="muted">none</span>'}</td></tr>`}).join('')+'</table>'}
// The next games of the schedule; a Swiss or a knockout pairs its rounds as it goes.
function upcoming(s){const n=s.next||[],asItGoes=s.format==='swiss'||s.format==='knockout',cycles=!asItGoes&&n.some(x=>x.cycle>1);
 let html=n.length?'<table>'+n.map(x=>`<tr><td>#${x.number}</td><td class="n">${cycles?'Cycle '+x.cycle+' · ':''}Round ${x.round}</td>`+
  `<td class="n wrap">${player(x.white)} – ${player(x.black)}${x.opening?`<div class="muted">${esc(x.opening)}</div>`:''}</td></tr>`).join('')+'</table>':'';
 if(asItGoes)html+=`<div class="muted">${n.length?'The later rounds are':'The next round is'} paired as the tournament goes, from the results: their games are not known in advance.</div>`;
 return html}
function render(s){last=s;const notation=document.getElementById('notation');notation.textContent='Moves: '+(figurines?'♘\uFE0Ef3':'Nf3');
 // The move list stays where the viewer scrolled it, at its end while the game goes on.
 const before=document.getElementById('moves'),shown=before&&before.dataset.n,atEnd=!before||before.scrollTop+before.clientHeight>=before.scrollHeight-4,top=before?before.scrollTop:0;
 document.getElementById('name').textContent=s.name||'fastchess-gui';document.title=s.name||'fastchess-gui';
 document.getElementById('progress').textContent=`${s.played}${s.total?' / '+s.total:''} games${s.finished?' · finished':''}`;
 document.getElementById('download').innerHTML=s.pgn?`<a href="games.pgn" download="${esc(s.pgn)}">Download the games (PGN)</a>`:'';
 const g=s.games.find(x=>x.number===selected)||s.games.find(x=>x.active)||s.games[0];
 const f=document.getElementById('focus');f.hidden=!g;f.innerHTML=g?focus(g):'';
 const moves=document.getElementById('moves');if(moves){moves.dataset.n=g.number;moves.scrollTop=atEnd||shown!==String(g.number)?moves.scrollHeight:top}
 document.getElementById('games').innerHTML=s.games.map(x=>`<div class="card${g&&x.number===g.number?' sel':''}" data-n="${x.number}">
  <div class="row"><b class="${!x.white_to_move&&x.active?'tomove':''}">${player(x.black,x.black_elo)}</b><span class="side">${pbar(x,false)}<span class="muted">${x.clocks?clock(x.black_ms):''}</span></span></div>
  <div class="board">${board(x.fen)}</div>
  <div class="row"><b class="${x.white_to_move&&x.active?'tomove':''}">${player(x.white,x.white_elo)}</b><span class="side">${pbar(x,true)}<span class="muted">${x.clocks?clock(x.white_ms):''}</span></span></div>
  <div class="row muted"><span>#${x.number} · ${Math.ceil(x.moves.length/2)} moves</span><span>${x.active?'':esc(x.result)}</span></div></div>`).join('')||'<div class="muted">No game in progress.</div>';
 const rows=s.standings;let t='<tr><th>#</th><th class="n">Engine</th><th>Games</th><th>Points</th><th>Elo</th><th>±</th><th>+</th><th>=</th><th>−</th>'+rows.map((_,i)=>`<th>${i+1}</th>`).join('')+'</tr>';
 rows.forEach(r=>{t+=`<tr><td>${r.rank}</td><td class="n">${esc(r.name)}</td><td>${r.games}</td><td>${half(r.points)}</td><td>${r.games?(r.elo>=0?'+':'')+Math.round(r.elo):''}</td><td class="muted">${r.games?Math.round(r.elo_error):''}</td><td>${r.wins}</td><td>${r.draws}</td><td>${r.losses}</td>`+
  r.crosstable.map(c=>c?`<td title="+${c.wins} =${c.draws} -${c.losses}">${c.wins+c.draws+c.losses?half(c.wins+c.draws/2):''}</td>`:'<td class="muted">–</td>').join('')+'</tr>'});
 document.getElementById('table').innerHTML=t;
 const nx=document.getElementById('nextcard'),soon=upcoming(s);nx.hidden=s.finished||!soon;document.getElementById('next').innerHTML=soon;
 document.getElementById('engines').innerHTML=engines(s);
 document.getElementById('stats').innerHTML=stats(s);
 document.getElementById('anomalies').innerHTML=anomalies(s);
 document.getElementById('recent').innerHTML=s.recent.slice(0,12).map(x=>`<div>${esc(x)}</div>`).join('')}
document.getElementById('notation').addEventListener('click',()=>{figurines=!figurines;
 try{localStorage.setItem('fcg-figurines',figurines?'1':'0')}catch(e){}if(last)render(last)});
document.getElementById('games').addEventListener('click',e=>{const c=e.target.closest('[data-n]');if(c){selected=+c.dataset.n;if(last)render(last)}});
async function refresh(){try{render(await (await fetch('state.json',{cache:'no-store'})).json())}
 catch(e){document.getElementById('progress').textContent='Waiting for fastchess-gui…'}}
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";
}

}  // namespace fcg
