#pragma once

// The matching of the command palette (Ctrl+K): what is typed against the names of pages, engines, tournaments and
// actions.

#include <string>
#include <string_view>
#include <vector>

namespace fcg::palette {

// How well `query` matches `text`, ignoring letter case: -1 when the letters of `query` are not all in `text` in
// that order, and its words not all in `text` either. Otherwise the higher the better: `text` starting with `query`
// first, then a word of `text` starting with it, then each word of `query` starting a word of `text` (in any order),
// then `query` anywhere in it, then its letters apart, better at the start of words and side by side. An empty query
// matches everything with 0.
[[nodiscard]] int matchScore(std::string_view query, std::string_view text);

// The texts `query` matches, best first: their indexes into `texts`, those of equal score in their order. `extra`,
// when given, holds by index more text to look in (the page an action is on): what is typed found only with it comes
// after what is found in the text itself, and before the texts that only have its letters apart.
[[nodiscard]] std::vector<int> rank(std::string_view query, const std::vector<std::string>& texts,
                                    const std::vector<std::string>* extra = nullptr);

}  // namespace fcg::palette
