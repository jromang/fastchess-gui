#include "chess324.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "files.hpp"

namespace fcg::tour {

std::vector<std::string> chess324Ranks() {
    // The squares between the rooks and the king, by file (a = 0). On one rank, two squares are of opposite colours
    // when their files differ by an odd number: b, d and f are of one colour, c and g of the other, on both sides.
    constexpr int free_files[] = {1, 2, 3, 5, 6};
    std::vector<std::string> ranks;
    for (const int b1 : free_files) {
        for (const int b2 : free_files) {
            if (b2 <= b1 || (b2 - b1) % 2 == 0) continue;
            for (const int queen : free_files) {
                if (queen == b1 || queen == b2) continue;
                std::string rank = "RNNNKNNR";  // the knights take the squares left
                rank[b1]         = 'B';
                rank[b2]         = 'B';
                rank[queen]      = 'Q';
                ranks.push_back(rank);
            }
        }
    }
    const auto usual = std::find(ranks.begin(), ranks.end(), "RNBQKBNR");
    std::rotate(ranks.begin(), usual, usual + 1);
    return ranks;
}

std::vector<std::string> chess324Positions(bool symmetrical) {
    const auto ranks = chess324Ranks();
    std::vector<std::string> out;
    for (const auto& white : ranks) {
        for (const auto& black : ranks) {
            if (symmetrical && black != white) continue;
            std::string top = black;
            for (auto& c : top) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            out.push_back(top + "/pppppppp/8/8/8/8/PPPPPPPP/" + white + " w KQkq -");
        }
    }
    return out;
}

std::size_t writeChess324(const std::filesystem::path& file, bool symmetrical) {
    const auto positions = chess324Positions(symmetrical);
    std::string text;
    for (const auto& p : positions) text += p + "\n";
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    replaceFile(file, text);
    return positions.size();
}

}  // namespace fcg::tour
