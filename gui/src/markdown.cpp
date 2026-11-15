#include "markdown.hpp"

#include <algorithm>

#include <md4c.h>

namespace fcg::md {

namespace {

char32_t decode(std::string_view s, std::size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    int len      = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) {
        ++i;
        return 0xFFFD;
    }
    char32_t cp = len == 1 ? b : b & (0x7F >> len);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return cp;
}

void encode(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// "&amp;", "&#233;"…: the character, or the entity as it is when it is not one of these.
std::string entity(std::string_view e) {
    if (e == "&amp;") return "&";
    if (e == "&lt;") return "<";
    if (e == "&gt;") return ">";
    if (e == "&quot;") return "\"";
    if (e == "&apos;") return "'";
    if (e == "&nbsp;") return "\xC2\xA0";
    if (e.size() > 3 && e[1] == '#') {
        char32_t cp = 0;
        const bool hex = e[2] == 'x' || e[2] == 'X';
        for (std::size_t i = hex ? 3 : 2; i + 1 < e.size(); ++i) {
            const char c = e[i];
            const int d  = c >= '0' && c <= '9' ? c - '0'
                           : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                           : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                         : -1;
            if (d < 0 || cp > 0x10FFFF) return std::string(e);
            cp = cp * (hex ? 16 : 10) + static_cast<char32_t>(d);
        }
        if (cp == 0 || cp > 0x10FFFF) return std::string(e);
        std::string out;
        encode(out, cp);
        return out;
    }
    return std::string(e);
}

struct Parser {
    std::vector<Block> blocks;
    int current = -1;  // the block text goes to, -1 when none

    struct List {
        bool ordered = false;
        unsigned next = 1;
    };
    std::vector<List> lists;
    std::vector<int> items;  // the depth of each list item entered, innermost last

    int bold = 0, italic = 0, code = 0, key = 0;
    std::vector<std::string> links;
    bool image = false;
    std::string image_src, image_alt;
    std::vector<Block> images;  // pictures of the paragraph being read, shown after it
    bool html_block = false;
    bool code_block = false;

    bool table = false;
    bool row_header = false;
    // A line break inside a paragraph is a space, but not between two Chinese or Japanese characters: decided when the
    // text after it comes.
    bool soft_break = false;

    Block& newBlock(BlockKind kind) {
        blocks.push_back(Block{});
        blocks.back().kind = kind;
        current            = static_cast<int>(blocks.size()) - 1;
        return blocks.back();
    }

    std::vector<Run>* target() {
        if (table && !blocks.empty() && blocks.back().kind == BlockKind::Table) {
            auto& rows = blocks.back().rows;
            if (!rows.empty() && !rows.back().empty()) return &rows.back().back();
            return nullptr;
        }
        if (current < 0) {
            // Text of a list item after a list nested in it: a paragraph continuing the item.
            if (!items.empty()) {
                Block& b = newBlock(BlockKind::ListItem);
                b.level  = items.back();
            } else {
                newBlock(BlockKind::Paragraph);
            }
        }
        return &blocks[static_cast<std::size_t>(current)].runs;
    }

    uint8_t style() const {
        uint8_t s = Plain;
        if (bold) s |= Bold;
        if (italic) s |= Italic;
        if (code) s |= Code;
        if (key) s |= Key;
        if (!links.empty()) s |= Link;
        return s;
    }

    void add(std::string_view text, uint8_t s) {
        if (text.empty()) return;
        auto* runs = target();
        if (!runs) return;
        const std::string link = (s & Link) ? links.back() : std::string();
        if (!runs->empty() && runs->back().style == s && runs->back().link == link) {
            runs->back().text += text;
        } else {
            runs->push_back(Run{std::string(text), s, link});
        }
    }

    void enterBlock(MD_BLOCKTYPE type, void* detail) {
        switch (type) {
            case MD_BLOCK_UL:
                lists.push_back({false, 1});
                break;
            case MD_BLOCK_OL:
                lists.push_back({true, static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start});
                break;
            case MD_BLOCK_LI: {
                const int depth = static_cast<int>(lists.size());
                items.push_back(depth);
                Block& b = newBlock(BlockKind::ListItem);
                b.level  = depth;
                if (!lists.empty() && lists.back().ordered) {
                    b.marker = std::to_string(lists.back().next++) + ".";
                } else {
                    b.marker = depth <= 1 ? "\xE2\x80\xA2" : "\xE2\x97\xA6";  // • ◦
                }
                break;
            }
            case MD_BLOCK_P:
                if (!items.empty()) {
                    // The item's first paragraph is the item itself; the next ones continue it.
                    if (current >= 0 && blocks[static_cast<std::size_t>(current)].kind == BlockKind::ListItem &&
                        blocks[static_cast<std::size_t>(current)].runs.empty())
                        break;
                    Block& b = newBlock(BlockKind::ListItem);
                    b.level  = items.back();
                } else {
                    newBlock(BlockKind::Paragraph);
                }
                break;
            case MD_BLOCK_H:
                newBlock(BlockKind::Heading).level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
                break;
            case MD_BLOCK_CODE:
                newBlock(BlockKind::Code);
                code_block = true;
                break;
            case MD_BLOCK_HR:
                newBlock(BlockKind::Rule);
                current = -1;
                break;
            case MD_BLOCK_HTML:
                html_block = true;
                break;
            case MD_BLOCK_TABLE:
                newBlock(BlockKind::Table);
                table   = true;
                current = -1;
                break;
            case MD_BLOCK_THEAD:
                row_header = true;
                break;
            case MD_BLOCK_TBODY:
                row_header = false;
                break;
            case MD_BLOCK_TR:
                if (table) blocks.back().rows.emplace_back();
                break;
            case MD_BLOCK_TH:
            case MD_BLOCK_TD:
                if (table && !blocks.back().rows.empty()) {
                    blocks.back().rows.back().emplace_back();
                    if (blocks.back().rows.size() == 1) {
                        const auto a = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
                        blocks.back().align.push_back(a == MD_ALIGN_CENTER ? 1 : a == MD_ALIGN_RIGHT ? 2 : 0);
                    }
                }
                break;
            default:
                break;
        }
    }

    void leaveBlock(MD_BLOCKTYPE type) {
        switch (type) {
            case MD_BLOCK_UL:
            case MD_BLOCK_OL:
                if (!lists.empty()) lists.pop_back();
                current = -1;
                break;
            case MD_BLOCK_LI:
                if (!items.empty()) items.pop_back();
                current = -1;
                break;
            case MD_BLOCK_P:
            case MD_BLOCK_H:
                // A paragraph that held only pictures leaves them alone.
                if (!images.empty()) {
                    if (current >= 0 && blocks[static_cast<std::size_t>(current)].runs.empty() &&
                        current == static_cast<int>(blocks.size()) - 1 &&
                        blocks[static_cast<std::size_t>(current)].kind == BlockKind::Paragraph)
                        blocks.pop_back();
                    for (auto& b : images) blocks.push_back(std::move(b));
                    images.clear();
                }
                current = -1;
                break;
            case MD_BLOCK_CODE:
                if (current >= 0) {
                    auto& text = blocks[static_cast<std::size_t>(current)].text;
                    while (!text.empty() && text.back() == '\n') text.pop_back();
                }
                code_block = false;
                current    = -1;
                break;
            case MD_BLOCK_HTML:
                html_block = false;
                break;
            case MD_BLOCK_TABLE:
                table   = false;
                current = -1;
                break;
            default:
                break;
        }
    }

    void enterSpan(MD_SPANTYPE type, void* detail) {
        switch (type) {
            case MD_SPAN_STRONG:
                ++bold;
                break;
            case MD_SPAN_EM:
                ++italic;
                break;
            case MD_SPAN_CODE:
                ++code;
                break;
            case MD_SPAN_A: {
                const auto& href = static_cast<MD_SPAN_A_DETAIL*>(detail)->href;
                links.emplace_back(href.text, href.size);
                break;
            }
            case MD_SPAN_IMG: {
                const auto& src = static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src;
                image           = true;
                image_src.assign(src.text, src.size);
                image_alt.clear();
                break;
            }
            default:
                break;
        }
    }

    void leaveSpan(MD_SPANTYPE type) {
        switch (type) {
            case MD_SPAN_STRONG:
                --bold;
                break;
            case MD_SPAN_EM:
                --italic;
                break;
            case MD_SPAN_CODE:
                --code;
                break;
            case MD_SPAN_A:
                if (!links.empty()) links.pop_back();
                break;
            case MD_SPAN_IMG: {
                image = false;
                Block b;
                b.kind = BlockKind::Image;
                b.src  = image_src;
                b.text = image_alt;
                images.push_back(std::move(b));
                break;
            }
            default:
                break;
        }
    }

    void text(MD_TEXTTYPE type, std::string_view s) {
        if (html_block) return;
        if (code_block) {
            if (current >= 0) blocks[static_cast<std::size_t>(current)].text += s;
            return;
        }
        std::string t;
        switch (type) {
            case MD_TEXT_NORMAL:
            case MD_TEXT_CODE:
                t = std::string(s);
                break;
            case MD_TEXT_SOFTBR:
                soft_break = true;
                return;
            case MD_TEXT_BR:
                t = "\n";
                break;
            case MD_TEXT_ENTITY:
                t = entity(s);
                break;
            case MD_TEXT_NULLCHAR:
                t = "\xEF\xBF\xBD";
                break;
            case MD_TEXT_HTML:
                if (s == "<kbd>")
                    ++key;
                else if (s == "</kbd>" && key > 0)
                    --key;
                return;
            default:
                return;
        }
        if (soft_break) {
            soft_break = false;
            if (!(endsCjk() && startsCjk(t))) t.insert(t.begin(), ' ');
        }
        if (image) {
            image_alt += t;
            return;
        }
        add(t, style());
    }

    // Whether the text so far ends, and `t` starts, with a Chinese or Japanese character.
    bool endsCjk() {
        const std::string* last = nullptr;
        if (image) {
            last = &image_alt;
        } else if (auto* runs = target(); runs && !runs->empty()) {
            last = &runs->back().text;
        }
        if (!last || last->empty()) return false;
        std::size_t i = last->size() - 1;
        while (i > 0 && (static_cast<unsigned char>((*last)[i]) & 0xC0) == 0x80) --i;
        return wide(decode(*last, i));
    }
    static bool startsCjk(const std::string& t) {
        std::size_t i = 0;
        return !t.empty() && wide(decode(t, i));
    }
    static bool wide(char32_t c) {
        return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
    }
};

}  // namespace

std::vector<Block> parse(std::string_view markdown) {
    Parser p;
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags       = MD_FLAG_TABLES | MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_NOINDENTEDCODEBLOCKS;
    parser.enter_block = [](MD_BLOCKTYPE type, void* detail, void* user) {
        static_cast<Parser*>(user)->enterBlock(type, detail);
        return 0;
    };
    parser.leave_block = [](MD_BLOCKTYPE type, void*, void* user) {
        static_cast<Parser*>(user)->leaveBlock(type);
        return 0;
    };
    parser.enter_span = [](MD_SPANTYPE type, void* detail, void* user) {
        static_cast<Parser*>(user)->enterSpan(type, detail);
        return 0;
    };
    parser.leave_span = [](MD_SPANTYPE type, void*, void* user) {
        static_cast<Parser*>(user)->leaveSpan(type);
        return 0;
    };
    parser.text = [](MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* user) {
        static_cast<Parser*>(user)->text(type, std::string_view(text, size));
        return 0;
    };
    md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &p);
    // Empty paragraphs (a list item made only of a nested list) are left out.
    p.blocks.erase(std::remove_if(p.blocks.begin(), p.blocks.end(),
                                  [](const Block& b) {
                                      return (b.kind == BlockKind::Paragraph ||
                                              (b.kind == BlockKind::ListItem && b.marker.empty())) &&
                                             b.runs.empty();
                                  }),
                   p.blocks.end());
    return std::move(p.blocks);
}

std::string plain(const std::vector<Run>& runs) {
    std::string out;
    for (const auto& r : runs) out += r.text;
    return out;
}

namespace {

bool isCjk(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) ||
           (c >= 0x20000 && c <= 0x2FFFF);
}

// No line starts with these…
bool isClosing(char32_t c) {
    switch (c) {
        case U'、': case U'。': case U'，': case U'．': case U'：': case U'；': case U'！': case U'？':
        case U'）': case U'］': case U'｝': case U'〉': case U'》': case U'」': case U'』': case U'】':
        case U'〕': case U'〗': case U'・': case U'…': case U'”': case U'’': case U')': case U',':
        case U'.': case U';': case U':': case U'!': case U'?': case U']':
            return true;
        default:
            return false;
    }
}

// …and none ends with these.
bool isOpening(char32_t c) {
    switch (c) {
        case U'（': case U'［': case U'｛': case U'〈': case U'《': case U'「': case U'『': case U'【':
        case U'〔': case U'〖': case U'“': case U'‘': case U'(': case U'[':
            return true;
        default:
            return false;
    }
}

struct Glyph {
    std::size_t run, begin, end;
    char32_t cp;
};

}  // namespace

std::vector<Line> wrap(const std::vector<Run>& runs, float width, const Measure& measure) {
    std::vector<Glyph> glyphs;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const auto& t = runs[r].text;
        for (std::size_t i = 0; i < t.size();) {
            const std::size_t b = i;
            const char32_t cp   = decode(t, i);
            glyphs.push_back({r, b, i, cp});
        }
    }

    // Segments: glyphs of one run with no break between them; `spaces` trailing spaces; `brk` a break after it.
    struct Segment {
        std::size_t run, begin, end, text_end;
        bool brk  = false;
        bool hard = false;  // a "\n": the line ends after it
        float w = 0, space = 0;
    };
    std::vector<Segment> segments;
    const auto breakBetween = [&](std::size_t i) {  // between glyphs i and i + 1
        const char32_t a = glyphs[i].cp, b = glyphs[i + 1].cp;
        if (a == ' ' || a == '\n') return b != ' ';
        if (b == ' ' || b == '\n') return false;
        if (isClosing(b) || isOpening(a)) return false;
        return isCjk(a) || isCjk(b);
    };
    std::size_t start = 0;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const bool last      = i + 1 == glyphs.size();
        const bool run_ends  = last || glyphs[i + 1].run != glyphs[i].run;
        const bool brk       = !last && breakBetween(i);
        const bool hard      = glyphs[i].cp == '\n';
        if (!(run_ends || brk || hard || last)) continue;
        Segment s;
        s.run   = glyphs[start].run;
        s.begin = glyphs[start].begin;
        s.end   = glyphs[i].end;
        // Its trailing spaces (and a "\n") take no width at the end of a line.
        std::size_t text_end = s.end;
        std::size_t k        = i + 1;
        int spaces           = 0;
        while (k > start && (glyphs[k - 1].cp == ' ' || glyphs[k - 1].cp == '\n')) {
            text_end = glyphs[k - 1].begin;
            spaces += glyphs[k - 1].cp == ' ';
            --k;
        }
        s.text_end = text_end;
        s.brk      = brk || last || hard;
        s.hard     = hard;
        const auto& text = runs[s.run].text;
        s.w     = text_end > s.begin ? measure(std::string_view(text).substr(s.begin, text_end - s.begin),
                                               runs[s.run].style)
                                     : 0.0f;
        s.space = spaces > 0 ? measure(" ", runs[s.run].style) * static_cast<float>(spaces) : 0.0f;
        segments.push_back(s);
        start = i + 1;
    }

    std::vector<Line> lines(1);
    float x = 0;
    const auto place = [&](const Segment& s, std::size_t begin, std::size_t end, float w) {
        if (end > begin) lines.back().pieces.push_back({s.run, begin, end, x, w});
        x += w;
        lines.back().width = std::max(lines.back().width, x);
    };
    for (std::size_t i = 0; i < segments.size();) {
        // A word: segments up to one with a break after it.
        std::size_t j = i;
        float word    = 0;
        while (true) {
            word += segments[j].w + (segments[j].brk ? 0.0f : segments[j].space);
            if (segments[j].brk || j + 1 == segments.size()) break;
            ++j;
        }
        if (x > 0 && x + word > width) {
            lines.emplace_back();
            x = 0;
        }
        for (std::size_t k = i; k <= j; ++k) {
            const auto& s = segments[k];
            if (x == 0 && s.w > width) {
                // Wider than the line: cut between characters.
                const auto& text = runs[s.run].text;
                std::size_t b    = s.begin;
                while (b < s.text_end) {
                    std::size_t e = b, best = b;
                    float best_w  = 0;
                    while (e < s.text_end) {
                        std::size_t n = e;
                        decode(text, n);
                        const float w = measure(std::string_view(text).substr(b, n - b), runs[s.run].style);
                        if (w > width - x && best > b) break;
                        best   = n;
                        best_w = w;
                        e      = n;
                    }
                    place(s, b, best, best_w);
                    b = best;
                    if (b < s.text_end) {
                        lines.emplace_back();
                        x = 0;
                    }
                }
            } else {
                place(s, s.begin, s.text_end, s.w);
            }
            if (s.hard) {
                lines.emplace_back();
                x = 0;
            } else {
                x += s.space;
            }
        }
        i = j + 1;
    }
    if (lines.size() > 1 && lines.back().pieces.empty()) lines.pop_back();
    return lines;
}

namespace {

char32_t lowerCp(char32_t cp) {
    if (cp >= 0x41 && cp <= 0x5A) return cp + 0x20;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) return cp % 2 == 0 ? cp + 1 : cp;
    if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return cp % 2 == 1 ? cp + 1 : cp;
    if (cp == 0x178) return 0xFF;
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    return cp;
}

// The letter without its accent (of a lower case letter), or the letters it stands for; empty: as it is.
std::u32string_view base(char32_t cp) {
    switch (cp) {
        case U'à': case U'á': case U'â': case U'ã': case U'ä': case U'å': case U'ā': case U'ă': case U'ą':
            return U"a";
        case U'ç': case U'ć': case U'ĉ': case U'ċ': case U'č':
            return U"c";
        case U'ď': case U'đ':
            return U"d";
        case U'è': case U'é': case U'ê': case U'ë': case U'ē': case U'ĕ': case U'ė': case U'ę': case U'ě':
            return U"e";
        case U'ĝ': case U'ğ': case U'ġ': case U'ģ':
            return U"g";
        case U'ĥ': case U'ħ':
            return U"h";
        case U'ì': case U'í': case U'î': case U'ï': case U'ĩ': case U'ī': case U'ĭ': case U'į': case U'ı':
            return U"i";
        case U'ĵ':
            return U"j";
        case U'ķ':
            return U"k";
        case U'ĺ': case U'ļ': case U'ľ': case U'ŀ': case U'ł':
            return U"l";
        case U'ñ': case U'ń': case U'ņ': case U'ň':
            return U"n";
        case U'ò': case U'ó': case U'ô': case U'õ': case U'ö': case U'ø': case U'ō': case U'ŏ': case U'ő':
            return U"o";
        case U'ŕ': case U'ŗ': case U'ř':
            return U"r";
        case U'ś': case U'ŝ': case U'ş': case U'š':
            return U"s";
        case U'ţ': case U'ť': case U'ŧ':
            return U"t";
        case U'ù': case U'ú': case U'û': case U'ü': case U'ũ': case U'ū': case U'ŭ': case U'ů': case U'ű': case U'ų':
            return U"u";
        case U'ŵ':
            return U"w";
        case U'ý': case U'ÿ': case U'ŷ':
            return U"y";
        case U'ź': case U'ż': case U'ž':
            return U"z";
        case U'æ':
            return U"ae";
        case U'œ':
            return U"oe";
        case U'ß':
            return U"ss";
        case U'ё':
            return U"е";
        case U'’': case U'‘':
            return U"'";
        case U'“': case U'”': case U'«': case U'»':
            return U"\"";
        case 0xA0: case 0x202F: case 0x3000:
            return U" ";
        default:
            return {};
    }
}

}  // namespace

Folded fold(std::string_view text) {
    Folded out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t at = i;
        const char32_t cp    = lowerCp(decode(text, i));
        const auto b         = base(cp);
        if (b.empty()) {
            out.text += cp;
            out.from.push_back(at);
        } else {
            for (const char32_t c : b) {
                out.text += c;
                out.from.push_back(at);
            }
        }
    }
    out.from.push_back(text.size());
    return out;
}

std::vector<std::u32string> queryWords(std::string_view query) {
    std::vector<std::u32string> out;
    std::u32string word;
    for (const char32_t c : fold(query).text) {
        if (c == ' ' || c == '\t') {
            if (!word.empty()) out.push_back(std::move(word));
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>> matches(std::string_view text,
                                                         const std::vector<std::u32string>& words) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    if (words.empty()) return out;
    const Folded f = fold(text);
    for (const auto& w : words) {
        for (std::size_t at = f.text.find(w); at != std::u32string::npos; at = f.text.find(w, at + 1))
            out.emplace_back(f.from[at], f.from[at + w.size()]);
    }
    std::sort(out.begin(), out.end());
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    for (const auto& m : out) {
        if (!merged.empty() && m.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, m.second);
        else
            merged.push_back(m);
    }
    return merged;
}

}  // namespace fcg::md
