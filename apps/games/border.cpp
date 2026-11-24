#include "apps/games/border.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

// stb_image (public domain, third_party/stb_image.h), for PNG only: ZeroOS's
// LVGL is built without an image decoder.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "apps/games/third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace zs::games {
namespace {

struct Rgb {
    float r = 0, g = 0, b = 0;
};

Rgb rgb(uint32_t hex) { return {float((hex >> 16) & 0xff), float((hex >> 8) & 0xff), float(hex & 0xff)}; }

Rgb unpack(uint16_t c) {
    return {float(((c >> 11) & 31) * 255 / 31), float(((c >> 5) & 63) * 255 / 63), float((c & 31) * 255 / 31)};
}

uint16_t pack(Rgb c) {
    auto q = [](float v, int bits) {
        int m = (1 << bits) - 1;
        return std::clamp(int(std::lround(v * m / 255.0f)), 0, m);
    };
    return uint16_t((q(c.r, 5) << 11) | (q(c.g, 6) << 5) | q(c.b, 5));
}

Rgb scale(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// A console's colours. Plain colours only: no names or logos are drawn.
struct Look {
    uint32_t body, bezel, accent;
    bool handheld;
};

Look look_for(const std::string& id) {
    if (id == "gb") return {0xc6c3bd, 0x5b5e6e, 0x9c2f63, true};
    if (id == "gbc") return {0x5d4290, 0x2a2a33, 0x9a7bd0, true};
    if (id == "gba") return {0x3d4192, 0x23242c, 0x7a7ee0, true};
    if (id == "ws") return {0x8d9097, 0x2c2e33, 0x3fa3c4, true};
    if (id == "nes") return {0xbdbdbd, 0x1e1e1e, 0xb0202a, false};
    if (id == "snes") return {0xcac8d0, 0x2b2a30, 0x5f4da0, false};
    if (id == "md") return {0x1f1f22, 0x0b0b0c, 0xc0302a, false};
    if (id == "sms") return {0x202024, 0x0b0b0c, 0xb02020, false};
    if (id == "pce") return {0xe4e4e0, 0x2a2a2a, 0xe07020, false};
    if (id == "a26") return {0x5a3a22, 0x111111, 0xc8a060, false};
    if (id == "psx") return {0xb9b9bf, 0x2a2a2e, 0x6080c0, false};
    return {0x3a3f4a, 0x16181c, 0x7a8090, false};
}

struct Canvas {
    uint16_t* px;
    int w, h;
    void set(int x, int y, uint16_t c) {
        if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = c;
    }
    void fill(int x0, int y0, int x1, int y1, uint16_t c) {   // [x0, x1) x [y0, y1)
        x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w), y1 = std::min(y1, h);
        for (int y = y0; y < y1; ++y) std::fill(px + y * w + x0, px + y * w + std::max(x0, x1), c);
    }
    // A rectangle with rounded corners of radius r.
    void round_rect(int x0, int y0, int x1, int y1, int r, uint16_t c) {
        for (int y = std::max(y0, 0); y < std::min(y1, h); ++y)
            for (int x = std::max(x0, 0); x < std::min(x1, w); ++x) {
                int dx = x < x0 + r ? x0 + r - x : x >= x1 - r ? x - (x1 - r - 1) : 0;
                int dy = y < y0 + r ? y0 + r - y : y >= y1 - r ? y - (y1 - r - 1) : 0;
                if (dx * dx + dy * dy <= r * r) px[y * w + x] = c;
            }
    }
    void disc(int cx, int cy, int r, uint16_t c) {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r) set(cx + x, cy + y, c);
    }
};

}  // namespace

const char* border_name(Border b) {
    switch (b) {
        case Border::Off: return "Off";
        case Border::Frame: return "Frame";
        case Border::Glow: return "Glow";
        case Border::Image: return "Image";
    }
    return "Frame";
}

Border border_from_name(const std::string& s) {
    if (s == "off" || s == "Off") return Border::Off;
    if (s == "glow" || s == "Glow") return Border::Glow;
    if (s == "image" || s == "Image") return Border::Image;
    return Border::Frame;
}

Border next_border(Border b) {
    switch (b) {
        case Border::Off: return Border::Frame;
        case Border::Frame: return Border::Glow;
        case Border::Glow: return Border::Image;
        case Border::Image: return Border::Off;
    }
    return Border::Frame;
}

void draw_frame(uint16_t* out, int sw, int sh, Rect pic, const std::string& system) {
    Canvas cv{out, sw, sh};
    Look lk = look_for(system);
    Rgb body = rgb(lk.body), bezel = rgb(lk.bezel), accent = rgb(lk.accent);
    bool light = body.r + body.g + body.b > 3 * 110;
    Rgb groove = light ? scale(body, 0.72f) : mix(body, rgb(0xffffff), 0.12f);

    // The case: a little lighter at the top, darker at the bottom.
    for (int y = 0; y < sh; ++y) {
        float t = sh > 1 ? float(y) / float(sh - 1) : 0;
        cv.fill(0, y, sw, y + 1, pack(scale(body, 1.08f - 0.18f * t)));
    }

    // The screen's surround: up to 8 pixels around the picture, as room allows.
    int pl = std::min(8, pic.x), pt = std::min(8, pic.y);
    int pr = std::min(8, sw - (pic.x + pic.w)), pb = std::min(8, sh - (pic.y + pic.h));
    int bx0 = pic.x - pl, by0 = pic.y - pt, bx1 = pic.x + pic.w + pr, by1 = pic.y + pic.h + pb;
    int r = std::min({6, pl, pt, pr, pb});
    if (r >= 2) cv.round_rect(bx0 - 1, by0 - 1, bx1 + 1, by1 + 1, r + 1, pack(scale(body, 0.6f)));   // edge
    cv.round_rect(bx0, by0, bx1, by1, std::max(r, 0), pack(bezel));
    cv.fill(pic.x, pic.y, pic.x + pic.w, pic.y + pic.h, 0);

    int left = bx0, right = sw - bx1;   // room beside the surround
    if (lk.handheld) {
        // A thin accent line over the screen, like the handhelds' printed trim.
        if (pt >= 5) cv.fill(bx0 + r + 3, by0 + 2, bx1 - r - 3, by0 + 3, pack(accent));
        // Power light on the left.
        if (left >= 18) {
            int cx = left / 2, cy = pic.y + pic.h / 3;
            cv.disc(cx, cy, 4, pack(scale(body, 0.55f)));
            cv.disc(cx, cy, 3, pack(rgb(0xd8262a)));
            cv.set(cx - 1, cy - 1, pack(rgb(0xff8a80)));
        }
        // Speaker slots on the right: short slanted grooves.
        if (right >= 24) {
            int cx = bx1 + right / 2, cy = pic.y + pic.h * 2 / 3;
            int n = std::min(6, right / 7);
            for (int i = 0; i < n; ++i) {
                int x0 = cx - (n - 1) * 3 + i * 6;
                for (int k = 0; k < 18; ++k) {
                    int x = x0 + (9 - k) / 3, y = cy - 9 + k;
                    cv.set(x, y, pack(groove));
                    cv.set(x + 1, y, pack(scale(groove, 0.85f)));
                }
            }
        }
    } else if (left >= 10 || right >= 10) {
        // Trim: an accent stripe across each side, with vent grooves under it.
        int cy = pic.y + pic.h / 2;
        auto side = [&](int x0, int x1) {
            if (x1 - x0 < 10) return;
            cv.fill(x0, cy - 2, x1 - 3, cy + 1, pack(accent));
            for (int g = 1; g <= 4; ++g) cv.fill(x0 + 4, cy + 3 + g * 5, x1 - 7, cy + 4 + g * 5, pack(groove));
        };
        side(0, left);
        // The right side, drawn mirrored so the stripe runs from the edge.
        if (right >= 10) {
            cv.fill(bx1 + 3, cy - 2, sw, cy + 1, pack(accent));
            for (int g = 1; g <= 4; ++g) cv.fill(bx1 + 7, cy + 3 + g * 5, sw - 4, cy + 4 + g * 5, pack(groove));
        }
    }
}

void draw_glow(uint16_t* out, int sw, int sh, Rect pic, const uint16_t* pixels) {
    if (pic.w <= 0 || pic.h <= 0 || !pixels) return;
    const int kEdge = 4;      // pixels averaged at each edge
    const int kBlur = 8;      // soften along the edge
    // Average colour along each edge: one per row (left, right), one per column (top, bottom).
    auto edge = [&](int n, auto at, int depth) {
        std::vector<Rgb> v(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            Rgb s;
            for (int d = 0; d < depth; ++d) {
                Rgb c = unpack(at(i, d));
                s.r += c.r, s.g += c.g, s.b += c.b;
            }
            v[size_t(i)] = scale(s, 1.0f / float(depth));
        }
        std::vector<Rgb> b(static_cast<size_t>(n));   // box blur
        for (int i = 0; i < n; ++i) {
            Rgb s;
            int k = 0;
            for (int j = std::max(0, i - kBlur); j <= std::min(n - 1, i + kBlur); ++j, ++k)
                s.r += v[size_t(j)].r, s.g += v[size_t(j)].g, s.b += v[size_t(j)].b;
            b[size_t(i)] = scale(s, 1.0f / float(k));
        }
        return b;
    };
    int dx = std::min(kEdge, pic.w), dy = std::min(kEdge, pic.h);
    auto L = edge(pic.h, [&](int y, int d) { return pixels[y * pic.w + d]; }, dx);
    auto R = edge(pic.h, [&](int y, int d) { return pixels[y * pic.w + pic.w - 1 - d]; }, dx);
    auto T = edge(pic.w, [&](int x, int d) { return pixels[d * pic.w + x]; }, dy);
    auto B = edge(pic.w, [&](int x, int d) { return pixels[(pic.h - 1 - d) * pic.w + x]; }, dy);

    int px1 = pic.x + pic.w, py1 = pic.y + pic.h;
    for (int y = 0; y < sh; ++y) {
        bool row_in = y >= pic.y && y < py1;
        for (int x = 0; x < sw; ++x) {
            bool col_in = x >= pic.x && x < px1;
            if (row_in && col_in) {
                x = px1 - 1;   // skip the picture
                continue;
            }
            int cx = std::clamp(x, pic.x, px1 - 1) - pic.x, cy = std::clamp(y, pic.y, py1 - 1) - pic.y;
            int ddx = x < pic.x ? pic.x - x : x >= px1 ? x - px1 + 1 : 0;
            int ddy = y < pic.y ? pic.y - y : y >= py1 ? y - py1 + 1 : 0;
            Rgb c = ddx >= ddy ? (x < pic.x ? L : R)[size_t(cy)] : (y < pic.y ? T : B)[size_t(cx)];
            float d = float(std::max(ddx, ddy));
            float k = 0.08f + 0.47f * std::max(0.0f, 1.0f - d / 72.0f);   // 55% at the picture, fading out
            out[y * sw + x] = pack(scale(c, k));
        }
    }
}

}  // namespace zs::games

namespace zs::games {

bool load_border_image(const std::string& path, BorderImage& out, std::string& error) {
    out = BorderImage{};
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Can't open " + path;
        return false;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.empty() || file.size() > (size_t(32) << 20)) {
        error = "The border picture is empty or too big";
        return false;
    }
    int w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4);
    if (!px) {
        const char* why = stbi_failure_reason();
        error = std::string("Not a PNG this app can read") + (why ? std::string(" (") + why + ")" : "");
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);

    // The window: the see-through area around the middle, if the middle is see-through.
    auto clear = [&](int x, int y) { return out.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] < 128; };
    int cx = w / 2, cy = h / 2;
    if (clear(cx, cy)) {
        std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
        std::vector<int> todo = {cy * w + cx};
        seen[size_t(cy * w + cx)] = 1;
        int x0 = cx, x1 = cx, y0 = cy, y1 = cy;
        while (!todo.empty()) {
            int i = todo.back();
            todo.pop_back();
            int x = i % w, y = i / w;
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                int j = ny[k] * w + nx[k];
                if (seen[size_t(j)] || !clear(nx[k], ny[k])) continue;
                seen[size_t(j)] = 1;
                todo.push_back(j);
            }
        }
        // A window that is the whole picture is no window: it's an empty picture.
        if (x1 - x0 + 1 < w || y1 - y0 + 1 < h) out.hole = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    }
    return true;
}

std::string find_border_image(const std::string& dir, const std::string& system, const std::string& game) {
    for (const std::string& name : {game, system, std::string("default")}) {
        if (name.empty()) continue;
        std::string p = dir + "/" + name + ".png";
        if (std::ifstream(p).good()) return p;
    }
    return "";
}

void draw_image(uint16_t* out, int sw, int sh, Rect pic, const BorderImage& img) {
    std::fill(out, out + size_t(sw) * size_t(sh), uint16_t(0));
    if (img.empty() || img.w <= 0 || img.h <= 0) return;
    // Screen pixel x = src x * s + ox.
    double s, ox, oy;
    if (img.hole.w > 0 && img.hole.h > 0 && pic.w > 0 && pic.h > 0) {
        s = std::max(double(pic.w) / img.hole.w, double(pic.h) / img.hole.h);
        ox = pic.x + pic.w / 2.0 - (img.hole.x + img.hole.w / 2.0) * s;
        oy = pic.y + pic.h / 2.0 - (img.hole.y + img.hole.h / 2.0) * s;
    } else {
        s = std::max(double(sw) / img.w, double(sh) / img.h);
        ox = (sw - img.w * s) / 2.0;
        oy = (sh - img.h * s) / 2.0;
    }
    // Each screen pixel covers source pixels [a, b): average them (a box
    // filter: a 1920 x 1080 bezel shrinks well), over black.
    auto span = [&](int n, int size, double o, std::vector<int>& a, std::vector<int>& b) {
        a.resize(size_t(n));
        b.resize(size_t(n));
        for (int i = 0; i < n; ++i) {
            double lo = (i - o) / s, hi = (i + 1 - o) / s;
            int ia = int(std::floor(lo + 1e-9)), ib = int(std::ceil(hi - 1e-9));
            if (ib <= ia) ib = ia + 1;
            a[size_t(i)] = std::clamp(ia, 0, size);
            b[size_t(i)] = std::clamp(ib, 0, size);
            if (ib <= 0 || ia >= size) a[size_t(i)] = b[size_t(i)] = 0;   // off the picture
        }
    };
    std::vector<int> xa, xb, ya, yb;
    span(sw, img.w, ox, xa, xb);
    span(sh, img.h, oy, ya, yb);
    for (int y = 0; y < sh; ++y) {
        if (ya[size_t(y)] >= yb[size_t(y)]) continue;
        for (int x = 0; x < sw; ++x) {
            if (x >= pic.x && x < pic.x + pic.w && y >= pic.y && y < pic.y + pic.h) continue;
            if (xa[size_t(x)] >= xb[size_t(x)]) continue;
            uint64_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = ya[size_t(y)]; sy < yb[size_t(y)]; ++sy) {
                const uint8_t* p = &img.rgba[(size_t(sy) * size_t(img.w) + size_t(xa[size_t(x)])) * 4];
                for (int sx = xa[size_t(x)]; sx < xb[size_t(x)]; ++sx, p += 4) {
                    r += uint64_t(p[0]) * p[3], g += uint64_t(p[1]) * p[3], b += uint64_t(p[2]) * p[3];
                    ++n;
                }
            }
            n *= 255;
            out[y * sw + x] = pack({float(r) / n, float(g) / n, float(b) / n});
        }
    }
}

}  // namespace zs::games
