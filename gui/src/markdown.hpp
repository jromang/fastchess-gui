#pragma once

// The Markdown of the guide as the guide panel draws it: md4c's parse turned into blocks of styled runs, the lines a
// run of text wraps into at a width, and the search's folding of letter case and accents. Nothing here draws: the
// width of a text comes from a function, so that the tests measure without fonts.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::md {

// How a run of text looks; several may be combined.
enum Style : uint8_t { Plain = 0, Bold = 1, Italic = 2, Code = 4, Link = 8, Key = 16 };

struct Run {
    std::string text;
    uint8_t style = Plain;
    std::string link;  // Link: "#anchor" or "https://…"
};

enum class BlockKind { Heading, Paragraph, ListItem, Table, Code, Image, Rule };

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    int level      = 0;    // Heading: 1 to 6; ListItem: its depth, from 1
    std::string marker;    // ListItem: "•", "1."…; empty for a paragraph continuing an item
    std::vector<Run> runs; // Heading, Paragraph, ListItem
    std::string text;      // Code: its lines; Image: its alt text
    std::string src;       // Image: its path, as the Markdown gives it
    // Table: rows of cells of runs, the first row the header; and each column's alignment (0 left, 1 centre, 2 right).
    std::vector<std::vector<std::vector<Run>>> rows;
    std::vector<int> align;
};

// The blocks of a Markdown text (GitHub's tables included; raw HTML left out but <kbd>, which makes Key runs).
[[nodiscard]] std::vector<Block> parse(std::string_view markdown);

// The text of runs without their styles.
[[nodiscard]] std::string plain(const std::vector<Run>& runs);

// A piece of a run on a line: run `run`, bytes [begin, end) of its text, `x` from the line's start.
struct Piece {
    std::size_t run   = 0;
    std::size_t begin = 0;
    std::size_t end   = 0;
    float x           = 0;
    float width       = 0;
};
struct Line {
    std::vector<Piece> pieces;
    float width = 0;
};

// The width of `text` drawn in `style`.
using Measure = std::function<float(std::string_view text, uint8_t style)>;

// The lines `runs` wrap into within `width`: between words, and between any two characters of Chinese or Japanese
// text (not before a closing mark such as 。 or ）, nor after an opening one); a word wider than the line is cut
// between characters. Spaces at the end of a line take no width; a run's "\n" (a hard break) starts a new line.
[[nodiscard]] std::vector<Line> wrap(const std::vector<Run>& runs, float width, const Measure& measure);

// Text folded for the search: lower case, accents taken off (é → e, ß → ss, ё → е), one code point each, with the
// byte where each came from in the original text (so that a match found in the folded text is found in it too).
struct Folded {
    std::u32string text;
    std::vector<std::size_t> from;  // text.size() + 1 entries: the last one is the end of the original
};
[[nodiscard]] Folded fold(std::string_view text);

// The words of a query, folded, without the empty ones.
[[nodiscard]] std::vector<std::u32string> queryWords(std::string_view query);

// Where the words occur in `text`: byte ranges of the original, in order, not overlapping.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> matches(std::string_view text,
                                                                       const std::vector<std::u32string>& words);

}  // namespace fcg::md
