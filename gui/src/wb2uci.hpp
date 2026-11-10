#pragma once

// Runs a WinBoard (CECP, xboard protocol) engine as a UCI one: `fastchess-gui --wb2uci ENGINE [ARGS...]` starts the
// engine and translates between the UCI fastchess speaks and the engine's protocol. fastchess starts it in place of
// the engine of a configuration whose protocol is "xboard" (fastchess::engine::xboard_adapter).
//
// The translation follows the protocol as WinBoard and cutechess speak it: protover 2 features (with a timeout and
// protocol 1 defaults for engines that send none), setboard or edit mode, usermove and SAN moves, ping, level/time/
// otim, st, sd, nps, options, thinking output as info lines, Chess960 (variant fischerandom).

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>

namespace fcg::wb2uci {

using Clock = std::chrono::steady_clock;

// How long an engine has to send its features after protover 2, extended by every feature line (WinBoard waits
// 2 s, cutechess 8 s; a slow disk or Wine needs more); after "feature done=0", up to fastchess' startup time.
inline constexpr std::chrono::milliseconds FEATURE_TIMEOUT       = std::chrono::seconds(5);
inline constexpr std::chrono::milliseconds FEATURE_DONE0_TIMEOUT = std::chrono::seconds(50);
// How long to wait for the move a stopped search still owes, and for a move after a draw claim.
inline constexpr std::chrono::milliseconds OWED_MOVE_TIMEOUT = std::chrono::seconds(1);
inline constexpr std::chrono::milliseconds CLAIM_TIMEOUT     = std::chrono::seconds(2);

// An engine option declared by "feature option=".
struct CecpOption {
    std::string name;
    std::string kind;  // -spin -slider -check -string -file -path -combo -button -save -reset
    std::string value;
    std::string min;
    std::string max;
    std::vector<std::string> vars;  // -combo
};

// The option the adapter adds for engines that give their scores from White's point of view (some old ones do),
// not from the side to move's as the protocol asks: with it, the scores of their searches as Black are reversed.
inline constexpr const char* WHITE_POV_OPTION = "Scores from White's side";

// Told before uciok when the engine asks to be started again for every game (feature reuse=0).
inline constexpr const char* NO_REUSE_INFO = "info string feature reuse=0";

// Parses the text of a "feature option" value, e.g. "Hash File -file book.bin", "Style -combo Solid /// *Normal".
[[nodiscard]] std::optional<CecpOption> parseOption(const std::string& text);

// Parses the arguments of a "feature" line: name=value pairs, values quoted or not.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parseFeatures(const std::string& args);

// The translation, without processes: lines in, lines out, so that it can be tested.
class Adapter {
   public:
    using Sink = std::function<void(const std::string&)>;

    // `name`: the engine's name until it says its own (feature myname).
    Adapter(Sink to_gui, Sink to_engine, std::string name);

    // Sends xboard / protover 2.
    void start(Clock::time_point now);

    // A UCI command from the GUI.
    void fromGui(const std::string& line, Clock::time_point now);

    // A line of the engine's output.
    void fromEngine(const std::string& line, Clock::time_point now);

    // Ends the wait for the features when it times out.
    void tick(Clock::time_point now);

    [[nodiscard]] bool quitting() const noexcept { return quitting_; }
    [[nodiscard]] bool ready() const noexcept { return init_ == Init::Done; }

   private:
    enum class Init { Waiting, Done };

    void finishInit();
    void handleGui(const std::string& line);
    void abandonSearch();
    void sendMoves(const std::vector<std::string>& moves);
    void engineResult(const std::string& line);
    void sendUciId();
    void setOption(const std::string& name, const std::string& value);
    void position(const std::vector<std::string>& tokens);
    void sendHeld();
    void newGame(const std::string& fen);
    void go(const std::vector<std::string>& tokens);
    void engineMove(const std::string& text);
    void thinking(const std::string& line);
    void onFeatures(const std::string& args);
    [[nodiscard]] std::string moveText(const chess::Board& board, chess::Move move) const;
    [[nodiscard]] bool feature(const std::string& name, bool def) const;
    [[nodiscard]] bool playsChess960() const;  // its variants feature has fischerandom
    void info(const std::string& text) const;

    Sink to_gui_;
    Sink to_engine_;
    std::string name_;

    Init init_ = Init::Waiting;
    Clock::time_point init_deadline_{};
    std::deque<std::string> queued_;  // GUI commands that came before the features
    std::map<std::string, std::string> features_;
    std::vector<CecpOption> options_;

    int ping_ = 0;
    std::optional<int> pending_pong_;
    std::map<std::string, std::string> held_;  // memory, cores, egtpath: sent again before every new game

    Clock::time_point now_{};
    std::optional<Clock::time_point> ready_deadline_;  // isready waits for an owed move (engines without ping)
    std::optional<Clock::time_point> move_deadline_;   // movetime: "?" then
    std::optional<Clock::time_point> claim_deadline_;  // a result claimed during a search, no move yet

    // The game as the GUI last gave it, to set the engine up again when it lost track.
    std::string gui_fen_;
    std::vector<std::string> gui_moves_;

    // What the engine knows of the game: the position it started from and the moves it was given or played.
    bool new_game_ = true;
    bool in_sync_  = false;
    std::string start_fen_;
    std::vector<std::string> moves_;
    std::vector<std::string> sent_moves_;  // as written to the engine, to tell which "Illegal move" is about one
    chess::Board board_;
    bool level_sent_ = false;
    bool chess960_   = false;  // UCI_Chess960: variant fischerandom, castling as O-O and king takes rook
    bool white_pov_  = false;  // WHITE_POV_OPTION: the engine's scores are White's, not the side to move's

    bool searching_             = false;
    bool stale_                 = false;  // a search the GUI gave up on: its move, when it comes, is dropped
    bool resigned_              = false;  // resigned while not searching: the answer to the next go
    chess::Color engine_colour_ = chess::Color::WHITE;
    bool quitting_              = false;
};

// The --wb2uci mode: `args` are the engine's command and its arguments. Returns the exit code.
int run(const std::vector<std::string>& args);

}  // namespace fcg::wb2uci
