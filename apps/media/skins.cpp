// The skins (skins.h), drawn as pixel art: hard 1-pixel bevels, a 3x5
// lettering font, seven-segment digits with the unlit segments showing,
// dithered light. Names and lists are in the system font, drawn into the
// same canvas through LVGL.
//
//   Hi-Fi Receiver  walnut and brushed aluminium; the tuning dial is the
//                   seek bar, two VU meters, latching piano keys
//   Liquid Chrome   a molten-metal body whose reflections ripple, an oval
//                   screen, jelly buttons, a tube that fills as a track plays
//
// The meters dance to a made-up beat: the player doesn't hand the sound to
// the app (README.md, "Limits").
#include "skins.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>

#include "skin_text.h"
#include "ui/theme.h"

namespace zs::media {
namespace {

constexpr int W = theme::kScreenW;
constexpr int H = theme::kBodyH;

// ---- Colour ---------------------------------------------------------------

struct C3 {
    float r, g, b;
};
constexpr C3 hx(uint32_t h) { return {float(h >> 16 & 255), float(h >> 8 & 255), float(h & 255)}; }
C3 mix(C3 a, C3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
uint16_t to565(C3 c) {
    auto q = [](float v) { return unsigned(std::clamp(v, 0.f, 255.f)); };
    return uint16_t((q(c.r) >> 3) << 11 | (q(c.g) >> 2) << 5 | q(c.b) >> 3);
}
constexpr uint16_t R(uint32_t h) { return uint16_t((h >> 19 & 31) << 11 | (h >> 10 & 63) << 5 | (h >> 3 & 31)); }

float hash(float x, float y) {
    float s = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return s - std::floor(s);
}
float bayer(int x, int y) {
    static const uint8_t m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.f;
}
float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

// ---- Pixels ---------------------------------------------------------------

struct P {
    uint16_t* b;
    void px(int x, int y, uint16_t c) const {
        if (unsigned(x) < unsigned(W) && unsigned(y) < unsigned(H)) b[y * W + x] = c;
    }
    void pxf(float x, float y, uint16_t c) const { px(int(std::lround(x)), int(std::lround(y)), c); }
    void rect(int x, int y, int w, int h, uint16_t c) const {
        int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
        for (int j = y0; j < y1; ++j)
            for (int i = x0; i < x1; ++i) b[j * W + i] = c;
    }
    template <class F>
    void shade(int x, int y, int w, int h, F f) const {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i)
                if (unsigned(i) < unsigned(W) && unsigned(j) < unsigned(H)) b[j * W + i] = to565(f(i, j));
    }
};

void bevel(const P& p, int x, int y, int w, int h, uint16_t fill, uint16_t light, uint16_t dark) {
    p.rect(x, y, w, h, fill);
    p.rect(x, y, w, 1, light);
    p.rect(x, y, 1, h, light);
    p.rect(x, y + h - 1, w, 1, dark);
    p.rect(x + w - 1, y, 1, h, dark);
}

void pline(const P& p, float fx0, float fy0, float fx1, float fy1, uint16_t c) {
    int x0 = int(std::lround(fx0)), y0 = int(std::lround(fy0)), x1 = int(std::lround(fx1)), y1 = int(std::lround(fy1));
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int n = 0; n < 2000; ++n) {
        p.px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) e += dy, x0 += sx;
        if (e2 <= dx) e += dx, y0 += sy;
    }
}

void screw(const P& p, int x, int y) {
    p.px(x, y, R(0xf4f4f4));
    p.px(x + 1, y, R(0x8a8a8a));
    p.px(x, y + 1, R(0x8a8a8a));
    p.px(x + 1, y + 1, R(0x3a3a3a));
}

// ---- The 3x5 lettering font -----------------------------------------------

uint16_t glyph3(char ch) {
    static const struct {
        char c;
        const char* bits;
    } k[] = {
        {'A', "010101111101101"}, {'B', "110101110101110"}, {'C', "011100100100011"}, {'D', "110101101101110"},
        {'E', "111100110100111"}, {'F', "111100110100100"}, {'G', "011100101101011"}, {'H', "101101111101101"},
        {'I', "111010010010111"}, {'J', "001001001101010"}, {'K', "101101110101101"}, {'L', "100100100100111"},
        {'M', "101111111101101"}, {'N', "110101101101101"}, {'O', "010101101101010"}, {'P', "110101110100100"},
        {'Q', "010101101110011"}, {'R', "110101110101101"}, {'S', "011100010001110"}, {'T', "111010010010010"},
        {'U', "101101101101111"}, {'V', "101101101101010"}, {'W', "101101111111101"}, {'X', "101101010101101"},
        {'Y', "101101010010010"}, {'Z', "111001010100111"}, {'0', "111101101101111"}, {'1', "010110010010111"},
        {'2', "110001010100111"}, {'3', "110001010001110"}, {'4', "101101111001001"}, {'5', "111100110001110"},
        {'6', "011100111101111"}, {'7', "111001010010010"}, {'8', "111101111101111"}, {'9', "111101111001110"},
        {'.', "000000000000010"}, {':', "000010000010000"}, {'-', "000000111000000"}, {'/', "001001010100100"},
        {'(', "010100100100010"}, {')', "010001001001010"}, {'+', "000010111010000"}, {'>', "100010001010100"},
        {'<', "001010100010001"}, {'#', "101111101111101"}, {'%', "101001010100101"}, {'!', "010010010000010"},
        {'*', "000101010101000"}, {'=', "000111000111000"}, {'\'', "010010000000000"}, {'&', "010101010101011"},
        {'_', "000000000000111"}, {',', "000000000010100"}, {'?', "110001010000010"},
    };
    char u = char(std::toupper((unsigned char)ch));
    for (auto& g : k)
        if (g.c == u) {
            uint16_t m = 0;
            for (int i = 0; i < 15; ++i)
                if (g.bits[i] == '1') m |= uint16_t(1 << i);
            return m;
        }
    return 0;
}
int w3(const std::string& s, int k = 1) { return s.empty() ? 0 : int(s.size()) * 4 * k - k; }
void t3(const P& p, const std::string& s, int x, int y, uint16_t col, int k = 1) {
    int cx = x;
    for (char ch : s) {
        uint16_t m = glyph3(ch);
        for (int i = 0; i < 15; ++i)
            if (m & (1 << i)) p.rect(cx + (i % 3) * k, y + (i / 3) * k, k, k, col);
        cx += 4 * k;
    }
}
// Engraved: dark letters with a light line under them.
void t3e(const P& p, const std::string& s, int x, int y, uint16_t col, uint16_t hi, int k = 1) {
    t3(p, s, x, y + 1, hi, k);
    t3(p, s, x, y, col, k);
}

// ---- Seven-segment digits, 7x13 (times k) ---------------------------------

int seg_w(const std::string& s, int k = 1) {
    int w = 0;
    for (char ch : s) w += (ch == ':' ? 3 : 9) * k;
    return w - 2 * k;
}
void seg7(const P& p, const std::string& s, int x, int y, uint16_t on, uint16_t off, int glow, int k = 1) {
    static const int r[7][4] = {{1, 0, 5, 1}, {6, 1, 1, 5}, {6, 7, 1, 5}, {1, 12, 5, 1}, {0, 7, 1, 5}, {0, 1, 1, 5}, {1, 6, 5, 1}};
    static const char* lit[10] = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};
    int cx = x;
    for (char ch : s) {
        if (ch == ':') {
            p.rect(cx, y + 3 * k, k, 2 * k, on);
            p.rect(cx, y + 9 * k, k, 2 * k, on);
            cx += 3 * k;
            continue;
        }
        const char* l = ch >= '0' && ch <= '9' ? lit[ch - '0'] : ch == '-' ? "g" : "";
        for (int pass = 0; pass < 2; ++pass)
            for (int s2 = 0; s2 < 7; ++s2) {
                bool is = std::strchr(l, 'a' + s2) != nullptr;
                auto& q = r[s2];
                if (pass == 0) {
                    if (is && glow >= 0) {
                        int gx = q[2] == 1 ? 1 : 0, gy = q[3] == 1 ? 1 : 0;
                        p.rect(cx + q[0] * k - gx, y + q[1] * k - gy, q[2] * k + 2 * gx, q[3] * k + 2 * gy, uint16_t(glow));
                    }
                } else {
                    p.rect(cx + q[0] * k, y + q[1] * k, q[2] * k, q[3] * k, is ? on : off);
                }
            }
        cx += 9 * k;
    }
}

std::string mmss(int ms) {
    if (ms < 0) return "-:--";
    int s = ms / 1000;
    char b[16];
    if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

// ---- The made-up music ----------------------------------------------------

float beat(float t) { return std::exp(-std::fmod(t, 0.5f) * 9.f); }
float band(int i, int n, float t) {
    float f = float(i) / n;
    float v = 0.42f + 0.3f * std::sin(t * 3.3f + i * 0.9f) * std::sin(t * 1.9f + i * 1.7f) + 0.16f * std::sin(t * 7.1f + i * 2.3f);
    v = v * (1 - f * 0.4f) + beat(t) * (0.55f - f * 0.45f);
    return std::clamp(v, 0.05f, 1.f);
}
bool live(const View& v) { return !v.stopped && !v.paused; }
float progress(const View& v) { return v.dur_ms > 0 ? clamp01(float(v.pos_ms) / v.dur_ms) : 0.f; }

// A line that scrolls round when it doesn't fit.
void marquee(Text& tx, const std::string& s, int x, int y, int w, const lv_font_t* f, uint32_t rgb, float t, float speed = 14) {
    int tw = Text::width(s, f);
    if (tw <= w) {
        tx.draw(s, x, y, f, rgb, box(x, y - 2, w, 24));
        return;
    }
    int gap = 30, period = tw + gap, off = int(std::fmod(t * speed, float(period)));
    tx.draw(s, x - off, y, f, rgb, box(x, y - 2, w, 24));
    tx.draw(s, x - off + period, y, f, rgb, box(x, y - 2, w, 24));
}

std::string row_text(const Row& r) { return r.icon ? std::string(r.icon) + " " + r.text : r.text; }

// ===========================================================================
// Hi-Fi Receiver
// ===========================================================================

constexpr uint16_t AMB = R(0xffb347), AMB_HI = R(0xffd9a0), AMB_DIM = R(0x5a3410), AMB_GLOW = R(0x6a3a0c),
                   AMB_OFF = R(0x2a1404), ENGR = R(0x2a2a2e), ENGR_HI = R(0xeeeef2), RED = R(0xff3018),
                   RED_OFF = R(0x3a0c04);

C3 wood(int x, int y) {
    float g = std::sin(x * 0.55f + std::sin(y * 0.07f + x * 0.3f) * 2.4f + hash(float(x), float(y)) * 0.5f);
    C3 base = mix(hx(0x3e1c0a), hx(0x7a4020), (g + 1) / 2 * 0.8f + hash(float(x >> 1), float(y)) * 0.2f);
    return g > 0.86f ? mix(base, {30, 12, 4}, 0.5f) : base;
}
C3 metal(int x, int y) {
    float r = 170 + (hash(0, float(y)) - 0.5f) * 26 + (hash(float(x), float(y)) - 0.5f) * 12 + std::sin(x * 0.015f) * 8;
    return {r, r, r + 4};
}

void chassis(const P& p, const std::string& title) {
    p.shade(0, 0, W, H, metal);
    p.shade(0, 0, 8, H, wood);
    p.shade(312, 0, 8, H, wood);
    p.rect(8, 0, 1, H, R(0x1a0c04));
    p.rect(311, 0, 1, H, R(0x1a0c04));
    p.rect(9, 0, 1, H, R(0xf0f0f4));
    p.rect(310, 0, 1, H, R(0x6a6a70));
    // The top trim, a brass nameplate in it.
    bevel(p, 9, 0, 302, 10, R(0xb4b4ba), R(0xf2f2f6), R(0x5a5a62));
    int tw = w3(title) + 12, tx = 160 - tw / 2;
    p.shade(tx, 1, tw, 8, [](int x, int y) {
        return mix(hx(0x8a6420), hx(0xf0d080), (std::sin(x * 0.4f + y) * 0.5f + 0.5f) * 0.45f + (y < 4 ? 0.4f : 0));
    });
    p.rect(tx - 1, 1, 1, 8, R(0x4a3008));
    p.rect(tx + tw, 1, 1, 8, R(0xf8e8b0));
    t3e(p, title, tx + 6, 2, R(0x2a1a04), R(0xf8e8b0));
    for (int k = 0; k < 2; ++k) {
        p.rect(14, 3 + k * 3, tx - 20, 1, R(0x7a7a82));
        p.rect(tx + tw + 6, 3 + k * 3, 280 - (tx + tw + 6), 1, R(0x7a7a82));
    }
    for (int x : {290, 299}) bevel(p, x, 2, 8, 7, R(0xc8a050), R(0xf8e0a0), R(0x5a3a10));
    t3(p, "-", 292, 3, R(0x3a2408));
    t3(p, "X", 301, 3, R(0x3a2408));
}

// Black glass with a warm backlight and a sheen across it.
void dial_glass(const P& p, int x, int y, int w, int h) {
    p.rect(x - 1, y - 1, w + 2, 1, R(0x4a4a50));
    p.rect(x - 1, y - 1, 1, h + 2, R(0x4a4a50));
    p.rect(x - 1, y + h, w + 2, 1, R(0xf4f4f8));
    p.rect(x + w, y - 1, 1, h + 2, R(0xf4f4f8));
    p.shade(x, y, w, h, [=](int X, int Y) {
        float u = float(X - x) / w, v = float(Y - y) / h;
        float g = std::max(0.f, 1 - std::pow((v - 0.55f) * 2.2f, 2.f)) * (0.55f + 0.45f * std::sin(u * 3.14159f));
        C3 col = mix(hx(0x070402), hx(0x3a1e05), clamp01(g * 0.9f + (bayer(X, Y) - 0.5f) * 0.18f));
        if (((X - x) - (Y - y) * 2 + 400) % 150 < 9) col = mix(col, {255, 255, 255}, 0.07f);
        return col;
    });
}

void vu_face(const P& p, int x, int y) {
    bevel(p, x, y, 52, 38, R(0x1a1a1c), R(0x4a4a50), R(0x000000));
    p.shade(x + 2, y + 2, 48, 32, [=](int X, int Y) {
        return mix(hx(0xfff3c8), hx(0xe0b860), clamp01(float(Y - y) / 34 + (bayer(X, Y) - 0.5f) * 0.25f));
    });
    float cx = x + 26, cy = y + 40;
    for (int i = 0; i <= 20; ++i) {
        float a = -2.3f + i * 0.0805f;
        bool red = i >= 14, major = i % 4 == 0;
        for (int r = 27; r <= (major ? 31 : 29); ++r) p.pxf(cx + std::cos(a) * r, cy + std::sin(a) * r, red ? R(0xc01010) : R(0x2a2010));
    }
    for (float a = -2.3f + 14 * 0.0805f; a <= -2.3f + 20 * 0.0805f; a += 0.02f)
        p.pxf(cx + std::cos(a) * 26, cy + std::sin(a) * 26, R(0xc01010));
    t3(p, "-20", x + 4, y + 14, R(0x2a2010));
    t3(p, "0", x + 34, y + 5, R(0x2a2010));
    t3(p, "+3", x + 42, y + 14, R(0xc01010));
    t3(p, "VU", x + 22, y + 21, R(0x2a2010));
    t3(p, "PEAK", x + 9, y + 25, R(0x7a6030));
    p.rect(x + 2, y + 31, 48, 3, R(0x2a2a2e));
    p.rect(x + 2, y + 31, 48, 1, R(0x5a5a60));
    screw(p, x + 2, y + 32);
    screw(p, x + 48, y + 32);
}
void vu_needle(const P& p, int x, int y, float level) {
    float cx = x + 26, cy = y + 40, a = -2.3f + std::min(1.08f, level) * 1.61f;
    pline(p, cx + std::cos(a) * 10, cy + std::sin(a) * 10, cx + std::cos(a) * 30, cy + std::sin(a) * 30, R(0x101010));
    bool peak = level > 0.95f;
    p.rect(x + 4, y + 25, 3, 3, peak ? R(0xff2010) : R(0x5a1008));
    if (peak) p.px(x + 4, y + 25, R(0xffc0a0));
}

void knob(const P& p, int cx, int cy, int r, float value) {
    for (int y = -r - 1; y <= r + 1; ++y)
        for (int x = -r - 1; x <= r + 1; ++x) {
            float d = std::hypot(float(x), float(y));
            if (d > r + 1) continue;
            if (d > r) {
                p.px(cx + x, cy + y, R(0x1a1a1c));
                continue;
            }
            if (d > r - 2) {
                int k = int(std::lround(std::atan2(float(y), float(x)) * r * 0.9f)) & 1;
                p.px(cx + x, cy + y, k ? R(0x2a2a2e) : y < 0 ? R(0xb0b0b6) : R(0x7a7a80));
                continue;
            }
            float ang = std::atan2(float(y), float(x));
            float v = 0.64f + 0.08f * std::sin(ang * 40 + hash(std::round(d), 3) * 3) - float(x + y) / (r * 4) +
                      (bayer(cx + x, cy + y) - 0.5f) * 0.12f;
            if (d < r * 0.22f) v += 0.12f;
            float g = v * 255;
            p.px(cx + x, cy + y, to565({g, g, g + 6}));
        }
    float a = -2.36f + value * 4.72f;
    pline(p, cx + std::cos(a) * (r * 0.35f), cy + std::sin(a) * (r * 0.35f), cx + std::cos(a) * (r - 3), cy + std::sin(a) * (r - 3),
          R(0x101012));
}
void knob_ticks(const P& p, int cx, int cy, int r, int n) {
    for (int i = 0; i <= n; ++i) {
        float a = -2.36f + i * 4.72f / n;
        p.pxf(cx + std::cos(a) * (r + 3), cy + std::sin(a) * (r + 3), ENGR);
    }
}
void piano_key(const P& p, int x, int y, int w, int h, bool down, int led) {
    p.rect(x - 1, y - 1, w + 2, h + 2, R(0x2a2a2e));
    int dy = down ? 1 : 0;
    p.shade(x, y + dy, w, h - dy, [=](int X, int Y) {
        float g = ((down ? 0.55f : 0.92f) - float(Y - y) / h * 0.35f + (bayer(X, Y) - 0.5f) * 0.08f) * 255;
        return C3{g, g, g + 4};
    });
    p.rect(x, y + dy, w, 1, down ? R(0x9a9aa0) : R(0xffffff));
    p.rect(x, y + h - 1, w, 1, R(0x5a5a60));
    if (down) p.rect(x, y, w, 1, R(0x1a1a1c));
    if (led >= 0) {
        p.rect(x + w / 2 - 2, y - 4, 4, 2, led ? RED : R(0x4a1008));
        if (led) p.px(x + w / 2 - 2, y - 4, R(0xffc0a0));
    }
}
void toggle(const P& p, int x, int y, bool up, const std::string& label) {
    p.rect(x - 3, y + 6, 11, 5, R(0x2a2a2e));
    p.rect(x - 3, y + 6, 11, 1, R(0x6a6a70));
    screw(p, x - 2, y + 8);
    screw(p, x + 5, y + 8);
    for (int k = 0; k < 7; ++k) {
        int yy = up ? y + 6 - k : y + 10 + k;
        float g = 210.f - k * 8;
        p.rect(x, yy, 4, 1, k == 6 ? R(0xffffff) : to565({g, g, g + 6}));
        p.px(x, yy, R(0xffffff));
        p.px(x + 3, yy, R(0x6a6a70));
    }
    t3e(p, label, x + 2 - w3(label) / 2, y + 18, ENGR, ENGR_HI);
}
void lamp(const P& p, int x, int y, const std::string& l, bool on) {
    p.rect(x, y, 3, 3, on ? RED : RED_OFF);
    if (on) p.px(x, y, R(0xffc0a0));
    t3(p, l, x + 5, y - 1, on ? AMB_HI : AMB_DIM);
}

class HiFi : public Skin {
public:
    const char* name() const override { return "Hi-Fi Receiver"; }
    int rows_shown() const override { return 6; }

    void draw(lv_obj_t* canvas, uint16_t* buf, const View& v) override {
        auto& bg = v.list ? list_bg_ : player_bg_;
        if (bg.empty()) {
            bg.resize(size_t(W) * H);
            P s{bg.data()};
            v.list ? list_static(s) : player_static(s);
        }
        std::memcpy(buf, bg.data(), bg.size() * 2);
        P p{buf};
        Text tx(canvas);
        v.list ? list(p, tx, v) : player(p, tx, v);
    }

private:
    static constexpr int kDialX = 22, kDialW = 258, kTrackW = 160;

    void player_static(const P& p) {
        chassis(p, "ZR-320 STEREO RECEIVER");
        dial_glass(p, 13, 13, 294, 31);
        t3(p, "TIME", 287, 23, AMB_DIM);
        vu_face(p, 12, 48);
        vu_face(p, 67, 48);
        t3e(p, "LEFT", 16, 89, ENGR, ENGR_HI);
        t3e(p, "RIGHT", 71, 89, ENGR, ENGR_HI);
        dial_glass(p, 123, 49, 141, 37);
        knob_ticks(p, 285, 64, 16, 10);
        t3(p, "0", 269, 79, ENGR);
        t3(p, "10", 297, 79, ENGR);
        t3e(p, "VOLUME", 274, 87, ENGR, ENGR_HI);
        static const char* keys[5] = {"PREV", "PLAY", "PAUSE", "STOP", "NEXT"};
        for (int i = 0; i < 5; ++i) {
            int x = 14 + i * 22;
            t3e(p, keys[i], x + 10 - w3(keys[i]) / 2, 118, ENGR, ENGR_HI);
        }
        p.rect(12, 96, 296, 1, R(0x7a7a80));
        p.rect(12, 97, 296, 1, R(0xf4f4f8));
        t3e(p, "HIGH FIDELITY", 207, 104, R(0x4a4a50), ENGR_HI);
        t3e(p, "STEREO RECEIVER", 203, 112, R(0x4a4a50), ENGR_HI);
        p.rect(200, 100, 64, 1, R(0x7a7a80));
        p.rect(200, 120, 64, 1, R(0xf4f4f8));
        t3e(p, "POWER", 276, 112, ENGR, ENGR_HI);
        screw(p, 12, H - 8);
        screw(p, 306, H - 8);
    }

    void player(const P& p, Text& tx, const View& v) {
        float t = v.t, b = live(v) ? beat(t) : 0, pr = progress(v);
        // The time scale: a mark a minute (or every few), and the end.
        int dur_s = v.dur_ms > 0 ? v.dur_ms / 1000 : 300;
        for (int i = 0; i <= 58; ++i) {
            int x = kDialX + int(std::lround(i * kDialW / 58.f));
            p.rect(x, 22, 1, i % 2 ? 2 : 4, i % 12 == 0 ? AMB_HI : AMB);
        }
        int step = std::max(1, (dur_s / 60 + 4) / 5) * 60;
        for (int s = 0; s < dur_s; s += step) {
            int x = kDialX + int(float(s) / dur_s * kDialW);
            if (s > 0 && kDialX + kDialW - x < 24) break;
            std::string m = mmss(s * 1000);
            t3(p, m, x - w3(m) / 2, 15, AMB_HI);
        }
        std::string end = v.dur_ms > 0 ? mmss(v.dur_ms) : "5:00";
        t3(p, end, std::min(kDialX + kDialW - w3(end) / 2, 304 - w3(end)), 15, AMB_HI);
        // The queue scale under it.
        int n = std::max(1, v.q_n);
        p.rect(kDialX, 33, kTrackW, 1, AMB_GLOW);
        int marks = std::min(n, 6);
        for (int i = 0; i < marks; ++i) {
            int num = n <= 6 ? i + 1 : 1 + int(std::lround(float(i) * (n - 1) / 5));
            int x = kDialX + (n == 1 ? 0 : int(float(num - 1) / (n - 1) * kTrackW));
            std::string sn = std::to_string(num);
            t3(p, sn, x - w3(sn) / 2, 37, AMB);
            p.rect(x, 34, 1, 2, AMB);
        }
        if (v.q_at >= 0) {
            int x = kDialX + (n == 1 ? 0 : int(float(v.q_at) / (n - 1) * kTrackW));
            p.rect(x - 1, 31, 4, 3, RED);
        }
        // The needle.
        int nx = kDialX + int(std::lround(pr * kDialW));
        p.rect(nx - 1, 14, 3, 29, R(0x6a1406));
        p.rect(nx, 14, 1, 29, RED);
        p.px(nx, 14, R(0xffd0b0));
        lamp(p, 196, 37, "STEREO", live(v));
        lamp(p, 234, 37, "TUNED", live(v) && b > 0.15f);
        lamp(p, 268, 37, "MUTE", v.muted);
        // VU meters.
        float L = live(v) ? band(0, 8, t) * 0.8f + b * 0.3f : 0, Rr = live(v) ? band(2, 8, t) * 0.8f + b * 0.28f : 0;
        vu_needle(p, 12, 48, L);
        vu_needle(p, 67, 48, Rr);
        // The readout: the time on the right, the track name big, the artist under it.
        std::string tm = v.stopped ? "0:00" : mmss(v.pos_ms);
        bool blink = v.paused && int(t * 2) % 2;
        seg7(p, blink ? std::string(tm.size(), ' ') : tm, 262 - seg_w(tm), 52, AMB, AMB_OFF, v.stopped ? -1 : AMB_GLOW);
        std::string info = v.stopped ? "STOPPED" : v.format;
        t3(p, info, 128, 80, AMB_DIM);
        if (v.q_at >= 0) {
            char q[16];
            std::snprintf(q, sizeof q, "%02d/%02d", v.q_at + 1, v.q_n);
            t3(p, q, 262 - w3(q), 80, AMB);
        }
        std::string title = v.stopped ? "Media" : v.title;
        std::string sub = v.stopped ? "Nothing playing"
                                    : v.artist + (v.album.empty() || v.album == v.artist ? "" : " \xC2\xB7 " + v.album);
        int tw = 262 - seg_w(tm) - 6 - 128;
        tx.draw(title, 127, 50, &::zs_font_16, 0x6a3a0c, box(124, 50, tw + 3, 20));
        tx.draw(title, 129, 50, &::zs_font_16, 0x6a3a0c, box(124, 50, tw + 3, 20));
        marquee(tx, title, 128, 49, tw, &::zs_font_16, 0xffd9a0, t, 20);
        marquee(tx, sub, 128, 66, 132, &::zs_font_12, 0xffb347, t);
        // Volume.
        knob(p, 285, 64, 14, v.muted ? 0.f : v.volume / 100.f);
        // The piano keys latch: PLAY while playing, PAUSE while paused, STOP when stopped.
        for (int i = 0; i < 5; ++i) {
            bool latched = (i == 1 && live(v)) || (i == 2 && v.paused) || (i == 3 && v.stopped);
            piano_key(p, 14 + i * 22, 104, 20, 11, latched || v.flash == i, latched ? 1 : 0);
        }
        toggle(p, 144, 101, v.shuffle, "SHUF");
        toggle(p, 172, 101, v.muted, "MUTE");
        p.rect(284, 104, 4, 4, RED);
        p.px(284, 104, R(0xffc0a0));
    }

    void list_static(const P& p) {
        chassis(p, "ZR-320 MEMORY BANK");
        dial_glass(p, 36, 13, 271, 80);
        t3e(p, "PRESET", 11, 13, ENGR, ENGR_HI);
        static const char* keys[6] = {"BACK", "OPEN", "FIND", "SHUF", "SKIN", "DECK"};
        for (int i = 0; i < 6; ++i) {
            int x = 14 + i * 25;
            t3e(p, keys[i], x + 11 - w3(keys[i]) / 2, 118, ENGR, ENGR_HI);
        }
        p.rect(12, 97, 296, 1, R(0x7a7a80));
        p.rect(12, 98, 296, 1, R(0xf4f4f8));
        dial_glass(p, 168, 103, 92, 22);
        knob_ticks(p, 288, 111, 10, 10);
        t3e(p, "SCROLL", 277, 124, ENGR, ENGR_HI);
    }

    void list(const P& p, Text& tx, const View& v) {
        const auto& rows = *v.rows;
        int n = int(rows.size());
        for (int i = 0; i < 6; ++i) {
            int r = v.top + i, y = 15 + i * 13;
            if (r >= n) break;
            bool sel = r == v.sel, play = !v.playing_path.empty() && rows[size_t(r)].path == v.playing_path;
            piano_key(p, 12, y + 6, 20, 8, sel, -1);
            t3(p, std::to_string(i + 1), 21, y + 7 + (sel ? 1 : 0), sel ? R(0x1a1a1c) : R(0x4a4a50));
            if (sel) {
                p.rect(38, y, 266, 12, R(0xd0841e));
                p.rect(38, y, 266, 1, AMB_HI);
                p.rect(38, y + 11, 266, 1, R(0x6a3a08));
            }
            if (play) {
                p.rect(41, y + 3, 5, 5, RED);
                p.px(41, y + 3, R(0xffc0a0));
            }
            const Row& row = rows[size_t(r)];
            uint32_t col = sel ? 0x1a0c02 : row.note ? 0x8a5a20 : row.folder ? 0xffd9a0 : 0xffb347;
            int rw = row.right.empty() ? 0 : Text::width(row.right, &::zs_font_12) + 6;
            tx.draw(row_text(row), 50, y - 1, &::zs_font_12, col, box(48, y, 252 - rw, 12));
            tx.draw(row.right, 300, y - 1, &::zs_font_12, col, box(200, y, 104, 12), true);
        }
        // The scroll scale on the glass's edge.
        for (int k = 0; k < 9; ++k) p.rect(305, 16 + k * 9, 1, 1, AMB_DIM);
        float sp = n > 6 ? float(v.top) / (n - 6) : 0;
        p.rect(304, 16 + int(sp * 60), 3, 16, AMB);
        for (int i = 0; i < 6; ++i) piano_key(p, 14 + i * 25, 104, 22, 11, false, i == 3 ? (v.shuffle ? 1 : 0) : -1);
        int tracks = 0;
        for (auto& r : rows) tracks += !r.folder && !r.note && !r.icon;
        std::string cnt = std::to_string(tracks);
        seg7(p, cnt, 196 - seg_w(cnt), 107, AMB, AMB_OFF, AMB_GLOW);
        t3(p, tracks == 1 ? "TRACK" : "TRACKS", 200, 107, AMB_HI);
        marquee(tx, v.crumb, 200, 111, 58, &::zs_font_12, 0x8a5a20, v.t, 10);
        knob(p, 288, 111, 8, n > 6 ? float(v.top) / (n - 6) : 0.f);
    }

    std::vector<uint16_t> player_bg_, list_bg_;
};

// ===========================================================================
// Liquid Chrome
// ===========================================================================

struct Ball {
    float x, y, r, sy;
};
// The body: metaballs, with their normals kept so the reflections can move.
struct Blob {
    std::vector<uint8_t> inside;
    std::vector<float> nx, ny;
};
Blob make_blob(const std::vector<Ball>& balls) {
    std::vector<float> f(size_t(W) * H), hgt(size_t(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float s = 0;
            for (auto& b : balls) {
                float dx = x - b.x, dy = (y - b.y) / b.sy;
                s += b.r * b.r / (dx * dx + dy * dy + 1);
            }
            f[size_t(y * W + x)] = s;
        }
    for (size_t i = 0; i < f.size(); ++i) hgt[i] = f[i] > 1 ? std::sqrt(std::min(1.f, (f[i] - 1) * 1.2f)) : 0;
    Blob B;
    B.inside.assign(f.size(), 0);
    B.nx.assign(f.size(), 0);
    B.ny.assign(f.size(), 0);
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x) {
            size_t i = size_t(y * W + x);
            if (f[i] <= 1) continue;
            B.inside[i] = 1;
            float gx = (hgt[i + 1] - hgt[i - 1]) * 9, gy = (hgt[i + W] - hgt[i - W]) * 9, l = std::sqrt(gx * gx + gy * gy + 1);
            B.nx[i] = gx / l;
            B.ny[i] = gy / l;
        }
    return B;
}
const C3 kEnv[] = {hx(0xffffff), hx(0xd4eaff), hx(0x8cbcf2), hx(0x3a72c8), hx(0x0e2a6a), hx(0x05070e),
                   hx(0x2a2430), hx(0x6a5a66), hx(0xa89aa6), hx(0xe0d8e2), hx(0xffffff)};
constexpr int kEnvN = 11;

void chrome_body(const P& p, const Blob& B, float t) {
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x) {
            size_t i = size_t(y * W + x);
            if (!B.inside[i]) continue;
            bool edge = !B.inside[i - 1] || !B.inside[i + 1] || !B.inside[i - W] || !B.inside[i + W];
            C3 col;
            if (edge) {
                col = {6, 10, 26};
            } else {
                // The reflection of a sky, a horizon and the ground; the horizon drifts.
                float v = B.ny[i] * 1.5f + B.nx[i] * 0.3f + (float(y) / H - 0.5f) * 1.1f +
                          0.14f * std::sin(x * 0.035f - y * 0.02f + t * 0.9f) + (bayer(x, y) - 0.5f) * 0.1f;
                int k = std::clamp(int(std::floor((v + 1.1f) / 2.2f * kEnvN)), 0, kEnvN - 1);
                col = kEnv[k];
                if (B.ny[i] < -0.6f && B.nx[i] < 0.1f) col = {255, 255, 255};
                if (B.ny[i] > 0.7f) col = mix(col, {60, 200, 255}, 0.35f);
            }
            p.b[i] = to565(col);
        }
}
void chrome_back(const P& p) {
    p.shade(0, 0, W, H, [](int x, int y) {
        C3 col = mix(hx(0x00061a), hx(0x0b2c74), clamp01(float(y) / H * 0.8f + (bayer(x, y) - 0.5f) * 0.22f));
        if (x % 8 == 0 || y % 8 == 0) col = mix(col, {60, 140, 255}, 0.09f);
        return col;
    });
}
void bubbles(const P& p, float t) {
    for (int i = 0; i < 12; ++i) {
        float x = float((i * 61 + 7) % 272), y = H - std::fmod(t * (6 + (i % 4) * 3) + i * 29, float(H + 10)), r = 1.f + i % 3;
        for (float a = 0; a < 6.28f; a += 0.5f) p.pxf(x + std::cos(a) * r, y + std::sin(a) * r, R(0x3a7ad8));
    }
}
void pix_glyph(const P& p, int k, int cx, int cy, int s, uint16_t col) {
    int h = s / 2;
    for (int y = -h; y <= h; ++y) {
        int w = h - std::abs(y);
        if (k == 1) p.rect(cx - h + 1, cy + y, w + 1, 1, col);
        if (k == 0) {
            p.rect(cx - h, cy + y, 1, 1, col);
            p.rect(cx + h - w, cy + y, w + 1, 1, col);
        }
        if (k == 4) {
            p.rect(cx + h, cy + y, 1, 1, col);
            p.rect(cx - h, cy + y, w + 1, 1, col);
        }
        if (k == 2) {
            int bw = std::max(2, s / 4);
            p.rect(cx - h + 1, cy + y, bw, 1, col);
            p.rect(cx + h - bw, cy + y, bw, 1, col);
        }
        if (k == 3 && std::abs(y) < h) p.rect(cx - h + 1, cy + y, s - 2, 1, col);
    }
}
// A pixel jelly sphere: stepped, dithered light, a hard glint, a glowing underside.
void jelly(const P& p, int cx, int cy, int r, uint32_t hue, int glyph_k = -1) {
    C3 base = hx(hue), dark = mix(base, {0, 10, 40}, 0.7f), lite = mix(base, {230, 255, 255}, 0.5f);
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            float d = std::hypot(float(x), float(y));
            if (d > r + 0.2f) continue;
            int X = cx + x, Y = cy + y;
            if (d > r - 1) {
                p.px(X, Y, R(0x021430));
                continue;
            }
            float nx = float(x) / r, ny = float(y) / r, nz = std::sqrt(std::max(0.f, 1 - nx * nx - ny * ny));
            float l = std::max(0.f, -0.45f * nx - 0.6f * ny + 0.65f * nz);
            if (ny > 0.4f) l += (ny - 0.4f) * 1.3f;
            float q = std::floor(l * 4 + bayer(X, Y)) / 4;
            p.px(X, Y, to565(mix(dark, lite, std::min(1.f, q))));
        }
    int gx = cx - int(std::lround(r * 0.45f)), gy = cy - int(std::lround(r * 0.55f));
    p.rect(gx, gy, std::max(2, r / 4), std::max(1, r / 8) + 1, R(0xffffff));
    p.px(gx - 1, gy + 1, R(0xffffff));
    if (glyph_k >= 0) {
        int g = r >= 13 ? 11 : r >= 10 ? 9 : 5;
        pix_glyph(p, glyph_k, cx + 1, cy + 2, g, R(0x9af8ff));
        pix_glyph(p, glyph_k, cx, cy + 1, g, R(0x001030));
        pix_glyph(p, glyph_k, cx + 1, cy + 1, g, R(0x001030));
    }
}
void socket(const P& p, int cx, int cy, int r) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            float d = std::hypot(float(x), float(y));
            if (d > r) continue;
            p.px(cx + x, cy + y, d > r - 1 ? (x + y < 0 ? R(0x0a1024) : R(0xf0f6ff)) : R(0x04102a));
        }
}
void plate(const P& p, int x, int y, const std::string& l, uint16_t col = R(0x7af4ff)) {
    int w = w3(l) + 6;
    p.rect(x - 1, y - 1, w + 2, 9, R(0x06101e));
    p.rect(x, y, w, 7, R(0x04163a));
    p.rect(x, y + 7, w, 1, R(0xc8e4ff));
    p.rect(x + w, y, 1, 8, R(0xc8e4ff));
    t3(p, l, x + 3, y + 1, col);
}
void tube(const P& p, int x, int y, int w, int h, float level, float t, const std::string& label) {
    float top = y + h - level * h;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int X = x + i, Y = y + j;
            bool liq = Y > top + std::sin(i * 0.8f + t * 5) * 1.2f;
            C3 col = liq ? mix(hx(0x0a6ab8), hx(0x4af0ff), (1 - float(i) / w) * 0.6f + (bayer(X, Y) - 0.5f) * 0.3f)
                         : mix(hx(0x061638), hx(0x0c2a5a), float(i) / w);
            if (i == 3 || i == 4) col = mix(col, {255, 255, 255}, liq ? 0.45f : 0.28f);
            if (i == w - 3) col = mix(col, {255, 255, 255}, 0.12f);
            p.px(X, Y, to565(col));
        }
    for (int i = 0; i < w; ++i) p.pxf(float(x + i), top + std::sin(i * 0.8f + t * 5) * 1.2f, R(0xc8fcff));
    for (int k = 0; k < 7; ++k) {
        float room = std::max(4.f, level * h - 4);
        int bx = x + 5 + (k * 5) % (w - 8);
        float by = y + h - 2 - std::fmod(t * (10 + k * 3) + k * 13, room);
        if (by > top + 2) {
            p.pxf(float(bx), by, R(0xc8fcff));
            p.pxf(float(bx + 1), by, R(0x7ae0ff));
            p.pxf(float(bx), by + 1, R(0x7ae0ff));
        }
    }
    p.rect(x - 1, y, 1, h, R(0x020818));
    p.rect(x + w, y, 1, h, R(0x020818));
    static const int band_k[7] = {0, 1, 2, 4, 5, 7, 8};
    for (int cy : {y - 7, y + h})
        for (int j = 0; j < 7; ++j) {
            p.rect(x - 3, cy + j, w + 6, 1, to565(kEnv[band_k[j]]));
            p.px(x - 3, cy + j, R(0x06101e));
            p.px(x + w + 2, cy + j, R(0x06101e));
        }
    p.rect(x - 3, y - 8, w + 6, 1, R(0x06101e));
    p.rect(x - 3, y + h + 7, w + 6, 1, R(0x06101e));
    for (int k = 0; k <= 10; ++k) p.rect(x - 6, y + int(std::lround(k * (h - 1) / 10.f)), k % 5 ? 2 : 3, 1, R(0x7ab0ff));
    for (size_t i = 0; i < label.size(); ++i) t3(p, std::string(1, label[i]), x + w + 5, y + 6 + int(i) * 7, R(0x9ad8ff));
}

class Chrome : public Skin {
public:
    const char* name() const override { return "Liquid Chrome"; }
    int rows_shown() const override { return 6; }

    void draw(lv_obj_t* canvas, uint16_t* buf, const View& v) override {
        if (back_.empty()) {
            back_.resize(size_t(W) * H);
            chrome_back(P{back_.data()});
        }
        std::memcpy(buf, back_.data(), back_.size() * 2);
        P p{buf};
        Text tx(canvas);
        v.list ? list(p, tx, v) : player(p, tx, v);
    }

private:
    void player(const P& p, Text&, const View& v) {
        float t = v.t;
        bubbles(p, t);
        if (player_blob_.inside.empty())
            player_blob_ = make_blob({{58, 44, 36, 0.8f}, {128, 38, 40, 0.75f}, {198, 46, 32, 0.8f}, {244, 72, 20, 1},
                                      {46, 98, 26, 1}, {112, 104, 30, 0.7f}, {176, 100, 28, 0.8f}, {228, 106, 18, 1}});
        chrome_body(p, player_blob_, t);
        // The oval screen, sunk into the metal.
        const float ex = 124, ey = 45, rx = 88, ry = 31;
        auto inE = [&](float x, float y, float a, float b) { return ((x - ex) / a) * ((x - ex) / a) + ((y - ey) / b) * ((y - ey) / b) <= 1; };
        float glow = live(v) ? beat(t) : 0;
        for (int y = int(ey - ry - 2); y <= int(ey + ry + 2); ++y)
            for (int x = int(ex - rx - 2); x <= int(ex + rx + 2); ++x) {
                if (!inE(float(x), float(y), rx + 2, ry + 2)) continue;
                if (!inE(float(x), float(y), rx, ry)) {
                    p.px(x, y, y < ey ? R(0x0a1022) : R(0xe8f4ff));
                    continue;
                }
                float g = 1 - (((x - ex) / rx) * ((x - ex) / rx) + ((y - ey) / ry) * ((y - ey) / ry));
                C3 col = mix(hx(0x000a22), hx(0x00306a), clamp01(g * 1.1f + glow * 0.15f + (bayer(x, y) - 0.5f) * 0.15f));
                if (y % 2) col = mix(col, {0, 0, 0}, 0.15f);
                p.px(x, y, to565(col));
            }
        const uint16_t CY = R(0x8afcff), CYD = R(0x0c2a50);
        // The time, double size, on the left. Paused, it blinks.
        std::string tm = v.stopped ? "0:00" : mmss(v.pos_ms);
        bool blink = v.paused && int(t * 2) % 2;
        seg7(p, blink ? std::string(tm.size(), ' ') : tm, std::max(46, 112 - seg_w(tm, 2)), 29, R(0xc8ffff), CYD,
             v.stopped ? -1 : R(0x1a7ac8), 2);
        t3(p, "SHUF", 52, 61, v.shuffle ? CY : CYD);
        t3(p, "MUTE", 72, 61, v.muted ? CY : CYD);
        // The title in big pixel letters, scrolling inside the oval.
        std::string mq = v.stopped ? "ZEROOS MEDIA - " : upper(v.title) + (v.artist.empty() ? "" : " - " + upper(v.artist)) + " - ";
        int mw = w3(mq, 3) + 3, off = int(std::fmod(t * 26, float(mw)));
        {
            // Clipped to its window and to the oval, a pixel at a time.
            for (int pass = 0; pass < 2; ++pass) {
                int x0 = 118 - off + pass * mw;
                for (size_t ci = 0; ci < mq.size(); ++ci) {
                    int gx = x0 + int(ci) * 12;
                    if (gx > 200 || gx + 12 < 116) continue;
                    uint16_t m = glyph3(mq[ci]);
                    for (int i = 0; i < 15; ++i) {
                        if (!(m & (1 << i))) continue;
                        for (int dy = 0; dy < 3; ++dy)
                            for (int dx = 0; dx < 3; ++dx) {
                                int X = gx + (i % 3) * 3 + dx, Y = 26 + (i / 3) * 3 + dy;
                                for (int sh = 1; sh >= 0; --sh) {
                                    int XX = X + sh, YY = Y + sh;
                                    if (XX < 116 || XX >= 200 || YY < 24 || YY >= 42) continue;
                                    if (!inE(float(XX), float(YY), rx - 4, ry - 3)) continue;
                                    p.px(XX, YY, sh ? R(0x0a4a8a) : R(0xffffff));
                                }
                            }
                    }
                }
            }
        }
        std::string info = v.stopped ? "STOPPED" : v.format;
        t3(p, info, 118, 45, CY);
        if (v.q_at >= 0) {
            char q[16];
            std::snprintf(q, sizeof q, "%02d/%02d", v.q_at + 1, v.q_n);
            t3(p, q, 176, 45, CY);
        }
        // Analyser, with peak dots that fall slowly.
        for (int i = 0; i < 14; ++i) {
            int x = 118 + i * 4, hgt = live(v) ? int(std::lround(band(i, 14, t) * 13)) : 0;
            for (int yy = 66 - hgt; yy < 66; ++yy)
                if (inE(float(x + 1), float(yy), rx - 3, ry - 2)) p.rect(x, yy, 3, 1, yy < 56 ? R(0xffffff) : yy < 61 ? R(0x7af4ff) : R(0x1aa8ff));
            peaks_[i] = std::max(peaks_[i] - 0.25f, float(hgt));
            int py = 65 - int(std::lround(peaks_[i]));
            if (peaks_[i] > 0.5f && inE(float(x + 1), float(py), rx - 3, ry - 2)) p.rect(x, py, 3, 1, R(0xffffff));
        }
        // The buttons in their sockets: previous, play, pause, stop, next.
        static const int bt[5][3] = {{60, 95, 10}, {88, 94, 13}, {117, 95, 10}, {142, 96, 10}, {167, 97, 10}};
        for (int k = 0; k < 5; ++k) {
            int x = bt[k][0], y = bt[k][1], r = bt[k][2];
            bool lit = v.flash == k || (k == 1 && live(v)) || (k == 2 && v.paused);
            socket(p, x, y, r + 2);
            jelly(p, x, y + (v.flash == k ? 1 : 0), r, lit ? 0x8afcff : k == 1 ? 0x3af0ff : 0x28b8ff, k);
        }
        // Volume.
        int sx = 190, sy = 98, sw = 46;
        float vol = v.muted ? 0 : v.volume / 100.f;
        p.rect(sx, sy, sw, 4, R(0x04102a));
        p.rect(sx, sy, sw, 1, R(0x0a1022));
        p.rect(sx, sy + 3, sw, 1, R(0xe8f4ff));
        for (int i = 1; i < sw - 1; ++i)
            if (float(i) / sw < vol) p.px(sx + i, sy + 1 + (i & 1), R(0x3ad8ff));
        int kx = sx + int(std::lround(vol * (sw - 1)));
        socket(p, kx, sy + 2, 4);
        jelly(p, kx, sy + 2, 3, 0x7af4ff);
        plate(p, sx - 2, sy - 11, v.muted ? "MUTE" : "VOL");
        plate(p, 20, 110, "XTAL-AMP", R(0xffffff));
        screw(p, 30, 30);
        screw(p, 214, 30);
        tube(p, 286, 14, 14, 104, progress(v), t, "TIME");
    }

    void list(const P& p, Text& tx, const View& v) {
        float t = v.t;
        bubbles(p, t + 7);
        if (list_blob_.inside.empty())
            list_blob_ = make_blob({{40, 34, 32, 0.9f}, {106, 28, 34, 0.8f}, {170, 30, 34, 0.8f}, {232, 36, 28, 0.9f},
                                    {40, 96, 32, 0.9f}, {106, 102, 34, 0.8f}, {170, 100, 34, 0.8f}, {232, 94, 28, 0.9f},
                                    {140, 66, 30, 1}});
        chrome_body(p, list_blob_, t);
        const int x0 = 14, y0 = 12, w = 248, h = 86;
        for (int y = y0 - 1; y < y0 + h + 1; ++y)
            for (int x = x0 - 1; x < x0 + w + 1; ++x) {
                bool edge = x == x0 - 1 || y == y0 - 1 || x == x0 + w || y == y0 + h;
                if (edge) {
                    p.px(x, y, x == x0 - 1 || y == y0 - 1 ? R(0x0a1022) : R(0xe8f4ff));
                    continue;
                }
                C3 col = mix(hx(0x001232), hx(0x00347a), float(y - y0) / h * 0.7f + (bayer(x, y) - 0.5f) * 0.15f);
                if (y % 2) col = mix(col, {0, 0, 0}, 0.18f);
                if (((x - x0) - (y - y0) * 2 + 300) % 140 < 7) col = mix(col, {255, 255, 255}, 0.08f);
                p.px(x, y, to565(col));
            }
        t3(p, "PLAYLIST", x0 + 4, y0 + 3, R(0x7af4ff));
        std::string crumb = upper(v.crumb);
        int room = (w - 52) / 4;
        if (int(crumb.size()) > room) crumb = ".." + crumb.substr(crumb.size() - size_t(room - 2));
        t3(p, crumb, x0 + 44, y0 + 3, R(0x2a7ab8));
        const auto& rows = *v.rows;
        int n = int(rows.size());
        for (int i = 0; i < 6; ++i) {
            int r = v.top + i;
            if (r >= n) break;
            int y = y0 + 11 + int(i * 12.5f);
            bool sel = r == v.sel, play = !v.playing_path.empty() && rows[size_t(r)].path == v.playing_path;
            if (sel) {
                for (int yy = 0; yy < 12; ++yy)
                    for (int xx = 0; xx < w - 6; ++xx) {
                        int rr = 6, dx = xx < rr ? rr - xx : xx > w - 7 - rr ? xx - (w - 7 - rr) : 0;
                        float dy = yy - 5.5f;
                        if (dx > 0 && dx * dx + dy * dy > rr * rr) continue;
                        p.px(x0 + 3 + xx, y + yy,
                             to565(mix(hx(0x0a7ad8), hx(0xb8fcff), (1 - yy / 12.f) * 0.9f + (bayer(xx, yy) - 0.5f) * 0.2f)));
                    }
                p.rect(x0 + 10, y + 1, 30, 1, R(0xffffff));
            }
            if (play) {
                int rr = 2 + (live(v) ? int(std::lround(beat(t) * 1.4f)) : 0);
                for (float a = 0; a < 6.28f; a += 0.4f) p.pxf(x0 + 9 + std::cos(a) * rr, y + 6 + std::sin(a) * rr, R(0x7af4ff));
            }
            const Row& row = rows[size_t(r)];
            uint32_t col = sel ? 0x00204a : play ? 0xffffff : row.note ? 0x2a7ab8 : row.folder ? 0xd8f4ff : 0x9cd8ff;
            int rw = row.right.empty() ? 0 : Text::width(row.right, &::zs_font_12) + 6;
            tx.draw(row_text(row), x0 + 16, y - 2, &::zs_font_12, col, box(x0 + 14, y - 1, w - 22 - rw, 14));
            tx.draw(row.right, x0 + w - 8, y - 2, &::zs_font_12, col, box(x0 + 100, y - 1, w - 106, 14), true);
        }
        static const char* labels[5] = {"BACK", "FIND", "SHUF", "SKIN", "DECK"};
        for (int i = 0; i < 5; ++i) {
            int x = 26 + i * 48, y = 113;
            socket(p, x, y, 8);
            jelly(p, x, y, 6, i == 2 && v.shuffle ? 0x8afcff : i == 3 ? 0xff71ce : 0x28b8ff);
            plate(p, x + 10, y - 4, labels[i]);
        }
        float level = n > 6 ? 1 - float(v.top) / (n - 6) : 1;
        tube(p, 286, 14, 14, 104, 0.08f + level * 0.88f, t, "LIST");
    }

    std::vector<uint16_t> back_;
    Blob player_blob_, list_blob_;
    float peaks_[14] = {};
};

}  // namespace

std::unique_ptr<Skin> make_hifi_skin() { return std::make_unique<HiFi>(); }
std::unique_ptr<Skin> make_chrome_skin() { return std::make_unique<Chrome>(); }

}  // namespace zs::media
