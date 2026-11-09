// The broadcast web page: every game in progress, the standings and the crosstable, read-only.

#include <algorithm>

#include "broadcast.hpp"

namespace fcg {

namespace {

std::string response(const std::string& status, const std::string& type, const std::string& body) {
    return "HTTP/1.0 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
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
    listener_.close();
    viewers_ = 0;
}

void WebServer::publish(const BroadcastSnapshot& snapshot) {
    auto json = snapshotJson(snapshot);
    std::lock_guard<std::mutex> lock(mutex_);
    state_json_ = std::move(json);
}

void WebServer::serve() {
    using Clock = std::chrono::steady_clock;
    while (running_) {
        const auto fd = listener_.accept(200);
        if (!fd) continue;

        net::TcpConnection connection(*fd);
        const auto request = connection.readUntil("\r\n\r\n", 8192, 1000);
        const auto end     = request.find(' ', 4);
        const auto path =
            request.rfind("GET ", 0) == 0 && end != std::string::npos ? request.substr(4, end - 4) : std::string();
        if (path == "/" || path == "/index.html") {
            connection.sendAll(response("200 OK", "text/html; charset=utf-8", webPage()));
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
            connection.sendAll(response("200 OK", "application/json", body));
        } else {
            connection.sendAll(response("404 Not Found", "text/plain", "not found"));
        }
    }
}

const char* webPage() {
    return R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>fastchess-gui live</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#1b1e24;--muted:#626976;--line:#e0e3e8;--light:#dee3e6;--dark:#8ca2ad;--accent:#2f6fde;--win:#1d7f4e;--loss:#c03b33}
@media (prefers-color-scheme:dark){:root{--bg:#121418;--card:#1a1d23;--text:#e8eaee;--muted:#a3aab6;--line:#262a32;--accent:#5b9dff;--win:#5fd39a;--loss:#ff8f87}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,sans-serif}
header{padding:16px 24px;border-bottom:1px solid var(--line);background:var(--card)}h1{margin:0;font-size:20px}
.muted{color:var(--muted)}main{padding:20px 24px;display:flex;flex-direction:column;gap:20px}
.games{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px}
.row{display:flex;justify-content:space-between;gap:8px;font-size:13px}.row b{font-weight:600}
.board{display:grid;grid-template-columns:repeat(8,1fr);aspect-ratio:1;border-radius:4px;overflow:hidden;margin:6px 0}
.sq{display:flex;align-items:center;justify-content:center;font-size:min(4.2vw,22px);line-height:1;font-family:"Segoe UI Symbol","DejaVu Sans",sans-serif}
.l{background:var(--light)}.d{background:var(--dark)}.w{color:#fff;text-shadow:0 0 1px #000,0 0 1px #000,0 0 2px #000}.b{color:#15171b}
.bar{height:4px;border-radius:2px;background:#15171b;overflow:hidden;margin-top:4px}.bar>div{height:100%;background:#e8eaee}
.tomove{color:var(--accent)}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{padding:6px 8px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}th{color:var(--muted);font-weight:500}
td.n,th.n{text-align:left}.scroll{overflow-x:auto}
</style></head><body>
<header><h1 id="name">fastchess-gui</h1><div class="muted" id="progress"></div></header>
<main><section><div class="games" id="games"></div></section>
<section class="card scroll"><table id="table"></table></section>
<section class="card"><div class="muted">Latest results</div><div id="recent"></div></section></main>
<script>
const P={k:'♚',q:'♛',r:'♜',b:'♝',n:'♞',p:'♟'};
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function clock(ms){if(ms<0)ms=0;const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
 return h?`${h}:${String(m%60).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`:`${m}:${String(s%60).padStart(2,'0')}`}
function board(fen){let html='',r=0,f=0;for(const ch of fen.split(' ')[0]){if(ch==='/'){r++;f=0;continue}
 if(/\d/.test(ch)){for(let i=0;i<+ch;i++){html+=`<div class="sq ${(r+f)%2?'d':'l'}"></div>`;f++}continue}
 const white=ch===ch.toUpperCase();html+=`<div class="sq ${(r+f)%2?'d':'l'} ${white?'w':'b'}">${P[ch.toLowerCase()]}&#xFE0E;</div>`;f++}return html}
function evalBar(g){const s=g.white_to_move?g.white_search:g.black_search;if(!s||!s.depth)return 50;
 let cp=g.white_to_move?s.score_cp:-s.score_cp;cp=Math.max(-1000,Math.min(1000,cp));return 50+50*(2/(1+Math.exp(-cp/250))-1)}
function half(x){return Number.isInteger(x)?String(x):Math.floor(x)+'½'}
async function refresh(){try{const s=await (await fetch('state.json',{cache:'no-store'})).json();
 document.getElementById('name').textContent=s.name||'fastchess-gui';document.title=s.name||'fastchess-gui';
 document.getElementById('progress').textContent=`${s.played}${s.total?' / '+s.total:''} games${s.finished?' · finished':''}`;
 document.getElementById('games').innerHTML=s.games.map(g=>`<div class="card">
  <div class="row"><b class="${!g.white_to_move&&g.active?'tomove':''}">${esc(g.black)}</b><span class="muted">${g.clocks?clock(g.black_ms):''}</span></div>
  <div class="board">${board(g.fen)}</div>
  <div class="row"><b class="${g.white_to_move&&g.active?'tomove':''}">${esc(g.white)}</b><span class="muted">${g.clocks?clock(g.white_ms):''}</span></div>
  <div class="bar"><div style="width:${evalBar(g)}%"></div></div>
  <div class="row muted"><span>#${g.number} · ${Math.ceil(g.moves.length/2)} moves</span><span>${g.active?'':esc(g.result)}</span></div></div>`).join('')||'<div class="muted">No game in progress.</div>';
 const rows=s.standings;let t='<tr><th>#</th><th class="n">Engine</th><th>Games</th><th>Points</th><th>Elo</th><th>±</th><th>+</th><th>=</th><th>−</th>'+rows.map((_,i)=>`<th>${i+1}</th>`).join('')+'</tr>';
 rows.forEach(r=>{t+=`<tr><td>${r.rank}</td><td class="n">${esc(r.name)}</td><td>${r.games}</td><td>${half(r.points)}</td><td>${r.games?(r.elo>=0?'+':'')+Math.round(r.elo):''}</td><td class="muted">${r.games?Math.round(r.elo_error):''}</td><td>${r.wins}</td><td>${r.draws}</td><td>${r.losses}</td>`+
  r.crosstable.map(c=>c?`<td title="+${c.wins} =${c.draws} -${c.losses}">${c.wins+c.draws+c.losses?half(c.wins+c.draws/2):''}</td>`:'<td class="muted">–</td>').join('')+'</tr>'});
 document.getElementById('table').innerHTML=t;
 document.getElementById('recent').innerHTML=s.recent.slice(0,12).map(x=>`<div>${esc(x)}</div>`).join('');
}catch(e){document.getElementById('progress').textContent='Waiting for fastchess-gui…'}}
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";
}

}  // namespace fcg
