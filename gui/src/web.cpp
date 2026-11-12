// The broadcast web page: every game in progress, the standings and the crosstable, read-only; the followed game's
// evaluation bar, arrows and charts; the statistics and anomalies of the games played; their PGN file to download.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <vector>

#include "broadcast.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

// Downloads at once: more are told to come back later.
constexpr int MAX_DOWNLOADS = 2;
// A download sends 64 KiB at a time, each taken within 10 seconds (6.4 KB/s at least), for 30 minutes at most.
constexpr std::size_t CHUNK      = 64 * 1024;
constexpr int CHUNK_MS           = 10000;
constexpr auto MAX_DOWNLOAD_TIME = std::chrono::minutes(30);

std::string response(const std::string& status, const std::string& type, const std::string& body) {
    return "HTTP/1.0 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
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
    // The downloads see running_ and end at once.
    for (auto& d : downloads_) {
        if (d.thread.joinable()) d.thread.join();
    }
    downloads_.clear();
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
        // The downloads that ended.
        for (auto it = downloads_.begin(); it != downloads_.end();) {
            if (*it->done) {
                it->thread.join();
                it = downloads_.erase(it);
            } else {
                ++it;
            }
        }

        const auto fd = listener_.accept(200);
        if (!fd) continue;

        // One request at a time, each within a few seconds in all: a slow or silent client cannot hold the others,
        // and a stop does not wait for it. A download goes on on a thread of its own.
        constexpr int READ_MS = 1000, REQUEST_MS = 10000;
        const auto start   = Clock::now();
        auto connection    = std::make_unique<net::TcpConnection>(*fd, &running_);
        const auto request = connection->readUntil("\r\n\r\n", 8192, READ_MS);
        const auto left    = [&] {
            return REQUEST_MS -
                   static_cast<int>(
                       std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
        };
        const auto end = request.find(' ', 4);
        const auto path =
            request.rfind("GET ", 0) == 0 && end != std::string::npos ? request.substr(4, end - 4) : std::string();
        if (path == "/" || path == "/index.html") {
            connection->sendAll(response("200 OK", "text/html; charset=utf-8", webPage()), left());
        } else if (path.rfind("/state.json", 0) == 0) {
            std::string body;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                body = state_json_;
                // A viewer polls every second: the requests of the last few seconds count the viewers.
                const auto now = Clock::now();
                recent_.push_back(now);
                std::erase_if(recent_, [&](const auto& t) { return now - t > std::chrono::seconds(3); });
                viewers_ = static_cast<int>((recent_.size() + 2) / 3);
            }
            connection->sendAll(response("200 OK", "application/json", body), left());
        } else if (path == "/games.pgn") {
            std::filesystem::path file;
            std::string name;
            uint64_t bytes = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                file  = pgn_file_;
                name  = pgn_name_;
                bytes = pgn_bytes_;
            }
            if (file.empty()) {
                connection->sendAll(response("404 Not Found", "text/plain", "no game played yet"), left());
            } else if (static_cast<int>(downloads_.size()) >= MAX_DOWNLOADS) {
                connection->sendAll(response("503 Service Unavailable", "text/plain", "busy, try again in a moment"),
                                    left());
            } else {
                auto done = std::make_shared<std::atomic_bool>(false);
                std::thread thread([this, c = std::move(connection), file, name, bytes, done]() mutable {
                    download(std::move(c), file, name, bytes);
                    *done = true;
                });
                downloads_.push_back({std::move(thread), std::move(done)});
            }
        } else {
            connection->sendAll(response("404 Not Found", "text/plain", "not found"), left());
        }
    }
}

void WebServer::download(std::unique_ptr<net::TcpConnection> connection, const std::filesystem::path& file,
                         const std::string& name, uint64_t bytes) {
    // The games recorded, read-only: up to the end of the last one, not into one being written.
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    std::ifstream in(file, std::ios::binary);
    if (ec || !in) {
        connection->sendAll(response("404 Not Found", "text/plain", "no PGN file"), CHUNK_MS);
        return;
    }
    bytes = std::min<uint64_t>(bytes, size);
    const auto head =
        "HTTP/1.0 200 OK\r\nContent-Type: application/x-chess-pgn\r\nContent-Length: " + std::to_string(bytes) +
        "\r\nContent-Disposition: " + disposition(name) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (!connection->sendAll(head, CHUNK_MS)) return;

    const auto start = Clock::now();
    std::vector<char> buffer(CHUNK);
    while (bytes > 0 && running_ && Clock::now() - start < MAX_DOWNLOAD_TIME) {
        in.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(bytes, buffer.size())));
        const auto n = in.gcount();
        if (n <= 0) break;
        // A client that does not take a chunk in time is let go.
        if (!connection->sendAll(std::string_view(buffer.data(), static_cast<std::size_t>(n)), CHUNK_MS)) break;
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
.bar{height:4px;border-radius:2px;background:#15171b;overflow:hidden;margin-top:4px}.bar>div{height:100%;background:#e8eaee}
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
.bad{color:var(--loss);font-weight:600}
@media (max-width:700px){.focus{grid-template-columns:1fr}header,main{padding-left:16px;padding-right:16px}}
</style></head><body>
<header><div><h1 id="name">fastchess-gui</h1><div class="muted" id="progress"></div></div><div id="download"></div></header>
<main><section class="card" id="focus" hidden></section>
<section><div class="games" id="games"></div></section>
<section class="card scroll"><table id="table"></table></section>
<section class="two"><div class="card"><h2>Statistics</h2><div id="stats"></div></div>
<div class="card"><h2>Anomalies</h2><div id="anomalies"></div></div></section>
<section class="card"><div class="muted">Latest results</div><div id="recent"></div></section></main>
<script>
const P={k:'♚',q:'♛',r:'♜',b:'♝',n:'♞',p:'♟'};let selected=null,last=null;
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
function evalBar(g){let cp=whiteCp(g);if(cp===null)return 50;cp=Math.max(-1000,Math.min(1000,cp));return 50+50*(2/(1+Math.exp(-cp/250))-1)}
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
 return `<div class="search">d${s.depth} · ${score(cp)} · ${count(s.nodes)} nodes · ${(s.time_ms/1000).toFixed(1)} s${thinks&&s.pv.length?'<br>'+esc(s.pv.slice(0,12).join(' ')):''}</div>`}
function focus(g){const side=(white)=>`<div class="row"><b class="${g.active&&g.white_to_move===white?'tomove':''}">${player(white?g.white:g.black,white?g.white_elo:g.black_elo)}</b><span>${g.clocks?clock(white?g.white_ms:g.black_ms):''}</span></div>${search(g,white)}`;
 return `<div class="focus"><div class="bwrap"><div class="ebar" title="${score(whiteCp(g))}"><div style="height:${evalBar(g)}%"></div></div>
  <div class="board big">${board(g.fen)}<svg class="arrows" viewBox="0 0 8 8">${MARKERS}${arrows(g)}</svg></div></div>
  <div>${side(false)}${side(true)}<div class="row"><span>Game #${g.number} · ${Math.ceil(g.moves.length/2)} moves · ${score(whiteCp(g))}</span><span>${g.active?'':esc(g.result)}</span></div>${charts(g)}</div></div>`}
function stats(s){const t=s.stats;if(!t||!t.games)return '<div class="muted">No game played yet.</div>';
 const pc=x=>`${x} <span class="muted">(${Math.round(100*x/t.games)} %)</span>`,row=(a,b)=>`<tr><td class="n">${a}</td><td>${b}</td></tr>`;
 return '<table>'+row('Games',t.games)+row('White wins (1-0)',pc(t.white_wins))+row('Black wins (0-1)',pc(t.black_wins))+row('Draws',pc(t.draws))+
  row('Average length',Math.round(t.plies/t.games/2)+' moves')+row('Average duration',clock(t.duration_ms/t.games))+row('Adjudicated',pc(t.adjudications))+'</table>'}
const KINDS={time:'time loss',crash:'crash',stall:'no answer',illegal:'illegal move'};
function anomalies(s){const t=s.stats||{},n=v=>v?`<span class="bad">${v}</span>`:'0';
 let html=`<div>Time losses ${n(t.time_losses)} · crashes ${n(t.crashes)} · no answer ${n(t.stalls)} · illegal moves ${n(t.illegal_moves)}</div>`;
 if(!s.anomalies.length)return html+'<div class="muted">None so far.</div>';
 return html+'<table>'+s.anomalies.slice(0,15).map(a=>`<tr title="${esc(a.reason)}"><td>#${a.number}</td><td class="n">${esc(a.white)} – ${esc(a.black)}</td><td>${esc(a.result)}</td><td class="n">${KINDS[a.kind]||esc(a.kind)}${a.engine?' · '+esc(a.engine):''}</td></tr>`).join('')+'</table>'}
function render(s){last=s;
 document.getElementById('name').textContent=s.name||'fastchess-gui';document.title=s.name||'fastchess-gui';
 document.getElementById('progress').textContent=`${s.played}${s.total?' / '+s.total:''} games${s.finished?' · finished':''}`;
 document.getElementById('download').innerHTML=s.pgn?`<a href="games.pgn" download="${esc(s.pgn)}">Download the games (PGN)</a>`:'';
 const g=s.games.find(x=>x.number===selected)||s.games.find(x=>x.active)||s.games[0];
 const f=document.getElementById('focus');f.hidden=!g;f.innerHTML=g?focus(g):'';
 document.getElementById('games').innerHTML=s.games.map(x=>`<div class="card${g&&x.number===g.number?' sel':''}" data-n="${x.number}">
  <div class="row"><b class="${!x.white_to_move&&x.active?'tomove':''}">${player(x.black,x.black_elo)}</b><span class="muted">${x.clocks?clock(x.black_ms):''}</span></div>
  <div class="board">${board(x.fen)}</div>
  <div class="row"><b class="${x.white_to_move&&x.active?'tomove':''}">${player(x.white,x.white_elo)}</b><span class="muted">${x.clocks?clock(x.white_ms):''}</span></div>
  <div class="bar"><div style="width:${evalBar(x)}%"></div></div>
  <div class="row muted"><span>#${x.number} · ${Math.ceil(x.moves.length/2)} moves</span><span>${x.active?'':esc(x.result)}</span></div></div>`).join('')||'<div class="muted">No game in progress.</div>';
 const rows=s.standings;let t='<tr><th>#</th><th class="n">Engine</th><th>Games</th><th>Points</th><th>Elo</th><th>±</th><th>+</th><th>=</th><th>−</th>'+rows.map((_,i)=>`<th>${i+1}</th>`).join('')+'</tr>';
 rows.forEach(r=>{t+=`<tr><td>${r.rank}</td><td class="n">${esc(r.name)}</td><td>${r.games}</td><td>${half(r.points)}</td><td>${r.games?(r.elo>=0?'+':'')+Math.round(r.elo):''}</td><td class="muted">${r.games?Math.round(r.elo_error):''}</td><td>${r.wins}</td><td>${r.draws}</td><td>${r.losses}</td>`+
  r.crosstable.map(c=>c?`<td title="+${c.wins} =${c.draws} -${c.losses}">${c.wins+c.draws+c.losses?half(c.wins+c.draws/2):''}</td>`:'<td class="muted">–</td>').join('')+'</tr>'});
 document.getElementById('table').innerHTML=t;
 document.getElementById('stats').innerHTML=stats(s);
 document.getElementById('anomalies').innerHTML=anomalies(s);
 document.getElementById('recent').innerHTML=s.recent.slice(0,12).map(x=>`<div>${esc(x)}</div>`).join('')}
document.getElementById('games').addEventListener('click',e=>{const c=e.target.closest('[data-n]');if(c){selected=+c.dataset.n;if(last)render(last)}});
async function refresh(){try{render(await (await fetch('state.json',{cache:'no-store'})).json())}
 catch(e){document.getElementById('progress').textContent='Waiting for fastchess-gui…'}}
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";
}

}  // namespace fcg
