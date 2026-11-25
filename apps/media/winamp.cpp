// Classic Winamp skins (README.md, "Winamp skins"): a .wsz, read by wsz.h,
// drawn the way Winamp 2 drew it. Its main window, 275 x 116, fits the app
// area at its own size, so it is the player screen, every sprite where
// Winamp put it; the playlist window, stretched to the whole area, is the
// playlist screen (the explorer), in the skin's pledit.txt colours.
//
// Where the sprites are in each bitmap, and where they go, are Winamp's own
// (as Webamp documents them: github.com/captbaritone/webamp, skinSprites.ts
// and its CSS). A bitmap the skin lacks is left out; Winamp would use its own
// skin's, which isn't ours to ship. Without main.bmp it isn't a skin.
//
// What a keyboard can't do is drawn but does nothing: the balance, the EQ,
// the window buttons. The analyser dances to a made-up beat, as the other
// skins' meters do (README.md, "Limits").
#include <algorithm>
#include <cmath>

#include "skin_text.h"
#include "skins.h"
#include "ui/theme.h"
#include "wsz.h"

namespace zs::media {
namespace {

constexpr int W = theme::kScreenW;
constexpr int H = theme::kBodyH;

uint16_t to565(uint32_t c) { return uint16_t((c >> 19 & 31) << 11 | (c >> 10 & 63) << 5 | (c >> 3 & 31)); }

// A bitmap, ready for the canvas.
struct Sheet {
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    bool ok() const { return w > 0; }
};

Sheet sheet(const wsz::Files& f, const char* name) {
    Sheet s;
    auto it = f.find(name);
    if (it == f.end()) return s;
    wsz::Image img = wsz::decode_bmp(it->second);
    if (!img.ok()) return s;
    s.w = img.w;
    s.h = img.h;
    s.px.resize(img.px.size());
    for (size_t i = 0; i < img.px.size(); ++i) s.px[i] = to565(img.px[i]);
    return s;
}

// The 5 x 6 letters of text.bmp: [row, column] for each character.
bool font_cell(char ch, int& row, int& col) {
    unsigned char u = (unsigned char)std::tolower((unsigned char)ch);
    if (u >= 'a' && u <= 'z') return row = 0, col = u - 'a', true;
    if (u >= '0' && u <= '9') return row = 1, col = u - '0', true;
    static const char* const r1 = "0123456789_.:()-'!_+\\/[]^&%,=$#";   // [1][10] is the ellipsis
    switch (u) {
        case '"': return row = 0, col = 26, true;
        case '@': return row = 0, col = 27, true;
        case '?': return row = 2, col = 3, true;
        case '*': return row = 2, col = 4, true;
        case '<': case '{': return row = 1, col = 22, true;
        case '>': case '}': return row = 1, col = 23, true;
    }
    for (int i = 11; r1[i]; ++i)
        if (r1[i] == char(u)) return row = 1, col = i, true;
    return row = 0, col = 30, true;   // a space, for anything else
}

// The made-up music (as skins.cpp's): a band's level, 0..1, at time t.
float beat(float t) { return std::exp(-std::fmod(t, 0.5f) * 9.f); }
float band(int i, int n, float t) {
    float f = float(i) / n;
    float v = 0.42f + 0.3f * std::sin(t * 3.3f + i * 0.9f) * std::sin(t * 1.9f + i * 1.7f) + 0.16f * std::sin(t * 7.1f + i * 2.3f);
    v = v * (1 - f * 0.4f) + beat(t) * (0.55f - f * 0.45f);
    return std::clamp(v, 0.05f, 1.f);
}

std::string mmss(int ms) {
    if (ms < 0) return "";
    int s = ms / 1000;
    char b[16];
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

class Winamp : public Skin {
public:
    Winamp(std::string name, const wsz::Files& f) : name_(std::move(name)) {
        main_ = sheet(f, "main.bmp");
        title_ = sheet(f, "titlebar.bmp");
        cbuttons_ = sheet(f, "cbuttons.bmp");
        playpaus_ = sheet(f, "playpaus.bmp");
        numbers_ = sheet(f, "nums_ex.bmp");
        if (!numbers_.ok()) numbers_ = sheet(f, "numbers.bmp");
        text_ = sheet(f, "text.bmp");
        monoster_ = sheet(f, "monoster.bmp");
        posbar_ = sheet(f, "posbar.bmp");
        volume_ = sheet(f, "volume.bmp");
        balance_ = sheet(f, "balance.bmp");
        shufrep_ = sheet(f, "shufrep.bmp");
        pledit_ = sheet(f, "pledit.bmp");
        auto txt = [&](const char* n) {
            auto it = f.find(n);
            return it == f.end() ? std::string() : it->second;
        };
        vis_ = wsz::viscolors(txt("viscolor.txt"));
        pl_ = wsz::pledit_colors(txt("pledit.txt"));
    }

    bool ok() const { return main_.ok(); }
    const char* name() const override { return name_.c_str(); }
    int rows_shown() const override { return kRows; }

    void draw(lv_obj_t* canvas, uint16_t* buf, const View& v) override {
        buf_ = buf;
        float dt = last_t_ < 0 ? 0 : std::clamp(v.t - last_t_, 0.f, 0.5f);
        last_t_ = v.t;
        if (v.list) {
            draw_playlist(canvas, v);
        } else {
            std::fill(buf, buf + size_t(W) * H, to565(0));
            draw_main(v, dt);
        }
    }

private:
    static constexpr int kRows = 6;        // the playlist's rows, 13 pixels each
    static constexpr int kMainX = (W - 275) / 2, kMainY = (H - 116) / 2;

    // A piece of a bitmap, at (dx, dy) on the canvas; clipped to both.
    void blit(const Sheet& s, int sx, int sy, int w, int h, int dx, int dy) {
        if (!s.ok()) return;
        for (int j = 0; j < h; ++j) {
            int y = dy + j, v = sy + j;
            if (y < 0 || y >= H || v < 0 || v >= s.h) continue;
            for (int i = 0; i < w; ++i) {
                int x = dx + i, u = sx + i;
                if (x < 0 || x >= W || u < 0 || u >= s.w) continue;
                buf_[y * W + x] = s.px[size_t(v) * size_t(s.w) + size_t(u)];
            }
        }
    }
    // The same, at a place in the main window.
    void put(const Sheet& s, int sx, int sy, int w, int h, int x, int y) { blit(s, sx, sy, w, h, kMainX + x, kMainY + y); }
    void rect(int x, int y, int w, int h, uint16_t c) {
        for (int j = std::max(0, y); j < std::min(H, y + h); ++j)
            for (int i = std::max(0, x); i < std::min(W, x + w); ++i) buf_[j * W + i] = c;
    }
    // text.bmp's letters, clipped to a box w pixels wide.
    void text(const std::string& s, int x, int y, int w, int skip = 0) {
        for (int i = 0; i * 5 - skip < w && i < int(s.size()); ++i) {
            int row, col, at = i * 5 - skip;
            font_cell(s[size_t(i)], row, col);
            int from = std::max(0, -at), to = std::min(5, w - at);
            if (to > from) blit(text_, col * 5 + from, row * 6, to - from, 6, x + at + from, y);
        }
    }

    // ---- The main window ----------------------------------------------------

    void draw_main(const View& v, float dt) {
        bool live = !v.stopped && !v.paused;
        put(main_, 0, 0, 275, 116, 0, 0);
        put(title_, 27, 0, 275, 14, 0, 0);
        put(title_, 0, 0, 9, 9, 6, 3);      // options
        put(title_, 9, 0, 9, 9, 244, 3);    // minimise
        put(title_, 0, 18, 9, 9, 254, 3);   // shade
        put(title_, 18, 0, 9, 9, 264, 3);   // close
        put(title_, 304, 0, 8, 43, 10, 22); // the clutter bar

        // Playing, paused or stopped, and the time (blinking while paused).
        put(playpaus_, v.stopped ? 18 : v.paused ? 9 : 0, 0, 9, 9, 26, 28);
        if (live) put(playpaus_, 39, 0, 3, 9, 24, 28);
        bool blink_off = v.paused && std::fmod(v.t, 1.f) >= 0.5f;
        if (!v.stopped && !blink_off) {
            int s = std::max(0, v.pos_ms) / 1000, m = s / 60 % 100;
            int d[4] = {m / 10, m % 10, s % 60 / 10, s % 10};
            const int xs[4] = {48, 60, 78, 90};
            for (int i = 0; i < 4; ++i) put(numbers_, d[i] * 9, 0, 9, 13, xs[i], 26);
        }

        draw_analyser(live, dt);

        // The title, scrolling a letter at a time when it doesn't fit.
        std::string m;
        if (v.stopped) {
            m = name_ + "  ***  ZeroOS Media";
        } else {
            m = (v.q_at >= 0 ? std::to_string(v.q_at + 1) + ". " : "") + (v.artist.empty() ? "" : v.artist + " - ") +
                v.title + (v.dur_ms > 0 ? " (" + mmss(v.dur_ms) + ")" : "");
        }
        if (int(m.size()) * 5 > 154) {
            m += "  ***  ";
            size_t off = size_t(v.t * 4) % m.size();
            m = m.substr(off) + m.substr(0, off);
        }
        text(m, kMainX + 111, kMainY + 27, 154);

        // Stereo lights while something plays.
        put(monoster_, 29, 12, 27, 12, 212, 41);
        put(monoster_, 0, v.stopped ? 12 : 0, 29, 12, 239, 41);

        // Volume: 28 pictures of the slider, one under the other, then the thumb.
        int vol = v.muted ? 0 : std::clamp(v.volume, 0, 100);
        put(volume_, 0, (vol * 27 + 50) / 100 * 15, 68, 13, 107, 57);
        if (volume_.h >= 433) put(volume_, 15, 422, 14, 11, 107 + (68 - 14) * vol / 100, 58);
        // Balance: always in the middle.
        const Sheet& bal = balance_.ok() ? balance_ : volume_;
        put(bal, 9, 0, 38, 13, 177, 57);
        if (bal.h >= 433) put(bal, 15, 422, 14, 11, 177 + 12, 58);

        // The EQ and playlist buttons; the playlist is the other screen.
        put(shufrep_, 0, 61, 23, 12, 219, 58);
        put(shufrep_, 23, 61, 23, 12, 242, 58);

        // The seek bar: the thumb shows where the track is.
        put(posbar_, 0, 0, 248, 10, 16, 72);
        if (!v.stopped && v.dur_ms > 0 && posbar_.w >= 277) {
            float f = std::clamp(float(v.pos_ms) / float(v.dur_ms), 0.f, 1.f);
            put(posbar_, 248, 0, 29, 10, 16 + int(std::lround(f * (248 - 29))), 72);
        }

        // The transport buttons, pressed for a moment when their key is.
        const int bx[5] = {16, 39, 62, 85, 108}, bw[5] = {23, 23, 23, 23, 22};
        for (int i = 0; i < 5; ++i) put(cbuttons_, i * 23, v.flash == i ? 18 : 0, bw[i], 18, bx[i], 88);
        put(cbuttons_, 114, 0, 22, 16, 136, 89);   // eject
        put(shufrep_, 28, v.shuffle ? 30 : 0, 47, 15, 164, 89);
        put(shufrep_, 0, 0, 28, 15, 210, 89);      // repeat
    }

    // 19 bars, 3 pixels wide, 16 high, in viscolor.txt's colours; peaks fall.
    void draw_analyser(bool live, float dt) {
        int x0 = kMainX + 24, y0 = kMainY + 43;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 76; ++x) rect(x0 + x, y0 + y, 1, 1, to565(vis_[(x & 1) && (y & 1) ? 1 : 0]));
        for (int i = 0; i < 19; ++i) {
            float target = live ? band(i, 19, last_t_) : 0;
            level_[i] = target > level_[i] ? target : std::max(0.f, level_[i] - dt * 2.5f);
            peak_[i] = std::max(level_[i], peak_[i] - dt * 0.8f);
            int n = int(std::lround(level_[i] * 16));
            for (int k = 0; k < n; ++k) rect(x0 + i * 4, y0 + 15 - k, 3, 1, to565(vis_[size_t(17 - k)]));
            int p = int(std::lround(peak_[i] * 16));
            if (p > 0) rect(x0 + i * 4, y0 + 16 - p, 3, 1, to565(vis_[23]));
        }
    }

    // ---- The playlist window ------------------------------------------------

    void draw_playlist(lv_obj_t* canvas, const View& v) {
        constexpr int kTop = 20, kBottom = 38, kMid = H - kTop - kBottom;   // 78: six rows
        rect(0, 0, W, H, to565(pl_.normal_bg));
        if (pledit_.ok()) {
            // The top: corners, the tiles between, the title in the middle.
            for (int x = 25; x < W - 25; x += 25) blit(pledit_, 127, 0, std::min(25, W - 25 - x), 20, x, 0);
            blit(pledit_, 0, 0, 25, 20, 0, 0);
            blit(pledit_, 26, 0, 100, 20, (W - 100) / 2, 0);
            blit(pledit_, 153, 0, 25, 20, W - 25, 0);
            // The sides.
            for (int y = kTop; y < kTop + kMid; y += 29) {
                int h = std::min(29, kTop + kMid - y);
                blit(pledit_, 0, 42, 12, h, 0, y);
                blit(pledit_, 31, 42, 20, h, W - 20, y);
            }
            // The bottom: its two corners, tiles between.
            for (int x = 125; x < W - 150; x += 25) blit(pledit_, 179, 0, std::min(25, W - 150 - x), kBottom, x, H - kBottom);
            blit(pledit_, 0, 72, 125, kBottom, 0, H - kBottom);
            blit(pledit_, 126, 72, 150, kBottom, W - 150, H - kBottom);
        }

        // The scroll handle, down the right side as far as the list is.
        int n = v.rows ? int(v.rows->size()) : 0;
        float f = n > kRows ? float(v.top) / float(n - kRows) : 0.f;
        blit(pledit_, 52, 53, 8, 18, W - 15, kTop + int(std::lround(f * (kMid - 18))));

        // Where the explorer is, in the running-time box; the time below it.
        std::string where = v.crumb;
        for (auto& ch : where) ch = char(std::toupper((unsigned char)ch));
        int bx = W - 150 + 7, by = H - kBottom + 10, bw = 16 * 5;
        if (int(where.size()) * 5 > bw) {
            where += "  ***  ";
            size_t off = size_t(v.t * 3) % where.size();
            where = where.substr(off) + where.substr(0, off);
        }
        text(where, bx, by, bw);
        if (!v.stopped) {
            std::string t = mmss(v.pos_ms);
            text(t, W - 150 + 66 + std::max(0, 5 - int(t.size())) * 5, H - kBottom + 23, 25);
        }

        // The rows, in the system font: Winamp's own font is too small for
        // file names, and lacks most letters besides.
        if (!v.rows) return;
        Text tx(canvas);
        const lv_font_t* font = &::zs_font_12;
        int x0 = 12, x1 = W - 20;
        for (int r = 0; r < kRows; ++r) {
            int i = v.top + r;
            if (i >= n) break;
            const Row& row = (*v.rows)[size_t(i)];
            int y = kTop + r * 13;
            if (i == v.sel) rect(x0, y, x1 - x0, 13, to565(pl_.selected_bg));
            bool now = !row.path.empty() && row.path == v.playing_path;
            uint32_t col = now ? pl_.current : pl_.normal;
            std::string t = row.icon ? std::string(row.icon) + " " + row.text : row.text;
            int rw = row.right.empty() ? 0 : Text::width(row.right, font) + 6;
            tx.draw(t, x0 + 2, y - 1, font, col, box(x0, y, x1 - x0 - rw - 2, 13));
            tx.draw(row.right, x1 - 3, y - 1, font, col, box(x0, y, x1 - x0, 13), true);
        }
    }

    std::string name_;
    Sheet main_, title_, cbuttons_, playpaus_, numbers_, text_, monoster_, posbar_, volume_, balance_, shufrep_,
        pledit_;
    std::vector<uint32_t> vis_;
    wsz::PlColors pl_;
    uint16_t* buf_ = nullptr;
    float last_t_ = -1;
    float level_[19] = {}, peak_[19] = {};
};

}  // namespace

std::unique_ptr<Skin> make_winamp_skin(const std::string& path, std::string* why) {
    wsz::Files files;
    if (!wsz::load_files(path, files, why)) return nullptr;
    std::string name = path.substr(path.rfind('/') + 1);
    auto dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    auto skin = std::make_unique<Winamp>(name, files);
    if (!skin->ok()) {
        if (why) *why = files.count("main.bmp") ? "its main.bmp can't be read" : "it has no main.bmp (a Winamp 3 or 5 skin?)";
        return nullptr;
    }
    return skin;
}

}  // namespace zs::media
