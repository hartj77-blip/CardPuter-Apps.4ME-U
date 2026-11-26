// See ui.h.
#include "apps/jellyfin/ui.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "apps/common.h"
#include "platform/keys.h"

namespace zs::jfapp {
using namespace theme;

namespace {

constexpr int kGap = 4;                        // between cards
constexpr size_t kMemMax = 4 * 1024 * 1024;    // pictures kept in memory
constexpr size_t kMemKeep = 3 * 1024 * 1024;
constexpr off_t kDiskMax = 24 * 1024 * 1024;   // and in ~/.cache
constexpr off_t kDiskKeep = 16 * 1024 * 1024;
constexpr size_t kBatch = 8;                   // pictures fetched by one curl
constexpr uint32_t kRetryMs = 20000;           // before asking again for one that failed

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && out.size() < 2 * 1024 * 1024) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Not fsync'd: a cover lost to a power cut is fetched again.
void write_cache_file(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) std::remove(tmp.c_str());
}

// Keeps the picture folder under kDiskMax: the oldest go first.
void prune_cache(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct File {
        std::string path;
        time_t mtime;
        off_t size;
    };
    std::vector<File> files;
    off_t total = 0;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string path = dir + "/" + name;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        // A .tmp left by a visit that was cut short.
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) {
            std::remove(path.c_str());
            continue;
        }
        files.push_back({path, st.st_mtime, st.st_size});
        total += st.st_size;
    }
    closedir(d);
    if (total <= kDiskMax) return;
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.mtime < b.mtime; });
    for (const File& f : files) {
        if (total <= kDiskKeep) break;
        if (std::remove(f.path.c_str()) == 0) total -= f.size;
    }
}

lv_color_t c_(uint32_t hex) { return lv_color_hex(hex); }

// Lower case, with punctuation as spaces: "Solder & Flux" and "solder flux"
// are the same words.
std::string plain(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch >= 0x80 || std::isalnum(ch)) o += char(std::tolower(ch));
        else if (!o.empty() && o.back() != ' ') o += ' ';
    }
    return o;
}

// What typing finds, as in every ZeroOS list: each word typed is somewhere
// in the text, in any order. (ZeroOS has this as search_score(), but not in
// every ZeroOS this app is installed on.)
bool matches(const std::string& typed, const std::string& text) {
    const std::string hay = plain(text), want = plain(typed) + " ";
    std::string word;
    for (char ch : want) {
        if (ch != ' ') {
            word += ch;
            continue;
        }
        if (!word.empty() && hay.find(word) == std::string::npos) return false;
        word.clear();
    }
    return true;
}

// The name under a card: an episode goes by its show there (which episode
// is in the caption).
std::string card_label(const Item& it) {
    return it.kind == Kind::Episode && !it.detail.empty() ? it.detail : it.name;
}

void show(lv_obj_t* o, bool on) {
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// A label of fixed size that ends in dots when the text doesn't fit.
lv_obj_t* fixed_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int w, int lines) {
    lv_obj_t* l = label(parent, "", font, color);
    lv_obj_set_size(l, w, lv_font_get_line_height(font) * lines);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

struct Size {
    int w, h;
};
Size row_card(Shape s) {
    return s == Shape::Wide ? Size{92, 52} : s == Shape::Poster ? Size{46, 69} : Size{56, 56};
}
Size grid_card(Shape s) {
    // Squares at 45: two whole rows of albums fit under the caption.
    return s == Shape::Wide ? Size{100, 56} : s == Shape::Poster ? Size{46, 69} : Size{45, 45};
}
int grid_cols(Shape s) { return s == Shape::Wide ? 3 : 6; }

}  // namespace

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    style_plain(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c_(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

Shape shape_for(const std::vector<Item>& items) {
    int wide = 0, poster = 0, square = 0;
    for (const Item& it : items) {
        switch (it.kind) {
            case Kind::Movie: case Kind::Series: case Kind::Season: ++poster; break;
            case Kind::Album: case Kind::Artist: case Kind::Playlist: case Kind::Track: ++square; break;
            case Kind::Folder:
                if (it.detail == "boxsets") ++poster;
                else ++wide;
                break;
            default: ++wide;
        }
    }
    if (poster >= wide && poster >= square && poster > 0) return Shape::Poster;
    if (square >= wide && square > 0) return Shape::Square;
    return Shape::Wide;
}

const char* icon_for(const Item& item) {
    switch (item.kind) {
        case Kind::Track: case Kind::Album: case Kind::Artist: return LV_SYMBOL_AUDIO;
        case Kind::Playlist: return LV_SYMBOL_LIST;
        case Kind::Movie: case Kind::Series: case Kind::Season: case Kind::Episode: case Kind::Video:
            return LV_SYMBOL_VIDEO;
        case Kind::Folder: break;
    }
    const std::string& t = item.detail;
    if (t == "music" || t == "browse:albums" || t == "browse:songs") return LV_SYMBOL_AUDIO;
    if (t == "browse:artists") return LV_SYMBOL_DIRECTORY;
    if (t == "movies" || t == "tvshows" || t == "homevideos" || t == "musicvideos" || t == "browse:movies" ||
        t == "browse:shows")
        return LV_SYMBOL_VIDEO;
    if (t == "playlists" || t == "browse:playlists") return LV_SYMBOL_LIST;
    if (t == "books") return LV_SYMBOL_FILE;
    if (t == "browse:favorites") return LV_SYMBOL_OK;
    if (t == "browse:latest") return LV_SYMBOL_BELL;
    if (t.compare(0, 7, "search:") == 0) return ZS_ICON_SEARCH;
    return LV_SYMBOL_DIRECTORY;
}

ArtPtr make_art(Pixels&& px) {
    auto a = std::make_shared<ArtImage>();
    a->px = std::move(px);
    a->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    a->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    a->dsc.header.w = uint32_t(a->px.w);
    a->dsc.header.h = uint32_t(a->px.h);
    a->dsc.header.stride = uint32_t(a->px.w * 2);
    a->dsc.data = reinterpret_cast<const uint8_t*>(a->px.px.data());
    a->dsc.data_size = uint32_t(a->px.px.size() * 2);
    return a;
}

// ---- ArtStore -----------------------------------------------------------

void ArtStore::start(Worker* worker, const std::string& server, const std::string& auth,
                     const std::string& cache_dir, const std::string& tmp_dir, bool demo) {
    stop();
    worker_ = worker;
    server_ = server;
    auth_ = auth;
    cache_dir_ = cache_dir;
    tmp_dir_ = tmp_dir;
    demo_ = demo;
    alive_ = std::make_shared<int>(0);
    if (!demo_ && worker_) {
        std::string dir = cache_dir_;
        worker_->post(Worker::Normal, [dir](std::atomic<int>&) { prune_cache(dir); }, [] {});
    }
}

void ArtStore::stop() {
    alive_.reset();
    wanted_.clear();
    in_flight_.clear();
    missing_.clear();
    failed_.clear();
    mem_.clear();
    bytes_ = 0;
    ++serial_;
}

void ArtStore::retry() {
    missing_.clear();
    failed_.clear();
    ++serial_;
}

ArtStore::State ArtStore::get(const Item& item, Shape shape, int w, int h, ArtPtr& out) {
    if (!worker_ || !alive_) return None;
    const Pic& pic = shape == Shape::Wide ? item.wide : item.poster;
    std::string key, name = item.name;
    Kind kind = item.kind;
    if (demo_) {
        // The simulator's items have no pictures: draw one from the name.
        // Not for the cards that aren't things on the server (Albums, a search).
        if (item.id.empty()) return None;
        // A track wears its album's cover.
        const bool on_album = item.kind == Kind::Track && !item.album.empty();
        name = on_album ? item.album : item.name;
        kind = on_album ? Kind::Album : item.kind;
        key = "demo/" + name + "/" + std::to_string(int(kind)) + "/" + std::to_string(w) + "x" + std::to_string(h);
    } else {
        key = cache_name(pic, w, h);
        if (key.empty()) return None;
    }
    auto hit = mem_.find(key);
    if (hit != mem_.end()) {
        hit->second.used = ++clock_;
        out = hit->second.art;
        return Ready;
    }
    if (missing_.count(key)) return None;
    auto failed = failed_.find(key);
    if (failed != failed_.end()) {
        if (lv_tick_elaps(failed->second) < kRetryMs) return None;
        failed_.erase(failed);
    }
    if (in_flight_.insert(key).second) wanted_.push_back(Want{key, pic, name, kind, w, h});
    return Loading;
}

void ArtStore::flush() {
    if (!worker_ || !alive_) return;
    struct Out {
        ArtPtr art;
        bool missing = false;
        bool tried = false;
    };
    while (!wanted_.empty()) {
        size_t n = std::min(kBatch, wanted_.size());
        // The newest wishes first: they are for what is on screen now.
        std::vector<Want> batch(wanted_.end() - long(n), wanted_.end());
        wanted_.resize(wanted_.size() - n);
        auto outs = std::make_shared<std::vector<Out>>(batch.size());
        std::weak_ptr<int> alive = alive_;
        const std::string server = server_, auth = auth_, cache_dir = cache_dir_, tmp_dir = tmp_dir_;
        const bool demo = demo_;
        worker_->post(
            Worker::Cover,
            [batch, outs, server, auth, cache_dir, tmp_dir, demo](std::atomic<int>& pid) {
                std::vector<std::string> urls;
                std::vector<size_t> which;
                for (size_t i = 0; i < batch.size(); ++i) {
                    const Want& w = batch[i];
                    Out& o = (*outs)[i];
                    o.tried = true;
                    Pixels px;
                    if (demo) {
                        demo_picture(w.name, w.kind, w.w, w.h, px);
                        o.art = make_art(std::move(px));
                        continue;
                    }
                    std::string bytes;
                    if (read_file(cache_dir + "/" + w.key, bytes) && cache_decode(bytes, w.w, w.h, px)) {
                        o.art = make_art(std::move(px));
                        continue;
                    }
                    std::string path = image_path(w.pic, w.w, w.h);
                    if (path.empty()) {
                        o.missing = true;
                        continue;
                    }
                    urls.push_back(server + path);
                    which.push_back(i);
                }
                if (urls.empty()) return;
                std::vector<Reply> replies = http_get_all(urls, auth, tmp_dir, &pid, 15);
                for (size_t k = 0; k < replies.size(); ++k) {
                    const Want& w = batch[which[k]];
                    Out& o = (*outs)[which[k]];
                    const Reply& r = replies[k];
                    Pixels px;
                    if (r.status == 200 && decode_cover(r.body, w.w, w.h, px)) {
                        write_cache_file(cache_dir + "/" + w.key, cache_encode(px));
                        o.art = make_art(std::move(px));
                    } else if (r.status >= 200 && r.status != 401 && r.status != 429 && r.status != 503) {
                        o.missing = true;   // the server has no such picture, or not one we can read
                    }
                    // Anything else (no answer, starting up): try again later.
                }
            },
            [this, alive, batch, outs] {
                if (alive.expired()) return;
                for (size_t i = 0; i < batch.size(); ++i) {
                    const std::string& key = batch[i].key;
                    Out& o = (*outs)[i];
                    in_flight_.erase(key);
                    if (!o.tried) continue;   // dropped before it started
                    if (o.art) {
                        bytes_ += o.art->px.px.size() * 2;
                        mem_[key] = Entry{o.art, ++clock_};
                    } else if (o.missing) {
                        missing_.insert(key);
                    } else {
                        failed_[key] = lv_tick_get();
                    }
                }
                ++serial_;
                trim();
            });
    }
}

void ArtStore::trim() {
    if (bytes_ <= kMemMax) return;
    // Least recently shown first; never one a card still holds.
    std::vector<std::pair<uint32_t, std::string>> old;
    for (auto& e : mem_)
        if (e.second.art.use_count() == 1) old.push_back({e.second.used, e.first});
    std::sort(old.begin(), old.end());
    for (auto& o : old) {
        if (bytes_ <= kMemKeep) break;
        auto it = mem_.find(o.second);
        bytes_ -= std::min(bytes_, it->second.art->px.px.size() * 2);
        mem_.erase(it);
    }
}

// ---- Cards --------------------------------------------------------------

void card_create(CardView& c, lv_obj_t* parent, int x, int y, int w, int h, bool label_below) {
    c = CardView{};
    c.w = w;
    c.h = h;
    c.box = panel(parent, x, y, w, h, kJfCard, 0);
    // In focus: a blue frame round the card, as Jellyfin marks focus on a TV.
    lv_obj_set_style_outline_color(c.box, c_(kJfBlue), 0);
    lv_obj_set_style_outline_width(c.box, 0, 0);
    lv_obj_set_style_outline_pad(c.box, 0, 0);
    c.img = lv_image_create(c.box);
    lv_obj_set_pos(c.img, 0, 0);
    show(c.img, false);
    c.icon = label(c.box, "", &zs_font_16, kJfDim);
    c.name = fixed_label(c.box, &zs_font_12, kJfText, w - 4, h >= 48 ? 2 : 1);
    lv_obj_set_style_text_align(c.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c.name, LV_ALIGN_BOTTOM_MID, 0, -2);
    if (label_below) {
        c.label = fixed_label(parent, &zs_font_12, kJfDim, w, 1);
        lv_obj_set_style_text_align(c.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(c.label, x, y + h + 1);
        c.name_on_card = false;
    }
}

void card_bind(CardView& c, const Item& item, const std::string& label_text) {
    c.art.reset();
    c.loaded = false;
    show(c.img, false);
    lv_label_set_text(c.icon, icon_for(item));
    lv_label_set_text(c.name, item.name.c_str());
    if (c.label) lv_label_set_text(c.label, label_text.c_str());

    // How far you got: a blue line along the bottom.
    bool part = item.playable_video() && item.resume_s > 0 && item.duration_s > 0;
    if (part && !c.bar) {
        c.bar = panel(c.box, 0, c.h - 3, c.w, 3, 0x000000, 0);
        lv_obj_set_style_bg_opa(c.bar, LV_OPA_60, 0);
        c.bar_fill = panel(c.bar, 0, 0, 1, 3, kJfBlue, 0);
    }
    if (part) {
        int fill = std::clamp(int(long(c.w) * item.resume_s / item.duration_s), 2, c.w);
        lv_obj_set_width(c.bar_fill, fill);
    }
    show(c.bar, part);

    // Watched: a tick. A show or season with episodes left: how many.
    bool group = item.kind == Kind::Series || item.kind == Kind::Season;
    bool tick = item.played && (item.playable_video() || group);
    bool count = !tick && group && item.unplayed > 0;
    if ((tick || count) && !c.badge) {
        c.badge = panel(c.box, 0, 2, 14, 13, kJfBlue, 6);
        c.badge_text = label(c.badge, "", &zs_font_12, kJfText);
    }
    if (tick || count) {
        std::string t = tick ? std::string(LV_SYMBOL_OK) : std::to_string(std::min(item.unplayed, 999));
        lv_label_set_text(c.badge_text, t.c_str());
        int bw = tick ? 14 : 8 + 6 * int(t.size());
        lv_obj_set_size(c.badge, bw, 13);
        lv_obj_set_pos(c.badge, c.w - bw - 2, 2);
        lv_obj_center(c.badge_text);
    }
    show(c.badge, tick || count);
}

void card_load(CardView& c, const Item& item, Shape shape, ArtStore& art) {
    if (c.loaded || !c.box) return;
    ArtPtr a;
    ArtStore::State st = art.get(item, shape, c.w, c.h, a);
    if (st == ArtStore::Ready) {
        c.art = a;
        lv_image_set_src(c.img, &a->dsc);
        show(c.img, true);
        show(c.icon, false);
        show(c.name, false);
        c.loaded = true;
        return;
    }
    bool waiting = st == ArtStore::Loading;
    // No picture: Jellyfin's coloured card with the name on it. One still
    // on its way: a plain dark card, so it doesn't flash a colour first.
    lv_obj_set_style_bg_color(c.box, c_(waiting ? kJfCard : tint_for(item.name)), 0);
    bool named = !waiting && c.name_on_card;
    show(c.icon, true);
    show(c.name, named);
    if (named) {
        // The icon over the name, the pair in the middle of the card.
        int ih = lv_font_get_line_height(&zs_font_16) - 2, nh = lv_obj_get_style_height(c.name, 0);
        int y = std::max(0, (c.h - ih - nh) / 2);
        lv_obj_align(c.icon, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_align(c.name, LV_ALIGN_TOP_MID, 0, y + ih);
    } else {
        lv_obj_center(c.icon);
    }
    c.loaded = !waiting;
}

void card_focus(CardView& c, bool on) {
    if (!c.box) return;
    lv_obj_set_style_outline_width(c.box, on ? 2 : 0, 0);
    if (c.label) lv_obj_set_style_text_color(c.label, c_(on ? kJfText : kJfDim), 0);
}

void card_show(CardView& c, bool on) {
    show(c.box, on);
    show(c.label, on);
}

// ---- RowsView -----------------------------------------------------------

void RowsView::reset() {
    page_ = nullptr;
    rows_.clear();
    sections_.clear();
}

void RowsView::build(lv_obj_t* parent, int w, int h, std::vector<Section> sections, ArtStore* art,
                     Caption caption) {
    reset();
    sections_ = std::move(sections);
    art_ = art;
    caption_ = std::move(caption);
    w_ = w;
    h_ = h;
    page_ = lv_obj_create(parent);
    style_plain(page_);
    lv_obj_set_pos(page_, 0, 0);
    lv_obj_set_size(page_, w, h);
    lv_obj_set_scrollbar_mode(page_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page_, LV_DIR_VER);

    constexpr int kTitleH = 16;
    int y = 0, last_h = 0;
    for (const Section& sec : sections_) {
        RowObjs r;
        Size cs = row_card(sec.shape);
        r.y = y;
        r.card_w = cs.w;
        r.pitch = cs.w + kGap;
        r.title = label(page_, sec.title.c_str(), &zs_font_14, kJfText);
        lv_obj_set_pos(r.title, 6, y);
        lv_obj_update_layout(r.title);
        int tw = lv_obj_get_width(r.title);
        int cw = std::max(20, w - 12 - tw - 10);
        r.caption = fixed_label(page_, &zs_font_12, kJfDim, cw, 1);
        lv_obj_set_style_text_align(r.caption, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(r.caption, w - 6 - cw, y + 2);
        y += kTitleH;

        int row_h = cs.h + 6 + (sec.labels ? 14 : 0);
        r.row = lv_obj_create(page_);
        style_plain(r.row);
        lv_obj_set_pos(r.row, 0, y);
        lv_obj_set_size(r.row, w, row_h);
        lv_obj_set_scrollbar_mode(r.row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(r.row, LV_DIR_HOR);
        r.cards.resize(sec.items.size());
        for (size_t i = 0; i < sec.items.size(); ++i) {
            card_create(r.cards[i], r.row, 6 + int(i) * r.pitch, 3, cs.w, cs.h, sec.labels);
            card_bind(r.cards[i], sec.items[i], card_label(sec.items[i]));
        }
        // Room after the last card, so it can scroll fully into view.
        panel(r.row, 6 + int(sec.items.size()) * r.pitch, 3, 2, 2, kJfBg, 0);
        y += row_h + 3;
        last_h = kTitleH + row_h + 3;
        rows_.push_back(std::move(r));
    }
    // Room under the last row, so it too can scroll up to the top.
    if (h - last_h > 0) panel(page_, 0, y, 1, h - last_h, kJfBg, 0);
    row_ = col_ = 0;
    apply_focus();
}

void RowsView::set_height(int h) {
    h_ = h;
    if (page_) lv_obj_set_height(page_, h);
    apply_focus();
}

const Section* RowsView::section() const {
    return row_ >= 0 && row_ < int(sections_.size()) ? &sections_[size_t(row_)] : nullptr;
}

const Item* RowsView::item() const {
    const Section* s = section();
    return s && col_ >= 0 && col_ < int(s->items.size()) ? &s->items[size_t(col_)] : nullptr;
}

std::string RowsView::focus_token() const {
    const Section* s = section();
    const Item* it = item();
    if (!s || !it) return "";
    return s->key + "\n" + (it->id.empty() ? it->name : it->id);
}

void RowsView::focus_by_token(const std::string& token) {
    auto nl = token.find('\n');
    if (nl == std::string::npos) return;
    std::string key = token.substr(0, nl), id = token.substr(nl + 1);
    for (size_t r = 0; r < sections_.size(); ++r) {
        if (sections_[r].key != key) continue;
        int col = 0;
        for (size_t i = 0; i < sections_[r].items.size(); ++i) {
            const Item& it = sections_[r].items[i];
            if ((it.id.empty() ? it.name : it.id) == id) col = int(i);
        }
        focus(int(r), col);
        return;
    }
}

void RowsView::focus(int row, int col) {
    if (rows_.empty()) return;
    // The card that had the frame gives it up.
    if (row_ >= 0 && row_ < int(rows_.size()) && col_ >= 0 && col_ < int(rows_[size_t(row_)].cards.size()))
        card_focus(rows_[size_t(row_)].cards[size_t(col_)], false);
    row_ = row;
    col_ = col;
    apply_focus();
}

void RowsView::apply_focus() {
    if (rows_.empty() || !page_) return;
    row_ = std::clamp(row_, 0, int(rows_.size()) - 1);
    RowObjs& r = rows_[size_t(row_)];
    col_ = std::clamp(col_, 0, std::max(0, int(r.cards.size()) - 1));
    r.col = col_;
    for (auto& other : rows_) lv_label_set_text(other.caption, "");
    if (!r.cards.empty()) {
        card_focus(r.cards[size_t(col_)], true);
        int x = 6 + col_ * r.pitch;
        if (x - 6 < r.scroll) r.scroll = x - 6;
        if (x + r.card_w + 6 > r.scroll + w_) r.scroll = x + r.card_w + 6 - w_;
        lv_obj_scroll_to_x(r.row, r.scroll, LV_ANIM_OFF);
        if (caption_) lv_label_set_text(r.caption, caption_(sections_[size_t(row_)], *item()).c_str());
    }
    // The page scrolls so the row starts at the top, title and all.
    lv_obj_scroll_to_y(page_, std::max(0, r.y - 1), LV_ANIM_OFF);
    load_art();
}

void RowsView::load_art() {
    if (rows_.empty() || !art_ || !page_) return;
    int top = std::max(0, rows_[size_t(row_)].y - 1);
    for (size_t i = 0; i < rows_.size(); ++i) {
        RowObjs& r = rows_[i];
        if (r.y + 16 < top - 8 || r.y > top + h_) continue;   // not on screen
        int first = std::max(0, r.scroll / r.pitch - 1);
        int last = std::min(int(r.cards.size()) - 1, (r.scroll + w_) / r.pitch + 1);
        for (int k = first; k <= last; ++k)
            card_load(r.cards[size_t(k)], sections_[i].items[size_t(k)], sections_[i].shape, *art_);
    }
    art_->flush();
}

bool RowsView::key(uint32_t key) {
    if (rows_.empty()) return false;
    int row = row_, col = col_;
    switch (key) {
        case LV_KEY_LEFT: col = std::max(0, col - 1); break;
        case LV_KEY_RIGHT: col = std::min(int(rows_[size_t(row_)].cards.size()) - 1, col + 1); break;
        case LV_KEY_UP:
            if (row == 0) return true;
            col = rows_[size_t(--row)].col;
            break;
        case LV_KEY_DOWN:
            if (row + 1 >= int(rows_.size())) return true;
            col = rows_[size_t(++row)].col;
            break;
        case LV_KEY_HOME: col = 0; break;
        case LV_KEY_END: col = int(rows_[size_t(row_)].cards.size()) - 1; break;
        default: return false;
    }
    if (row != row_ || col != col_) focus(row, col);
    return true;
}

// ---- GridView -----------------------------------------------------------

void GridView::widgets_gone() {
    page_ = nullptr;
    title_ = detail_ = note_ = thumb_ = nullptr;
    cells_.clear();
    bound_.clear();
    shown_.clear();
    items_ = nullptr;
}

void GridView::reset() {
    widgets_gone();
    filter_.clear();
    note_text_.clear();
}

void GridView::build(lv_obj_t* parent, int w, int h, Shape shape, const std::vector<Item>* items, ArtStore* art,
                     Caption caption) {
    widgets_gone();
    items_ = items;
    art_ = art;
    caption_ = std::move(caption);
    shape_ = shape;
    w_ = w;
    h_ = h;
    page_ = lv_obj_create(parent);
    style_plain(page_);
    lv_obj_set_pos(page_, 0, 0);
    lv_obj_set_size(page_, w, h);
    lv_obj_remove_flag(page_, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int kCaptionH = 18;
    Size cs = grid_card(shape);
    cols_ = grid_cols(shape);
    pitch_x_ = cs.w + kGap;
    pitch_y_ = cs.h + kGap + 1;
    int x0 = (w - (cols_ * cs.w + (cols_ - 1) * kGap)) / 2;
    // The name of the card in focus, in line with the cards.
    title_ = fixed_label(page_, &zs_font_14, kJfText, w - 2 * x0 - 124, 1);
    lv_obj_set_pos(title_, x0, 0);
    detail_ = fixed_label(page_, &zs_font_12, kJfDim, 118, 1);
    lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(detail_, w - x0 - 118, 2);

    int gh = h - kCaptionH;
    full_rows_ = std::max(1, (gh + kGap) / pitch_y_);
    rows_ = std::max(full_rows_, (gh + pitch_y_ - 1) / pitch_y_);
    cells_.resize(size_t(rows_ * cols_));
    bound_.assign(cells_.size(), -1);
    for (int r = 0; r < rows_; ++r)
        for (int col = 0; col < cols_; ++col)
            card_create(cells_[size_t(r * cols_ + col)], page_, x0 + col * pitch_x_, kCaptionH + 2 + r * pitch_y_,
                        cs.w, cs.h, false);
    note_ = label(page_, note_text_.c_str(), &zs_font_14, kJfDim);
    lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
    // Where the screen is in a long list.
    thumb_ = panel(page_, w - 2, kCaptionH, 2, 8, kJfDim, 0);
    top_ = 0;
    cursor_ = 0;
    apply_filter();
}

void GridView::set_note(const std::string& note) {
    note_text_ = note;
    if (note_) {
        lv_label_set_text(note_, note.c_str());
        lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
    }
}

void GridView::items_changed() {
    if (!page_) return;
    std::fill(bound_.begin(), bound_.end(), -1);
    apply_filter();
}

void GridView::apply_filter() {
    if (!items_) return;
    int was = focused();
    shown_.clear();
    for (size_t i = 0; i < items_->size(); ++i) {
        const Item& it = (*items_)[i];
        if (!filter_.empty()) {
            std::string text = it.name + " " + it.detail + " " + it.album;
            if (it.year > 0) text += " " + std::to_string(it.year);
            if (!matches(filter_, text)) continue;
        }
        shown_.push_back(int(i));
    }
    cursor_ = 0;
    for (size_t i = 0; i < shown_.size(); ++i)
        if (shown_[i] == was) cursor_ = int(i);
    bind();
}

int GridView::focused() const {
    return cursor_ >= 0 && cursor_ < int(shown_.size()) ? shown_[size_t(cursor_)] : -1;
}

void GridView::focus(int index) {
    for (size_t i = 0; i < shown_.size(); ++i)
        if (shown_[i] == index) cursor_ = int(i);
    bind();
}

void GridView::bind() {
    if (!page_ || !items_) return;
    int n = int(shown_.size());
    cursor_ = std::clamp(cursor_, 0, std::max(0, n - 1));
    int row = cursor_ / cols_;
    if (row < top_) top_ = row;
    if (row >= top_ + full_rows_) top_ = row - full_rows_ + 1;
    top_ = std::max(0, top_);
    for (size_t k = 0; k < cells_.size(); ++k) {
        int i = top_ * cols_ + int(k);
        if (i >= n) {
            card_show(cells_[k], false);
            bound_[k] = -1;
            continue;
        }
        int idx = shown_[size_t(i)];
        if (bound_[k] != idx) {
            card_bind(cells_[k], (*items_)[size_t(idx)], "");
            bound_[k] = idx;
        }
        card_show(cells_[k], true);
        card_focus(cells_[k], i == cursor_);
    }
    show(note_, n == 0);
    if (n == 0 && !filter_.empty()) {
        lv_label_set_text(note_, ("Nothing matches \"" + filter_ + "\"").c_str());
        lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
    } else if (n == 0) {
        set_note(note_text_);
    }
    int total_rows = (n + cols_ - 1) / cols_;
    bool scrolls = total_rows > full_rows_;
    show(thumb_, scrolls);
    if (scrolls) {
        int track = h_ - 18;
        int th = std::max(8, track * full_rows_ / total_rows);
        lv_obj_set_height(thumb_, th);
        lv_obj_set_y(thumb_, 18 + (track - th) * top_ / std::max(1, total_rows - full_rows_));
    }
    update_caption();
    load_art();
}

void GridView::update_caption() {
    std::string t, d;
    int f = focused();
    if (f >= 0 && caption_) caption_((*items_)[size_t(f)], t, d);
    if (!filter_.empty()) d = std::string(ZS_ICON_SEARCH " ") + filter_ + fmt("  (%zu)", shown_.size());
    lv_label_set_text(title_, t.c_str());
    lv_label_set_text(detail_, d.c_str());
    lv_obj_set_style_text_color(detail_, c_(filter_.empty() ? kJfDim : kJfBlue), 0);
}

void GridView::load_art() {
    if (!page_ || !art_ || !items_) return;
    for (size_t k = 0; k < cells_.size(); ++k)
        if (bound_[k] >= 0) card_load(cells_[k], (*items_)[size_t(bound_[k])], shape_, *art_);
    art_->flush();
}

bool GridView::key(uint32_t key) {
    if (!page_ || !items_) return false;
    int n = int(shown_.size());
    int to = cursor_;
    switch (key) {
        case LV_KEY_LEFT: to = cursor_ - 1; break;
        case LV_KEY_RIGHT: to = cursor_ + 1; break;
        case LV_KEY_UP: to = cursor_ >= cols_ ? cursor_ - cols_ : cursor_; break;
        case LV_KEY_DOWN:
            // Onto the last row even where it is short.
            to = cursor_ + cols_ < n ? cursor_ + cols_ : (cursor_ / cols_ < (n - 1) / cols_ ? n - 1 : cursor_);
            break;
        case kKeyPageUp: to = cursor_ - full_rows_ * cols_; break;
        case kKeyPageDown: to = cursor_ + full_rows_ * cols_; break;
        case LV_KEY_HOME: to = 0; break;
        case LV_KEY_END: to = n - 1; break;
        case LV_KEY_ESC:
            // Esc takes what was typed off first, as in every ZeroOS list.
            if (filter_.empty()) return false;
            filter_.clear();
            apply_filter();
            return true;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            if (filter_.empty()) return false;
            // One character, however many bytes it is.
            while (!filter_.empty() && (filter_.back() & 0xC0) == 0x80) filter_.pop_back();
            if (!filter_.empty()) filter_.pop_back();
            apply_filter();
            return true;
        default:
            // Typing narrows the grid. With nothing typed yet, Space and the
            // digits keep their own jobs (pause; the key sheet).
            if (key < 0x20 || key == 0x7f || key >= 0x100) return false;
            if (filter_.empty() && (key == ' ' || (key >= '0' && key <= '9'))) return false;
            if (filter_.size() >= 40) return true;
            if (key < 0x80) {
                filter_ += char(key);
            } else {   // Latin-1, as UTF-8
                filter_ += char(0xC0 | (key >> 6));
                filter_ += char(0x80 | (key & 0x3F));
            }
            apply_filter();
            return true;
    }
    if (n == 0) return false;
    to = std::clamp(to, 0, n - 1);
    if (to != cursor_) {
        cursor_ = to;
        bind();
    }
    return true;
}

}  // namespace zs::jfapp
