#pragma once

// Rating lists of others, read to fill the reference Elo of the library's engines: the CCRL lists (their text
// version, the web page copied and pasted, or the page itself), Ordo's output (text or CSV) and plain CSV files
// (name,elo).

#include <string>
#include <string_view>
#include <vector>

namespace fcg::tour {

struct ListedRating {
    std::string name;
    int elo = 0;  // rounded
};

// The engines a rating list names, in the list's order; a name met again is left out (a CCRL page shows each engine a
// second time, in its crosstable). Whatever is not an engine's line (titles, headers, a CCRL page's LOS) is skipped.
// Each line is read by its look: cells separated by tabs (a table copied from a web page) or by commas or
// semicolons (CSV), Ordo's "rank name : rating ...", or columns aligned with spaces (CCRL's text lists); the rows of
// an HTML table are cells too. In cells, a header naming a Name (Engine, Player) and an Elo (Rating) column tells
// where they are; without one, the name is the first cell with a letter and the rating the first number after it.
// A rating is a number from 1 to 5000.
[[nodiscard]] std::vector<ListedRating> parseRatingList(std::string_view text);

struct RatingMatch {
    std::string engine;  // in the library
    std::string listed;  // its name on the list
    int elo = 0;
};

struct RatingMatches {
    std::vector<RatingMatch> matched;    // at most one per library engine, in the list's order
    std::vector<std::string> unmatched;  // the names of the list that are no library engine
};

// The library engines the list names, by the rule of libraryEngine() (game_db.hpp): the same name, or the only one
// that is the same once letter case and spaces are set aside. Two names of the list for one engine: its own name
// wins, else the first.
[[nodiscard]] RatingMatches matchRatings(const std::vector<ListedRating>& list,
                                         const std::vector<std::string>& library);

}  // namespace fcg::tour
