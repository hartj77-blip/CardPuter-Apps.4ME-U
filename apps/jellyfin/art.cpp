// See art.h.
#include "apps/jellyfin/art.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace zs::jfapp {
namespace {

constexpr long kMaxPixels = 12L * 1024 * 1024;   // 36 MB of RGB while decoding
constexpr char kMagic[4] = {'Z', 'J', 'A', '1'};

// 4 x 4 ordered dither: 16-bit colour bands on the soft gradients covers are
// full of; this spreads the steps so they don't show.
constexpr int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint16_t to565(int r, int g, int b, int x, int y) {
    int d = kBayer[y & 3][x & 3];           // 0..15
    r = std::min(255, r + (d >> 1));        // 5 bits: steps of 8
    g = std::min(255, g + (d >> 2));        // 6 bits: steps of 4
    b = std::min(255, b + (d >> 1));
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t hash(const std::string& s) {
    uint32_t h = 2166136261u;   // FNV-1a
    for (unsigned char ch : s) h = (h ^ ch) * 16777619u;
    return h;
}

struct Rgb {
    int r, g, b;
};

Rgb mix(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t), int(a.b + (b.b - a.b) * t)};
}

}  // namespace

void cover_from_rgb(const uint8_t* rgb, int sw, int sh, int w, int h, Pixels& out) {
    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * size_t(h), 0);
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0) return;
    // The part of the picture that has the card's shape, centred.
    double scale = std::max(double(w) / sw, double(h) / sh);
    double cw = w / scale, ch = h / scale;
    double x0 = (sw - cw) / 2, y0 = (sh - ch) / 2;
    for (int y = 0; y < h; ++y) {
        int ya = std::clamp(int(y0 + y * ch / h), 0, sh - 1);
        int yb = std::clamp(int(y0 + (y + 1) * ch / h), ya + 1, sh);
        for (int x = 0; x < w; ++x) {
            int xa = std::clamp(int(x0 + x * cw / w), 0, sw - 1);
            int xb = std::clamp(int(x0 + (x + 1) * cw / w), xa + 1, sw);
            // Each pixel on screen is the average of the ones it covers.
            long r = 0, g = 0, b = 0;
            for (int yy = ya; yy < yb; ++yy) {
                const uint8_t* p = rgb + (size_t(yy) * size_t(sw) + size_t(xa)) * 3;
                for (int xx = xa; xx < xb; ++xx, p += 3) {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            }
            long n = long(yb - ya) * (xb - xa);
            out.px[size_t(y) * size_t(w) + size_t(x)] = to565(int(r / n), int(g / n), int(b / n), x, y);
        }
    }
}

bool decode_cover(const std::string& bytes, int w, int h, Pixels& out) {
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF || w <= 0 || h <= 0) return false;
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    int len = int(bytes.size());
    int sw = 0, sh = 0, n = 0;
    if (!stbi_info_from_memory(data, len, &sw, &sh, &n)) return false;
    if (sw <= 0 || sh <= 0 || long(sw) * sh > kMaxPixels) return false;
    stbi_uc* px = stbi_load_from_memory(data, len, &sw, &sh, &n, 3);
    if (!px) return false;
    cover_from_rgb(px, sw, sh, w, h, out);
    stbi_image_free(px);
    return true;
}

void demo_picture(const std::string& name, Kind kind, int w, int h, Pixels& out) {
    out.w = w;
    out.h = h;
    out.px.assign(size_t(std::max(0, w)) * size_t(std::max(0, h)), 0);
    if (w <= 0 || h <= 0) return;
    static const Rgb kPairs[][2] = {
        {{214, 51, 108}, {60, 20, 80}},   {{240, 160, 32}, {120, 40, 20}}, {{40, 20, 120}, {230, 60, 140}},
        {{20, 110, 120}, {10, 30, 60}},   {{90, 170, 80}, {30, 80, 50}},   {{120, 60, 30}, {20, 20, 40}},
        {{60, 90, 130}, {10, 15, 30}},    {{200, 60, 90}, {40, 10, 40}},   {{70, 50, 120}, {200, 120, 60}},
        {{30, 120, 160}, {10, 30, 50}},
    };
    uint32_t hv = hash(name);
    const Rgb top = kPairs[hv % 10][0], bottom = kPairs[hv % 10][1];
    const Rgb light = {255, 236, 170}, dark = {14, 18, 26};
    // What is drawn over the gradient: a sun for music, hills for film,
    // bars for shows; folders get stripes.
    int shape = kind == Kind::Album || kind == Kind::Track || kind == Kind::Artist || kind == Kind::Playlist ? 0
                : kind == Kind::Movie || kind == Kind::Video ? 1
                : kind == Kind::Folder ? 3 : 2;
    shape = (shape + int((hv >> 8) % 2) * (shape == 0 ? 2 : 0)) % 4;   // some covers get bars
    double sx = 0.3 + double((hv >> 12) % 40) / 100.0;                 // where the sun or the peak sits
    double m = std::min(w, h);
    for (int y = 0; y < h; ++y) {
        double v = double(y) / std::max(1, h - 1);
        for (int x = 0; x < w; ++x) {
            double u = double(x) / std::max(1, w - 1);
            Rgb c = mix(top, bottom, v);
            if (shape == 0) {
                double dx = (x - sx * w) / m, dy = (y - 0.36 * h) / m;
                double d = std::sqrt(dx * dx + dy * dy);
                if (d < 0.22) c = light;
                else if (d < 0.26) c = mix(light, c, (d - 0.22) / 0.04);
                if (v > 0.72 && int(v * 40) % 3 == 0) c = mix(c, dark, 0.35);
            } else if (shape == 1) {
                double ridge = 0.78 - 0.34 * std::max(0.0, 1.0 - std::fabs(u - sx) * 2.4) -
                               0.16 * std::max(0.0, 1.0 - std::fabs(u - (1.0 - sx)) * 3.2);
                double dx = (x - (1.0 - sx) * w) / m, dy = (y - 0.24 * h) / m;
                if (std::sqrt(dx * dx + dy * dy) < 0.09) c = light;
                if (v > ridge) c = mix(dark, bottom, (v - ridge) * 0.6);
            } else if (shape == 2) {
                int bar = int(u * 7);
                double top_of = 0.25 + 0.09 * double((hv >> (bar * 3)) % 6);
                if (bar >= 1 && bar <= 5 && u * 7 - bar > 0.22 && u * 7 - bar < 0.78 && v > top_of && v < 0.82)
                    c = mix(light, c, 0.15);
            } else {
                if (int((u + v) * 9) % 2 == 0) c = mix(c, dark, 0.3);
            }
            out.px[size_t(y) * size_t(w) + size_t(x)] = to565(c.r, c.g, c.b, x, y);
        }
    }
}

std::string cache_name(const Pic& pic, int w, int h) {
    if (!safe_id(pic.id) || w <= 0 || h <= 0) return "";
    std::string n = pic.id + "-";
    n += pic.type == "Thumb" ? 't' : pic.type == "Backdrop" ? 'b' : 'p';
    n += "-" + std::to_string(w) + "x" + std::to_string(h);
    // The tag changes when the picture does, so an old file is never shown
    // for a new cover. Only its plain characters, and not too many.
    std::string tag;
    for (unsigned char ch : pic.tag)
        if (std::isalnum(ch) && tag.size() < 40) tag += char(ch);
    if (!tag.empty()) n += "-" + tag;
    return n + ".565";
}

std::string cache_encode(const Pixels& p) {
    std::string s(kMagic, 4);
    s += char(p.w & 0xFF);
    s += char((p.w >> 8) & 0xFF);
    s += char(p.h & 0xFF);
    s += char((p.h >> 8) & 0xFF);
    s.reserve(8 + p.px.size() * 2);
    for (uint16_t v : p.px) {
        s += char(v & 0xFF);
        s += char(v >> 8);
    }
    return s;
}

bool cache_decode(const std::string& bytes, int w, int h, Pixels& out) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kMagic, 4) != 0) return false;
    const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
    int fw = b[4] | (b[5] << 8), fh = b[6] | (b[7] << 8);
    if (fw != w || fh != h || w <= 0 || h <= 0) return false;
    if (bytes.size() != 8 + size_t(w) * size_t(h) * 2) return false;
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = uint16_t(b[8 + i * 2] | (b[9 + i * 2] << 8));
    return true;
}

uint32_t backdrop_for(const Pixels& p) {
    if (p.px.empty()) return 0;
    long r = 0, g = 0, b = 0;
    for (uint16_t v : p.px) {
        r += (v >> 11) * 255 / 31;
        g += ((v >> 5) & 0x3F) * 255 / 63;
        b += (v & 0x1F) * 255 / 31;
    }
    long n = long(p.px.size());
    r /= n;
    g /= n;
    b /= n;
    // At most a quarter of full light, keeping the colour: white text on it
    // keeps a contrast of 10:1 or better.
    long top = std::max({r, g, b});
    if (top > 64) {
        r = r * 64 / top;
        g = g * 64 / top;
        b = b * 64 / top;
    }
    return uint32_t((r << 16) | (g << 8) | b);
}

uint32_t tint_for(const std::string& name) {
    // Jellyfin's defaultCardBackground1 to 5, at about a third of their light.
    static const uint32_t kTints[] = {0x4a3e0c, 0x143646, 0x282740, 0x4e1a12, 0x21483c};
    return kTints[hash(name) % 5];
}

}  // namespace zs::jfapp
