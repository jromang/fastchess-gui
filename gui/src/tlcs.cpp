// A TLCS compatible server. The protocol, as Tom's Live Chess Viewer 2.1 and node-tlcv expect it (node-tlcv's
// docs/protocol.md and docs/official-client.md describe both):
//
//   client -> server   LOGONv15:<name>, PING, ACK: <id>, RESULTTABLE, LOGOFF; the client numbers some of them
//                      ("<id>PING"), which are acknowledged too
//   server -> client   LOGON SUCCESSFUL (not numbered; TLCV ignores everything until it comes), then "<id>" + one
//                      command per datagram, acknowledged with "ACK: <id>"; resent until it is. One numbered command
//                      at a time per client: node-tlcv drops a number lower than one it has, so a command resent
//                      after later ones would be lost to it. Numbered:
//     FEATURE: PING                 (TLCV only sends its keepalive PING when told)
//     SITE: <event>                 WPLAYER: <name>        BPLAYER: <name>
//     FEN: <board> <side> <castling>                       (the initial position resets the game)
//     FEN: <position after the move>, XMOVE: <n>. <san>, FMR: <halfmove clock>    for each move (X = W or B)
//     result: <1-0|0-1|1/2-1/2>
//   and not numbered, nor resent, as TLCS sends them (a lost one is replaced by the next):
//     XTIME: <cs> otim <cs>         when X starts thinking: its clock, then the other one
//     XPV: <depth> <score cp> <time cs> <nodes> <uci moves...>   (mate in N: +-(100000 - N))
//     CTRESET, then CT: <line> for each line of the crosstable, "total games = N", then the games played, newest
//                                   first, under "Game No." (node-tlcv numbers its games from them)
//     PONG                          (answer to PING)
//
// The port may be open to the internet, where the address of a UDP datagram can be forged: until a client proves
// its address by acknowledging the first numbered command (whose number it can only learn by receiving it), it gets
// that command and LOGON SUCCESSFUL only, a few bytes. Logons are limited in number and rate, on each port.
//
// With lanes, as TorsGUI does, every concurrent game is served on a port of its own (slot k on the first port + k),
// each port a server of its own, with its own thread, clients and limits.

#include <chrono>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <thread>

#include <chess.hpp>

#include <fmt/include/fmt/format.h>

#include "broadcast.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t MAX_CLIENTS       = 100;  // viewers in all, on a port
constexpr std::size_t MAX_WAITING       = 20;   // of which have not proved their address yet
constexpr std::size_t LOGONS_PER_SECOND = 10;   // in all, and one per second from an address
constexpr std::size_t MAX_LANES         = 64;   // ports, with lanes

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// The first three fields of a FEN: what TLCS sends.
std::string shortFen(const std::string& fen) {
    std::size_t pos = 0;
    for (int field = 0; field < 3 && pos != std::string::npos; ++field) pos = fen.find(' ', pos + 1);
    return pos == std::string::npos ? fen : fen.substr(0, pos);
}

}  // namespace

TlcsMessage readTlcsMessage(const std::string& datagram) {
    TlcsMessage m;
    auto msg = trim(datagram);
    // A numbered message: acknowledged, then read like the others (TLCV numbers its PING and CHAT).
    if (const auto close = msg.find('>'); !msg.empty() && msg.front() == '<' && close != std::string::npos) {
        m.number = std::strtoul(msg.c_str() + 1, nullptr, 10);
        msg      = trim(msg.substr(close + 1));
    }
    const auto colon = msg.find(':');
    if (msg.starts_with("LOGON")) {
        m.kind = TlcsMessage::Kind::Logon;
        m.name = colon == std::string::npos ? std::string("viewer") : trim(msg.substr(colon + 1));
    } else if (msg.starts_with("ACK")) {
        m.kind = TlcsMessage::Kind::Ack;
        m.ack =
            colon == std::string::npos ? 0 : static_cast<uint32_t>(std::strtoul(msg.c_str() + colon + 1, nullptr, 10));
    } else if (msg == "PING") {
        m.kind = TlcsMessage::Kind::Ping;
    } else if (msg == "LOGOFF") {
        m.kind = TlcsMessage::Kind::Logoff;
    } else if (msg == "RESULTTABLE") {
        m.kind = TlcsMessage::Kind::ResultTable;
    }
    return m;
}

struct TlcsServer::Impl {
    struct Pending {
        uint32_t id = 0;
        std::string text;
        Clock::time_point sent;
        int tries = 0;
    };
    struct Client {
        std::string name;
        uint32_t next_id = 1;
        std::deque<Pending> unacked;  // sent, waiting for their ACK: at most one
        std::deque<Pending> queued;   // not sent yet, in order
        Clock::time_point last_seen;
        Clock::time_point logon_at;
        bool verified     = false;  // has acknowledged `first_id`: the address is the client's
        uint32_t first_id = 0;
    };

    int lane = -1;  // the slot this port follows, -1: the featured game
    std::thread thread;
    std::atomic_int verified{0};  // clients whose address is proved

    net::UdpSocket socket;
    std::map<net::Endpoint, Client> clients;
    std::mt19937 random{std::random_device{}()};
    std::deque<Clock::time_point> logons;  // accepted in the last second

    // Latest snapshot from the app.
    std::mutex mutex;
    BroadcastSnapshot latest;
    int featured = -1;
    bool fresh   = false;

    // What the clients were told.
    std::string site;
    bool has_game = false;
    LiveGame game;  // the game followed
    std::size_t moves_sent = 0;
    int64_t times_sent[2]  = {-1, -1};
    std::string pv_sent[2];
    Clock::time_point pv_at[2];
    bool result_sent      = false;
    uint64_t table_played = ~uint64_t(0);
    std::vector<std::string> table_lines;

    void send(const net::Endpoint& to, Client& c, const std::string& text) {
        c.queued.push_back({c.next_id++, text, {}, 0});
        pump(to, c);
    }

    // The next numbered command, once the previous one is acknowledged (or given up).
    void pump(const net::Endpoint& to, Client& c) {
        if (!c.unacked.empty() || c.queued.empty()) return;
        auto p  = std::move(c.queued.front());
        p.sent  = Clock::now();
        p.tries = 1;
        c.queued.pop_front();
        socket.send(to, fmt::format("<{}>{}", p.id, p.text));
        c.unacked.push_back(std::move(p));
    }

    void broadcast(const std::string& text) {
        for (auto& [to, c] : clients) {
            if (c.verified) send(to, c, text);
        }
    }

    // Not numbered, not resent: a lost one is replaced by the next.
    void broadcastUnnumbered(const std::string& text) {
        for (const auto& [to, c] : clients) {
            if (c.verified) socket.send(to, text);
        }
    }

    [[nodiscard]] int verifiedClients() const {
        int n = 0;
        for (const auto& [to, c] : clients) n += c.verified;
        return n;
    }

    // Everything about the game followed, from its start: for a new game, or a client that just logged on.
    std::vector<std::string> gameFromStart() const {
        std::vector<std::string> out;
        if (!has_game) return out;
        // The players first: node-tlcv names the game's PGN when the initial position comes.
        out.push_back("WPLAYER: " + game.white);
        out.push_back("BPLAYER: " + game.black);
        out.push_back("FEN: " + shortFen(game.start_fen));
        const auto moves = movesFrom(0);
        out.insert(out.end(), moves.begin(), moves.end());
        return out;
    }

    // The clocks, from the side to move: TLCV sets both from it.
    std::string clocks() const {
        const bool white = game.white_to_move;
        return fmt::format("{}: {} otim {}", white ? "WTIME" : "BTIME", (white ? game.white_ms : game.black_ms) / 10,
                           (white ? game.black_ms : game.white_ms) / 10);
    }

    // Moves from index `from` on, each preceded by the position after it, as TLCS does: TLCV draws the board from
    // the last FEN, and node-tlcv reloads it when a move does not fit its board (a game started from a book position).
    std::vector<std::string> movesFrom(std::size_t from) const {
        std::vector<std::string> out;
        chess::Board board;
        board.setFen(game.start_fen);
        for (std::size_t i = 0; i < game.uci.size() && i < game.san.size(); ++i) {
            const bool white  = board.sideToMove() == chess::Color::WHITE;
            const auto number = board.fullMoveNumber();
            const auto move   = chess::uci::uciToMove(board, game.uci[i]);
            if (move == chess::Move::NO_MOVE) break;
            board.makeMove<true>(move);
            if (i >= from) {
                out.push_back("FEN: " + shortFen(board.getFen()));
                out.push_back(fmt::format("{}: {}. {}", white ? "WMOVE" : "BMOVE", number, game.san[i]));
                out.push_back(fmt::format("FMR: {}", board.halfMoveClock()));
            }
        }
        return out;
    }

    // Room for one more logon from `from`, which is counted.
    bool admit(const net::Endpoint& from) {
        const auto now = Clock::now();
        while (!logons.empty() && now - logons.front() >= std::chrono::seconds(1)) logons.pop_front();
        if (logons.size() >= LOGONS_PER_SECOND) return false;
        const auto it = clients.find(from);
        if (it != clients.end() && now - it->second.logon_at < std::chrono::seconds(1)) return false;
        if (it == clients.end()) {
            std::size_t waiting = 0;
            for (const auto& [to, c] : clients) waiting += !c.verified;
            if (clients.size() >= MAX_CLIENTS || waiting >= MAX_WAITING) return false;
        }
        logons.push_back(now);
        return true;
    }

    void logon(const net::Endpoint& from, const std::string& name) {
        if (!admit(from)) return;
        const bool known = clients.count(from) > 0;
        auto& c          = clients[from];
        // The first number cannot be guessed, so that its ACK proves the address.
        if (!known) c.next_id = std::uniform_int_distribution<uint32_t>(1, 100000)(random);
        c.name      = name;
        c.last_seen = c.logon_at = Clock::now();
        socket.send(from, "LOGON SUCCESSFUL");
        if (!c.verified) {
            c.unacked.clear();
            c.queued.clear();
            c.first_id = c.next_id;
        }
        send(from, c, "FEATURE: PING");
        if (c.verified) welcome(from, c);
    }

    // Everything there is to know, for a client whose address is proved.
    void welcome(const net::Endpoint& from, Client& c) {
        if (!site.empty()) send(from, c, "SITE: " + site);
        if (!table_lines.empty()) {
            socket.send(from, "CTRESET");
            for (const auto& line : table_lines) socket.send(from, "CT: " + line);
        }
        for (const auto& m : gameFromStart()) send(from, c, m);
        if (has_game && game.clocks) socket.send(from, clocks());
        if (result_sent) send(from, c, "result: " + game.result);
    }

    void handle(const net::Endpoint& from, const std::string& raw) {
        using Kind     = TlcsMessage::Kind;
        const auto msg = readTlcsMessage(raw);
        if (msg.kind == Kind::Logon) logon(from, msg.name);
        // Clients only: anyone else gets no answer at all.
        const auto it = clients.find(from);
        if (it == clients.end()) return;
        if (msg.number > 0) socket.send(from, fmt::format("ACK: {}", msg.number));
        if (msg.kind == Kind::Logon) return;
        auto& c     = it->second;
        c.last_seen = Clock::now();

        if (msg.kind == Kind::Ack) {
            const auto id = msg.ack;
            auto& q       = c.unacked;
            for (auto p = q.begin(); p != q.end(); ++p) {
                if (p->id == id) {
                    q.erase(p);
                    break;
                }
            }
            if (!c.verified && id == c.first_id) {
                c.verified = true;
                welcome(from, c);
            }
            pump(from, c);
        } else if (msg.kind == Kind::Ping) {
            socket.send(from, "PONG");
        } else if (msg.kind == Kind::Logoff) {
            clients.erase(it);
        } else if (msg.kind == Kind::ResultTable && c.verified) {
            socket.send(from, "CTRESET");
            for (const auto& line : table_lines) socket.send(from, "CT: " + line);
        }
    }

    // Sends what changed since the last snapshot.
    void update(const BroadcastSnapshot& s, int slot) {
        if (s.name != site) {
            site = s.name;
            broadcast("SITE: " + site);
        }

        // The game to follow: the one asked for, else the current one until its result is told (it may already have
        // left its slot for the next game: then it is among the ended ones), else any game in progress. A lane's
        // games are those of its slot only.
        const auto mine      = [&](const LiveGame& g) { return lane < 0 || g.slot == lane; };
        const LiveGame* next = nullptr;
        for (const auto& g : s.games) {
            if (g.slot == slot && g.active) next = &g;
        }
        if (!next && has_game && !result_sent) {
            for (const auto* list : {&s.games, &s.ended}) {
                for (const auto& g : *list) {
                    if (!next && g.number == game.number) next = &g;
                }
            }
        }
        if (!next) {
            for (const auto& g : s.games) {
                if (g.active && mine(g)) {
                    next = &g;
                    break;
                }
            }
        }
        // Nothing in progress (the end of the tournament) and the game followed was not told to its end: a game
        // that ended, so that viewers see the last result.
        if (!next && (!has_game || !result_sent)) {
            for (const auto& g : s.games) {
                if (!g.result.empty() && mine(g)) {
                    next = &g;
                    break;
                }
            }
        }

        if (next) {
            const bool new_game = !has_game || next->number != game.number;
            game                = *next;
            has_game            = true;
            if (new_game) {
                moves_sent    = game.san.size();
                result_sent   = false;
                times_sent[0] = times_sent[1] = -1;
                pv_sent[0].clear();
                pv_sent[1].clear();
                for (const auto& m : gameFromStart()) broadcast(m);
            } else if (game.san.size() > moves_sent) {
                for (const auto& m : movesFrom(moves_sent)) broadcast(m);
                moves_sent    = game.san.size();
                times_sent[0] = times_sent[1] = -1;  // with each move, the clocks again
            }

            // The clocks when a side starts thinking.
            if (game.clocks && times_sent[0] < 0) {
                broadcastUnnumbered(clocks());
                times_sent[0] = times_sent[1] = 0;
            }

            // The searching side's PV, at most once a second.
            const int side = game.white_to_move ? 0 : 1;
            const auto& sr = game.search[side];
            if (game.active && sr.depth > 0 && Clock::now() - pv_at[side] > std::chrono::seconds(1)) {
                std::string pv;
                for (const auto& m : sr.pv) pv += " " + m;
                const auto text = fmt::format("{}: {} {} {} {}{}", side == 0 ? "WPV" : "BPV", sr.depth, sr.score_cp,
                                              sr.time_ms / 10, sr.nodes, pv);
                if (text != pv_sent[side]) {
                    broadcastUnnumbered(text);
                    pv_sent[side] = text;
                    pv_at[side]   = Clock::now();
                }
            }

            if (!game.active && !game.result.empty() && !result_sent) {
                broadcast("result: " + game.result);
                result_sent = true;
            }
        }

        if (s.played != table_played && !s.table.rows.empty()) {
            table_played = s.played;
            table_lines  = tlcsCrosstable(s, s.played);
            broadcastUnnumbered("CTRESET");
            for (const auto& line : table_lines) broadcastUnnumbered("CT: " + line);
        }
    }

    void resendAndExpire() {
        const auto now = Clock::now();
        for (auto it = clients.begin(); it != clients.end();) {
            auto& c = it->second;
            // A client that says nothing for two minutes (they PING every few seconds) is gone, one that does not
            // prove its address in ten seconds too.
            if (now - c.last_seen > std::chrono::minutes(2) ||
                (!c.verified && now - c.logon_at > std::chrono::seconds(10))) {
                it = clients.erase(it);
                continue;
            }
            // The command waiting for its ACK: sent again every second, given up after 8 tries (3 before the
            // address is proved).
            if (!c.unacked.empty() && now - c.unacked.front().sent >= std::chrono::seconds(1)) {
                auto& p = c.unacked.front();
                if (p.tries >= (c.verified ? 8 : 3)) {
                    c.unacked.pop_front();
                    pump(it->first, c);
                } else {
                    socket.send(it->first, fmt::format("<{}>{}", p.id, p.text));
                    p.sent = now;
                    p.tries++;
                }
            }
            ++it;
        }
    }

    // The server's loop, until `running` turns false.
    void run(const std::atomic_bool& running) {
        while (running) {
            net::Endpoint from;
            if (const auto msg = socket.receive(from, 100)) handle(from, *msg);

            std::optional<BroadcastSnapshot> snapshot;
            int follow = -1;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (fresh) {
                    snapshot = latest;
                    follow   = lane >= 0 ? lane : featured;
                    fresh    = false;
                }
            }
            if (snapshot) update(*snapshot, follow);
            resendAndExpire();
            verified = verifiedClients();
        }
    }
};

TlcsServer::TlcsServer() = default;

TlcsServer::~TlcsServer() { stop(); }

std::string TlcsServer::open(uint16_t port, int lane) {
    auto impl  = std::make_unique<Impl>();
    impl->lane = lane;
    if (auto error = impl->socket.bind(port); !error.empty()) return error;
    impl->thread = std::thread([this, p = impl.get()] { p->run(running_); });
    impls_.push_back(std::move(impl));
    return {};
}

std::string TlcsServer::start(uint16_t port, bool lanes) {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    port_    = port;
    lanes_   = lanes;
    running_ = true;
    if (auto error = open(port, lanes ? 0 : -1); !error.empty()) {
        running_ = false;
        return error;
    }
    return {};
}

void TlcsServer::stop() {
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& impl : impls_) {
        if (impl->thread.joinable()) impl->thread.join();
        impl->socket.close();
    }
    impls_.clear();
    lane_error_.clear();
}

void TlcsServer::publish(const BroadcastSnapshot& snapshot, int featured) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    if (lanes_) {
        // A port for every slot that has had a game, opened in order; one that cannot be is not tried again.
        std::size_t slots = 0;
        for (const auto* list : {&snapshot.games, &snapshot.ended}) {
            for (const auto& g : *list) slots = std::max(slots, static_cast<std::size_t>(g.slot) + 1);
        }
        while (impls_.size() < std::min(slots, MAX_LANES) && lane_error_.empty()) {
            const auto lane = impls_.size();
            if (port_ + lane > 65535) {
                lane_error_ = fmt::format("no UDP port after {}", port_ + lane - 1);
                break;
            }
            lane_error_ = open(static_cast<uint16_t>(port_ + lane), static_cast<int>(lane));
        }
    }
    for (auto& impl : impls_) {
        std::lock_guard<std::mutex> impl_lock(impl->mutex);
        impl->latest   = snapshot;
        impl->featured = featured;
        impl->fresh    = true;
    }
}

int TlcsServer::clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (const auto& impl : impls_) n += impl->verified;
    return n;
}

std::vector<uint16_t> TlcsServer::ports() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint16_t> out;
    for (std::size_t i = 0; i < impls_.size(); ++i) out.push_back(static_cast<uint16_t>(port_ + i));
    return out;
}

std::string TlcsServer::laneError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lane_error_;
}

}  // namespace fcg
