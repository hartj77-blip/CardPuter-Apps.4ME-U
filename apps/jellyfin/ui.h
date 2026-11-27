// What the Jellyfin app's screens are made of: cards with their cover art,
// rows of cards that scroll sideways (Home, Search), and a grid of them (a
// library), in Jellyfin's dark theme.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "apps/jellyfin/api.h"
#include "apps/jellyfin/art.h"
#include "apps/jellyfin/work.h"
#include "lvgl.h"

namespace zs::jfapp {

// Jellyfin's dark theme and logo colours.
constexpr uint32_t kJfBg = 0x101010;       // page
constexpr uint32_t kJfCard = 0x242424;     // a card whose picture hasn't come yet
constexpr uint32_t kJfRaised = 0x303030;   // dialogs, troughs
constexpr uint32_t kJfBar = 0x202020;      // the now-playing bar
constexpr uint32_t kJfText = 0xffffff;
constexpr uint32_t kJfDim = 0x9a9a9a;      // secondary text
constexpr uint32_t kJfBlue = 0x00a4dc;     // the accent
constexpr uint32_t kJfPurple = 0xaa5cc3;   // the logo's other end

// A plain rectangle: no border, no padding, no scrolling.
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, int radius);

// The shape of a card, as Jellyfin's: 16:9 for libraries, episodes and
// videos; 2:3 for film and show posters; square for music.
enum class Shape { Wide, Poster, Square };
// The shape that suits most of a list.
Shape shape_for(const std::vector<Item>& items);
const char* icon_for(const Item& item);

// A picture ready to draw: an LVGL image over its own pixels.
struct ArtImage {
    Pixels px;
    lv_image_dsc_t dsc{};
};
using ArtPtr = std::shared_ptr<ArtImage>;
ArtPtr make_art(Pixels&& px);

// Every picture the app shows. Asked for by the cards on screen, fetched
// several at a time off the screen's thread, kept in memory while they are
// likely to be shown again and in ~/.cache/zeroos/jellyfin between visits.
class ArtStore {
public:
    // demo: the simulator. Pictures are drawn from each item's name.
    void start(Worker* worker, const std::string& server, const std::string& auth, const std::string& cache_dir,
               const std::string& tmp_dir, bool demo);
    void stop();

    enum State { None, Loading, Ready };
    // The picture of `item` for a card of this shape, w x h. Ready: *out is
    // it. Loading: asked for; serial() changes when it is here. None: the
    // item has no picture (or the server has none after all).
    State get(const Item& item, Shape shape, int w, int h, ArtPtr& out);
    // Send for what get() asked for since the last flush().
    void flush();
    // Changes whenever pictures arrive (or turn out not to exist).
    uint32_t serial() const { return serial_; }
    // Ask again for the pictures the server didn't have or couldn't send.
    void retry();

private:
    struct Want {
        std::string key;
        Pic pic;
        std::string name;
        Kind kind = Kind::Track;
        int w = 0, h = 0;
    };
    struct Entry {
        ArtPtr art;
        uint32_t used = 0;
    };
    void trim();

    Worker* worker_ = nullptr;
    std::string server_, auth_, cache_dir_, tmp_dir_;
    bool demo_ = false;
    std::vector<Want> wanted_;
    std::unordered_set<std::string> in_flight_, missing_;
    std::unordered_map<std::string, uint32_t> failed_;   // key -> when (lv_tick)
    std::unordered_map<std::string, Entry> mem_;
    size_t bytes_ = 0;
    uint32_t serial_ = 0, clock_ = 0;
    // Jobs outlive a sign-out: they check this before touching the store.
    std::shared_ptr<int> alive_;
};

// One card's widgets. Made once, then bound to an item (a grid binds the
// same card to other items as it scrolls).
struct CardView {
    lv_obj_t* box = nullptr;      // the frame; the colour of a card with no picture
    lv_obj_t* img = nullptr;      // the picture
    lv_obj_t* icon = nullptr;     // with no picture: what kind of thing it is
    lv_obj_t* name = nullptr;     // with no picture: its name
    lv_obj_t* bar = nullptr;      // how far you watched
    lv_obj_t* bar_fill = nullptr;
    lv_obj_t* badge = nullptr;    // watched, or how many episodes are left
    lv_obj_t* badge_text = nullptr;
    lv_obj_t* label = nullptr;    // the name under the card
    ArtPtr art;
    int w = 0, h = 0;
    bool name_on_card = true;
    bool loaded = false;          // the picture is on, or there is none to wait for
};
void card_create(CardView& c, lv_obj_t* parent, int x, int y, int w, int h, bool label_below);
// The texts, badges and placeholder of `item`. progress: permille watched, -1 for none.
void card_bind(CardView& c, const Item& item, const std::string& label);
// The picture: shows it if it is here, asks for it otherwise.
void card_load(CardView& c, const Item& item, Shape shape, ArtStore& art);
void card_focus(CardView& c, bool on);
void card_show(CardView& c, bool on);

// Rows of cards, each row under its title, scrolling sideways; the page
// scrolls so the row in focus starts at the top (Home, Search).
struct Section {
    std::string key;         // stays the same across reloads: "media", "resume", "latest:<id>"
    std::string title;
    Shape shape = Shape::Wide;
    std::vector<Item> items;
    bool labels = false;     // names under the cards
};

class RowsView {
public:
    // What to say about the card in focus, at the right of its row's title.
    using Caption = std::function<std::string(const Section&, const Item&)>;

    void build(lv_obj_t* parent, int w, int h, std::vector<Section> sections, ArtStore* art, Caption caption);
    void reset();   // the widgets went with the page
    bool built() const { return page_ != nullptr; }
    bool empty() const { return sections_.empty(); }
    void set_height(int h);

    bool key(uint32_t key);   // the arrows
    const Section* section() const;
    const Item* item() const;
    int row() const { return row_; }
    int col() const { return col_; }
    // Focus the same card again after a rebuild.
    std::string focus_token() const;
    void focus_by_token(const std::string& token);
    void focus(int row, int col);
    // Show or ask for the pictures of the cards on screen.
    void load_art();
    const std::vector<Section>& sections() const { return sections_; }

private:
    struct RowObjs {
        lv_obj_t* title = nullptr;
        lv_obj_t* caption = nullptr;
        lv_obj_t* row = nullptr;
        std::vector<CardView> cards;
        int y = 0, pitch = 0, card_w = 0;
        int scroll = 0;
        int col = 0;   // where focus was last in this row
    };
    void apply_focus();

    lv_obj_t* page_ = nullptr;
    std::vector<Section> sections_;
    std::vector<RowObjs> rows_;
    ArtStore* art_ = nullptr;
    Caption caption_;
    int w_ = 0, h_ = 0;
    int row_ = 0, col_ = 0;
};

// A grid of cards with the name of the one in focus above it. Typing
// narrows it, as in every ZeroOS list. Only the rows on screen have
// widgets, however long the list.
class GridView {
public:
    // The focused item's name and, on the right, what else to say.
    using Caption = std::function<void(const Item&, std::string& title, std::string& detail)>;

    void build(lv_obj_t* parent, int w, int h, Shape shape, const std::vector<Item>* items, ArtStore* art,
               Caption caption);
    void reset();          // another list: forget what was typed too
    void widgets_gone();   // the widgets went with the page
    bool built() const { return page_ != nullptr; }
    // The list grew, or its items changed.
    void items_changed();
    // Shown in place of the cards when there are none ("Loading...").
    void set_note(const std::string& note);

    // Arrows; letters and digits (the filter); Backspace while there is one.
    bool key(uint32_t key);
    int focused() const;       // index in items, -1 for none
    void focus(int index);
    const std::string& filter() const { return filter_; }
    void load_art();

private:
    void apply_filter();
    void bind();
    void update_caption();

    lv_obj_t* page_ = nullptr;
    lv_obj_t *title_ = nullptr, *detail_ = nullptr, *note_ = nullptr, *thumb_ = nullptr;
    std::vector<CardView> cells_;
    std::vector<int> bound_;        // the item each cell shows, -1 for none
    const std::vector<Item>* items_ = nullptr;
    std::vector<int> shown_;        // the items the filter leaves
    ArtStore* art_ = nullptr;
    Caption caption_;
    Shape shape_ = Shape::Poster;
    std::string filter_, note_text_;
    int w_ = 0, h_ = 0, cols_ = 1, rows_ = 1, full_rows_ = 1, pitch_x_ = 1, pitch_y_ = 1;
    int top_ = 0;      // the first row of cards on screen
    int cursor_ = 0;   // index in shown_
};

}  // namespace zs::jfapp
