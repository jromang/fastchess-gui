#pragma once

#include <string>

#include <core/helper.hpp>
#include <types/enums.hpp>

namespace fastchess::config {

struct Pgn {
    std::vector<std::string> additional_lines_rgx;
    std::string event_name = "Fastchess Tournament";
    std::string site       = "?";

    std::string file;
    NotationType notation = NotationType::SAN;
    bool append_file      = true;
    bool track_nodes      = false;
    bool track_seldepth   = false;
    bool track_nps        = false;
    bool track_hashfull   = false;
    bool track_tbhits     = false;
    bool track_timeleft   = false;
    bool track_latency    = false;
    bool track_pv         = false;
    bool track_wdl        = false;  // fastchess-gui: wdl=W/D/L, when the engine reports it
    bool track_decision   = false;  // fastchess-gui: dt=, when the engine settled on its move
    bool min              = false;
    bool crc              = false;
};
// fastchess-gui: with defaults, so that a tournament written before a field was added still reads.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Pgn, additional_lines_rgx, event_name, site, file, notation,
                                                append_file, track_nodes, track_seldepth, track_nps, track_hashfull,
                                                track_tbhits, track_timeleft, track_latency, track_pv, track_wdl,
                                                track_decision, min, crc)

}  // namespace fastchess::config
