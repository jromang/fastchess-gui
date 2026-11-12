#include <doctest/doctest.hpp>

#include <string>
#include <vector>

#include <tour/rating_import.hpp>

using namespace fcg::tour;

namespace {

// "name elo" of each engine read, to compare at a glance.
std::vector<std::string> read(const std::string& text) {
    std::vector<std::string> out;
    for (const auto& r : parseRatingList(text)) out.push_back(r.name + " " + std::to_string(r.elo));
    return out;
}

}  // namespace

// The samples are taken from the 40/15 pages of a rating list site (November 2026) and from Ordo
// output posted on talkchess.com and sites.google.com/site/computerschess, cut short.

TEST_CASE("rating list: a list site's text list, its title and footer left out") {
    const std::string text = R"(Engine List 40/15 Rating List - All engines, best versions only
2483177 games played by 4792 programs, run by 31 testers
Ponder off, General books (up to 12 moves), 3-4-5 piece EGTB
Time control: Equivalent to 40 moves in 15 minutes on an Intel i7-4770k.
Computed on November 7, 2026 with Bayeselo based on 2'483'177 games
Tested by the list team, 2005-2026, http://ratings.example.org/4040/

   Rank                 Engine                   Elo   +    -   Score  AvOp  Games

    1-2 Stockfish 19 64-bit 4CPU                3650  +16  -16  53.4%  -20.9   752

    1-2 Triumviratus 8.0 64-bit 4CPU            3650  +19  -19  53.3%  -19.6   499

      3 Reckless 0.9.0 64-bit 4CPU              3644  +12  -12  54.7%  -29.0  1643

    736 Hokus-Pokus 0.6.3                       1281  +27  -27  26.7% +208.5   628

    737 BACE 4.6                                1280  +22  -22  21.5% +259.2  1027

Back to Engine List 40/15
Last games added on November 7, 2026
)";
    CHECK(read(text) == std::vector<std::string>{"Stockfish 19 64-bit 4CPU 3650", "Triumviratus 8.0 64-bit 4CPU 3650",
                                                 "Reckless 0.9.0 64-bit 4CPU 3644", "Hokus-Pokus 0.6.3 1281",
                                                 "BACE 4.6 1280"});
    // Windows line ends, a byte order mark, no-break spaces from a web page.
    std::string windows = "\xEF\xBB\xBF";
    for (const char c : text) {
        if (c == '\n') windows += '\r';
        windows += c;
    }
    windows.replace(windows.find("BACE 4.6  "), 10,
                    "BACE\xC2\xA0"
                    "4.6\xC2\xA0\xC2\xA0");
    CHECK(read(windows) == read(text));
}

TEST_CASE("rating list: a list site's text list saved as a web page") {
    const std::string html =
        "<html><body><pre>\n"
        "&nbsp;&nbsp;&nbsp;&nbsp;1-2&nbsp;<span class=\"oss\"><a href=\"engine_details.cgi?match_length=30&amp;"
        "print=Details+(text)&amp;eng=Stockfish%2019%2064-bit%204CPU#Stockfish_19_64-bit_4CPU\">Stockfish&nbsp;19&nbsp;"
        "64-bit&nbsp;4CPU</a></"
        "span>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
        "&nbsp;&nbsp;3650&nbsp;&nbsp;+16&nbsp;&nbsp;-16&nbsp;&nbsp;53.4%&nbsp;&nbsp;-20.9&nbsp;&nbsp;&nbsp;<a "
        "href=\"../"
        "games-by-engine-commented/Stockfish_19_64-bit_4CPU.commented.[752].pgn.7z\">752</a><br>\n"
        "&nbsp;&nbsp;&nbsp;&nbsp;1-2&nbsp;<span class=\"oss\"><a href=\"engine_details.cgi?match_length=30&amp;"
        "print=Details+(text)&amp;eng=Triumviratus%208.0%2064-bit%204CPU#Triumviratus_8_0_64-bit_4CPU\">Triumviratus"
        "&nbsp;8.0&nbsp;64-bit&nbsp;4CPU</a></span>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
        "&nbsp;3650&nbsp;&nbsp;+19&nbsp;&nbsp;-19&nbsp;&nbsp;53.3%&nbsp;&nbsp;-19.6&nbsp;&nbsp;&nbsp;<a href=\"../"
        "games-by-engine-commented/Triumviratus_8_0_64-bit_4CPU.commented.[499].pgn.7z\">499</a><br>\n"
        "</pre><script>var x = '<td>Fake 1</td><td>2000</td>';</script></body></html>";
    CHECK(read(html) == std::vector<std::string>{"Stockfish 19 64-bit 4CPU 3650", "Triumviratus 8.0 64-bit 4CPU 3650"});
}

TEST_CASE("rating list: a list site's web page, its rating table and the crosstable that names the engines again") {
    const std::string html = R"(<table border=0 cellspacing=0 cellpadding=0 class=rating_table>
<tr><td rowspan=2 class=header>Rank</td><td rowspan=2 class=header>Name</td><td colspan=3 class=header>Rating</td><td rowspan=2 class=header>Score</td><td rowspan=2 class=header>Average<br>Opponent</td><td rowspan=2 class=header>Draws</td><td rowspan=2 class=header>Games</td><td rowspan=2 class=header>LOS</td></tr>
<tr><td class=header>Elo</td><td class=header>+</td><td class=header>&#8722;</td></tr>
<tr class="odd"><td rowspan=2 class="number"><b>1&#8209;2</b></td><td rowspan=2 class="name"><span class="oss"><b><a href="cgi/engine_details.cgi?print=Details&amp;each_game=0&amp;eng=Stockfish%2019%2064-bit%204CPU#Stockfish_19_64-bit_4CPU">Stockfish 19 64-bit 4CPU</a></b></span></td><td rowspan=2 class="number_left"><b>3650</b></td><td rowspan=2 class=number_2><b>+16</b></td><td rowspan=2 class=number_2><b>&#8722;16</b></td><td rowspan=2 class=number><b>53.4%</b></td><td rowspan=2 class=number><b>&#8722;20.9</b></td><td rowspan=2 class=number><b>93.0%</b></td><td rowspan=2 class=number><a href="games-by-engine-commented/Stockfish_19_64-bit_4CPU.commented.[752].pgn.7z"><b>752</b></a></td><td style="font-size: 6pt;">&nbsp;</td></tr>
<tr><td rowspan=2 class="los" style="background-color: #8BCB87"><div style="background-color: #88CA8B"><b>49.2%</b></div></td></tr>
<tr class="odd"><td rowspan=2 class="number"><b>3</b></td><td rowspan=2 class="name"><span class="oss"><b><a href="cgi/engine_details.cgi?print=Details&amp;each_game=0&amp;eng=Reckless%200.9.0%2064-bit%204CPU#Reckless_0_9_0_64-bit_4CPU">Reckless 0.9.0 64-bit 4CPU</a></b></span></td><td rowspan=2 class="number_left"><b>3644</b></td><td rowspan=2 class=number_2><b>+12</b></td><td rowspan=2 class=number_2><b>&#8722;12</b></td><td rowspan=2 class=number><b>54.7%</b></td><td rowspan=2 class=number><b>&#8722;29.0</b></td><td rowspan=2 class=number><b>89.8%</b></td><td rowspan=2 class=number><a href="games-by-engine-commented/Reckless_0_9_0_64-bit_4CPU.commented.[1643].pgn.7z"><b>1643</b></a></td></tr>
<tr><td rowspan=2 class="los" style="background-color: #6CBFA7"><div style="background-color: #A7D66B"><b>63.6%</b></div></td></tr>
</table>
<table border=1 cellspacing=0 cellpadding=0 class=crosstable>
<tr class=header><td class=number>#</td><td class=name>Name</td><td class=rating>Elo</td><td class=engine>1<div class="name"><span class="oss"><a href="cgi/engine_details.cgi?match_length=30">Stock</a></span></div></td></tr>
<tr class=data>
<td class=number>1</td><td class=name style="white-space: nowrap;"><span class="oss"><a href="cgi/engine_details.cgi?match_length=30">Stockfish 19 64-bit 4CPU</a></span></td><td class=rating>3650</td><td>&nbsp;</td><td class=results style="background-color: inherit">12&nbsp;&#8722;&nbsp;12<br><span class=detail>+0&#8722;0=24</span><br><b>0</b></td></tr>
</table>)";
    CHECK(read(html) == std::vector<std::string>{"Stockfish 19 64-bit 4CPU 3650", "Reckless 0.9.0 64-bit 4CPU 3644"});
}

TEST_CASE("rating list: a list site's table copied from the browser and pasted, cells apart by tabs") {
    const std::string text =
        "Rank\tName\tRating\tScore\tAverage Opponent\tDraws\tGames\tLOS\n"
        "Elo\t+\t\xE2\x88\x92\n"
        "1\xE2\x80\x91"
        "2\tStockfish 19 64-bit 4CPU\t3650\t+16\t\xE2\x88\x92"
        "16\t53.4%\t\xE2\x88\x92"
        "20.9\t93.0%\t752\t\xC2\xA0\n"
        "49.2%\n"
        "3\tReckless 0.9.0 64-bit 4CPU\t3644\t+12\t\xE2\x88\x92"
        "12\t54.7%\t\xE2\x88\x92"
        "29.0\t89.8%\t1643\n"
        "63.6%\n";
    CHECK(read(text) == std::vector<std::string>{"Stockfish 19 64-bit 4CPU 3650", "Reckless 0.9.0 64-bit 4CPU 3644"});
    // Without the header: the first cell with a letter, and the number after it.
    CHECK(read(text.substr(text.find("1\xE2\x80\x91"))) ==
          std::vector<std::string>{"Stockfish 19 64-bit 4CPU 3650", "Reckless 0.9.0 64-bit 4CPU 3644"});
}

TEST_CASE("rating list: Ordo's text output, ratings rounded, its marks left out") {
    const std::string text = R"(
   # PLAYER                :  RATING  ERROR  POINTS  PLAYED   (%)  CFS(%)     W    D     L  D(%)  OppAvg
   1 Stockfish 13          :  4098.1   50.7  1157.0    1203    96      89  1157    0    46     0  3413.5
   2 Stockfish 21-05-18    :  4058.9   82.7   610.0     630    97      99   610    0    20     0  3335.1
   3 Stockfish 12          :  3934.3   69.2   817.0     873    94     100   817    0    56     0  3350.6
   4 Komodo-Dragon 2       :  3842.2   36.8  1276.0    1409    91      82  1276    0   133     0  3348.4

White advantage = 0.00
Draw rate (equal opponents) = 50.00 %
)";
    CHECK(read(text) == std::vector<std::string>{"Stockfish 13 4098", "Stockfish 21-05-18 4059", "Stockfish 12 3934",
                                                 "Komodo-Dragon 2 3842"});

    const std::string other = R"(   # PLAYER                 : RATING    POINTS   GAMES    (%)
   1 Stockfish 251220 MP    : 3610.2     861.0    1070   80.5%
   2 Lc0 LS15 GTX1050ti     : 3575.7     467.5     600   77.9%
     Engine X             > : 3500.0      10.0      10  100.0%
  12 Fruit 2.1            * :  2700         0       0    0.0%
)";
    CHECK(read(other) == std::vector<std::string>{"Stockfish 251220 MP 3610", "Lc0 LS15 GTX1050ti 3576",
                                                  "Engine X 3500", "Fruit 2.1 2700"});
}

TEST_CASE("rating list: CSV, Ordo's or a plain name,elo; semicolons with decimal commas") {
    CHECK(read("\"#\",\"PLAYER\",\"RATING\",\"ERROR\",\"POINTS\",\"PLAYED\",\"(%)\"\n"
               "1,\"Stockfish 13\",4098.1,\"50.7\",1157.00,1203,96.18\n"
               "2,\"Engine, with a comma\",4058.9,\"82.7\",610.00,630,96.83\n") ==
          std::vector<std::string>{"Stockfish 13 4098", "Engine, with a comma 4059"});
    CHECK(read("name,elo\nStockfish 17,3650\nFruit 2.1,2690\nNo rating,\nToo strong,9000\n") ==
          std::vector<std::string>{"Stockfish 17 3650", "Fruit 2.1 2690"});
    CHECK(read("Elo;Engine\n3650,4;Stockfish 17\n2689,6;Fruit 2.1\n") ==
          std::vector<std::string>{"Stockfish 17 3650", "Fruit 2.1 2690"});
    CHECK(read("Stockfish 17,3650\nStockfish 17,3600\n") == std::vector<std::string>{"Stockfish 17 3650"});
    CHECK(read("").empty());
    CHECK(read("Nothing here.\nNor here, 42 times over.\n").empty());
}

TEST_CASE("rating list: the library engines it names, by the names, letter case and spaces aside") {
    const std::vector<ListedRating> list = {{"Stockfish 17", 3650},    {"stockfish  17", 3600}, {"Fruit 2.1", 2690},
                                            {"komodo dragon 3", 3600}, {"Crafty 25.2", 2850},   {"ethereal 14", 3480}};
    const std::vector<std::string> library = {"Stockfish 17", "Fruit 2.1",   "Komodo Dragon 3",
                                              "Ethereal 14",  "ETHEREAL 14", "Crafty"};
    const auto m                           = matchRatings(list, library);
    REQUIRE(m.matched.size() == 3);
    CHECK(m.matched[0].engine == "Stockfish 17");
    CHECK(m.matched[0].elo == 3650);  // its own name, not "stockfish  17"
    CHECK(m.matched[1].engine == "Fruit 2.1");
    CHECK(m.matched[2].engine == "Komodo Dragon 3");
    CHECK(m.matched[2].listed == "komodo dragon 3");
    // Crafty 25.2 is not Crafty; two library engines are "ethereal 14": neither is guessed.
    CHECK(m.unmatched == std::vector<std::string>{"Crafty 25.2", "ethereal 14"});

    // The folded name first, then the engine's own: its own wins.
    const auto own = matchRatings({{"FRUIT 2.1", 2600}, {"Fruit 2.1", 2690}}, library);
    REQUIRE(own.matched.size() == 1);
    CHECK(own.matched[0].elo == 2690);
}
