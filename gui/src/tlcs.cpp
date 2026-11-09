// A TLCS compatible server. The protocol, as Tom's Live Chess Viewer 2.1 and node-tlcv expect it (node-tlcv's
// docs/protocol.md and docs/official-client.md describe both):
//
//   client -> server   LOGONv15:<name>, PING, ACK: <id>, RESULTTABLE, LOGOFF; the client numbers some of them
//                      ("<id>PING"), which are acknowledged too
//   server -> client   LOGON SUCCESSFUL (not numbered; TLCV ignores everything until it comes), then "<id>" + one
//                      command per datagram, acknowledged with "ACK: <id>"; resent until it is:
//     FEATURE: PING                 (TLCV only sends its keepalive PING when told)
//     SITE: <event>                 WPLAYER: <name>        BPLAYER: <name>
//     FEN: <board> <side> <castling>                       (the initial position resets the game)
//     XTIME: <cs> otim <cs>         when X starts thinking: its clock, then the other one (X = W or B)
//     XPV: <depth> <score cp> <time cs> <nodes> <uci moves...>   (mate in N: +-(100000 - N))
//     FEN: <position after the move>, XMOVE: <n>. <san>, FMR: <halfmove clock>    for each move
//     result: <1-0|0-1|1/2-1/2>
//     CTRESET, then CT: <line> for each line of the crosstable, ending with "total games = N"
//     PONG                          (answer to PING, not numbered)

#include <chrono>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>

#include <chess.hpp>

#include <fmt/include/fmt/format.h>

#include "broadcast.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

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
        std::deque<Pending> unacked;
        Clock::time_point last_seen;
    };

    net::UdpSocket socket;
    std::map<net::Endpoint, Client> clients;

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
        Pending p{c.next_id++, text, Clock::now(), 1};
        socket.send(to, fmt::format("<{}>{}", p.id, p.text));
        c.unacked.push_back(std::move(p));
    }

    void broadcast(const std::string& text) {
        for (auto& [to, c] : clients) send(to, c, text);
    }

    // Everything about the game followed, from its start: for a new game, or a client that just logged on.
    std::vector<std::string> gameFromStart() const {
        std::vector<std::string> out;
        if (!has_game) return out;
        out.push_back("FEN: " + shortFen(game.start_fen));
        out.push_back("WPLAYER: " + game.white);
        out.push_back("BPLAYER: " + game.black);
        const auto moves = movesFrom(0);
        out.insert(out.end(), moves.begin(), moves.end());
        if (game.clocks) out.push_back(clocks());
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

    void logon(const net::Endpoint& from, const std::string& name) {
        auto& c     = clients[from];
        c.name      = name;
        c.last_seen = Clock::now();
        socket.send(from, "LOGON SUCCESSFUL");
        send(from, c, "FEATURE: PING");
        if (!site.empty()) send(from, c, "SITE: " + site);
        if (!table_lines.empty()) {
            send(from, c, "CTRESET");
            for (const auto& line : table_lines) send(from, c, "CT: " + line);
        }
        for (const auto& m : gameFromStart()) send(from, c, m);
        if (result_sent) send(from, c, "result: " + game.result);
    }

    void handle(const net::Endpoint& from, const std::string& raw) {
        auto msg = trim(raw);
        // A numbered message: acknowledged, then read like the others (TLCV numbers its PING and CHAT).
        if (const auto close = msg.find('>'); !msg.empty() && msg.front() == '<' && close != std::string::npos) {
            const auto id = std::strtoul(msg.c_str() + 1, nullptr, 10);
            if (id > 0) socket.send(from, fmt::format("ACK: {}", id));
            msg = trim(msg.substr(close + 1));
        }
        if (msg.rfind("LOGON", 0) == 0) {
            const auto colon = msg.find(':');
            logon(from, colon == std::string::npos ? std::string("viewer") : trim(msg.substr(colon + 1)));
            return;
        }
        const auto it = clients.find(from);
        if (it == clients.end()) return;
        it->second.last_seen = Clock::now();

        if (msg.rfind("ACK", 0) == 0) {
            const auto colon = msg.find(':');
            const auto id    = static_cast<uint32_t>(std::strtoul(msg.c_str() + (colon + 1), nullptr, 10));
            auto& q          = it->second.unacked;
            for (auto p = q.begin(); p != q.end(); ++p) {
                if (p->id == id) {
                    q.erase(p);
                    break;
                }
            }
        } else if (msg == "PING") {
            socket.send(from, "PONG");
        } else if (msg == "LOGOFF") {
            clients.erase(it);
        } else if (msg == "RESULTTABLE") {
            send(from, it->second, "CTRESET");
            for (const auto& line : table_lines) send(from, it->second, "CT: " + line);
        }
    }

    // Sends what changed since the last snapshot.
    void update(const BroadcastSnapshot& s, int slot) {
        if (s.name != site) {
            site = s.name;
            broadcast("SITE: " + site);
        }

        // The game to follow: the one asked for, else the current one until its result is told (it may already have
        // left its slot for the next game: then it is among the ended ones), else any game in progress.
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
                if (g.active) {
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
                broadcast(clocks());
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
                    broadcast(text);
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
            broadcast("CTRESET");
            for (const auto& line : table_lines) broadcast("CT: " + line);
        }
    }

    void resendAndExpire() {
        const auto now = Clock::now();
        for (auto it = clients.begin(); it != clients.end();) {
            auto& c = it->second;
            // A client that says nothing for two minutes (they PING every few seconds) is gone.
            if (now - c.last_seen > std::chrono::minutes(2)) {
                it = clients.erase(it);
                continue;
            }
            for (auto p = c.unacked.begin(); p != c.unacked.end();) {
                if (now - p->sent < std::chrono::seconds(1)) {
                    ++p;
                } else if (p->tries >= 8) {
                    p = c.unacked.erase(p);
                } else {
                    socket.send(it->first, fmt::format("<{}>{}", p->id, p->text));
                    p->sent = now;
                    p->tries++;
                    ++p;
                }
            }
            ++it;
        }
    }
};

TlcsServer::TlcsServer() : impl_(std::make_unique<Impl>()) {}

TlcsServer::~TlcsServer() { stop(); }

std::string TlcsServer::start(uint16_t port) {
    stop();
    if (auto error = impl_->socket.bind(port); !error.empty()) return error;
    running_ = true;
    thread_  = std::thread([this] {
        while (running_) {
            net::Endpoint from;
            if (const auto msg = impl_->socket.receive(from, 100)) impl_->handle(from, *msg);

            std::optional<BroadcastSnapshot> snapshot;
            int featured = -1;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                if (impl_->fresh) {
                    snapshot     = impl_->latest;
                    featured     = impl_->featured;
                    impl_->fresh = false;
                }
            }
            if (snapshot) impl_->update(*snapshot, featured);
            impl_->resendAndExpire();
            clients_ = static_cast<int>(impl_->clients.size());
        }
    });
    return {};
}

void TlcsServer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    impl_->socket.close();
    impl_->clients.clear();
    clients_ = 0;
}

void TlcsServer::publish(const BroadcastSnapshot& snapshot, int featured) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->latest   = snapshot;
    impl_->featured = featured;
    impl_->fresh    = true;
}

}  // namespace fcg
