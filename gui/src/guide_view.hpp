#pragma once

// The guide panel (docs/design/help.md, "The panel"): the guide of the interface's language at the right of the
// page, one section with its subsections at a time, with its contents, its search and its history.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "guide.hpp"
#include "markdown.hpp"

namespace fcg {

class GuideView {
   public:
    ~GuideView();

    // Opens the panel at the English section `id` (a topic), highlighted a moment; the history keeps where it was.
    void open(const std::string& id);
    // F1: opens it at `id`, or closes it when it shows that section already.
    void toggle(const std::string& id);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    // Whether the mouse was over it in the last frame.
    [[nodiscard]] bool hovered() const noexcept { return open_ && hovered_; }
    // Opens it on the search of `query`.
    void search(const std::string& query);
    // The English section shown (the one asked for last), and the search typed.
    [[nodiscard]] const std::string& current() const noexcept { return id_; }
    [[nodiscard]] std::string query() const { return query_; }

    // The width the panel takes of `available` pixels, 0 when it is closed.
    [[nodiscard]] float width(float available) const;
    // Draws it at the cursor, `width` x `height` pixels.
    void draw(float width, float height);

    // Its width in font sizes (0: the default), kept in settings.json through on_width when dragged.
    float width_em = 0;
    std::function<void(float)> on_width;
    // Opens a web page (the program's SDL_OpenURL; replaced by the tests).
    std::function<void(const std::string&)> open_url;

   private:
    struct Section {
        guide::Fallback fallback = guide::Fallback::None;
        const guide::Doc* doc    = nullptr;  // the guide its text comes from
        std::string title;
        int level = 0;
        std::vector<md::Block> blocks;
    };
    struct Layout {
        float width = -1, size = 0;
        float height = 0;
        std::vector<md::Line> lines;                              // a text block
        std::vector<float> columns;                               // a table: each column's width
        std::vector<float> row_heights;                           // and each row's
        std::vector<std::vector<std::vector<md::Line>>> cells;    // and the lines of each cell
    };
    struct Result {
        std::size_t index = 0;
        std::string title;
        std::string snippet;
        int score = 0;
    };
    struct Entry {  // a section as the search reads it
        std::size_t index = 0;
        std::string title, text;
    };

    void navigate(const std::string& id, bool record);
    void refreshLanguage();
    const Section& section(std::size_t english_index);
    void runSearch();
    void drawHeader(float width);
    void drawContents();
    void drawResults(float width);
    void drawPage(float width);
    float drawBlock(const md::Block& block, const Section& from, float width);
    float drawText(const std::vector<md::Run>& runs, const std::vector<md::Line>& lines, ImVec2 origin, float size,
                   bool heading, const Section& from);
    const Layout& layout(const md::Block& block, float width, float size);
    void link(const std::string& target, const Section& from);
    ImTextureID picture(const std::string& src, int* w, int* h);

    bool open_ = false;
    std::string id_;  // the English section asked for
    std::vector<std::string> back_, forward_;
    // Frames left to hold the section asked for at the top, while the page settles (see drawPage).
    int scroll_   = 0;
    double flash_ = 0;        // the section asked for is highlighted until then
    char query_[200] = {};
    std::string results_for_;  // the query the results were made for
    std::vector<Result> results_;
    std::vector<std::u32string> highlight_;  // the words highlighted in the sections
    bool searching_    = false;  // the results shown rather than a section
    bool focus_search_ = false;
    bool dragging_     = false;
    bool hovered_      = false;

    std::string code_;  // the language of the caches below
    std::map<std::size_t, Section> sections_;
    std::vector<Entry> entries_;
    std::map<const void*, Layout> layouts_;
    struct Picture {
        ImTextureID texture = ImTextureID_Invalid;
        int w = 0, h = 0;
    };
    std::map<std::string, Picture> pictures_;
};

}  // namespace fcg
