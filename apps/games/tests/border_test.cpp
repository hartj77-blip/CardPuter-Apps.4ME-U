// The borders around a game's picture (border.h): they fill the bars, never
// the picture. cmake ... && ctest, or run border_test.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "apps/games/border.h"

using namespace zs::games;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static const int W = 320, H = 170;

// A minimal PNG writer (RGBA, uncompressed deflate), to make test pictures.
static uint32_t crc(const std::vector<uint8_t>& d, size_t from) {
    uint32_t c = 0xffffffffu;
    for (size_t i = from; i < d.size(); ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
static void be32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 3; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i)));
}
static void chunk(std::vector<uint8_t>& f, const char* type, const std::vector<uint8_t>& data) {
    be32(f, uint32_t(data.size()));
    size_t at = f.size();
    f.insert(f.end(), type, type + 4);
    f.insert(f.end(), data.begin(), data.end());
    be32(f, crc(f, at));
}
static bool write_png(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);   // no filter
        raw.insert(raw.end(), rgba.begin() + y * w * 4, rgba.begin() + (y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) a = (a + v) % 65521, b = (b + a) % 65521;
    for (size_t i = 0; i < raw.size() || i == 0; i += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(i), raw.begin() + long(i + n));
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w)), be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    std::vector<uint8_t> f = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    chunk(f, "IHDR", ihdr);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) return false;
    bool ok = std::fwrite(f.data(), 1, f.size(), out) == f.size();
    return std::fclose(out) == 0 && ok;
}

// A w x h picture, all green, with a see-through window (none if hw = 0).
static std::vector<uint8_t> picture(int w, int h, int hx, int hy, int hw, int hh) {
    std::vector<uint8_t> v(size_t(w * h * 4));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &v[size_t((y * w + x) * 4)];
            bool hole = x >= hx && x < hx + hw && y >= hy && y < hy + hh;
            p[0] = 0, p[1] = 255, p[2] = 0, p[3] = hole ? 0 : 255;
        }
    return v;
}

static void image_tests() {
    char dir[] = "/tmp/border_test.XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(false);
        return;
    }
    std::string d = dir;
    // A Super Game Boy style border: 256 x 224, a 160 x 144 window at 48, 40.
    CHECK(write_png(d + "/gb.png", 256, 224, picture(256, 224, 48, 40, 160, 144)));
    CHECK(write_png(d + "/Some Game.png", 64, 36, picture(64, 36, 0, 0, 0, 0)));
    CHECK(find_border_image(d, "gb", "Some Game") == d + "/Some Game.png");   // the game's own first
    CHECK(find_border_image(d, "gb", "Other") == d + "/gb.png");               // then the console's
    CHECK(find_border_image(d, "nes", "Other").empty());
    CHECK(write_png(d + "/default.png", 8, 8, picture(8, 8, 0, 0, 0, 0)));
    CHECK(find_border_image(d, "nes", "Other") == d + "/default.png");

    BorderImage img;
    std::string err;
    CHECK(load_border_image(d + "/gb.png", img, err));
    CHECK(img.w == 256 && img.h == 224);
    CHECK(img.hole.x == 48 && img.hole.y == 40 && img.hole.w == 160 && img.hole.h == 144);
    // On a Game Boy picture at 80, 13 the window lands on the picture at 1:1:
    // the border is green right up to it, and the picture area is black.
    Rect gb{80, 13, 160, 144};
    std::vector<uint16_t> out(W * H, 0x1234);
    draw_image(out.data(), W, H, gb, img);
    auto at = [&](int x, int y) { return out[size_t(y * W + x)]; };
    CHECK(at(79, 80) == 0x07e0 && at(240, 80) == 0x07e0 && at(160, 12) == 0x07e0);
    CHECK(at(80, 13) == 0 && at(239, 156) == 0);
    CHECK(at(31, 80) == 0 && at(288, 80) == 0);   // left and right of the border: black
    CHECK(at(32, 80) == 0x07e0 && at(287, 80) == 0x07e0);

    // No window: it covers the whole screen.
    CHECK(load_border_image(d + "/Some Game.png", img, err));
    CHECK(img.hole.w == 0);
    draw_image(out.data(), W, H, gb, img);
    CHECK(at(0, 0) == 0x07e0 && at(319, 169) == 0x07e0 && at(100, 50) == 0);

    // Not a picture: an error, nothing loaded.
    FILE* f = std::fopen((d + "/bad.png").c_str(), "wb");
    if (f) std::fputs("not a png", f), std::fclose(f);
    CHECK(!load_border_image(d + "/bad.png", img, err) && img.empty() && !err.empty());
    CHECK(!load_border_image(d + "/missing.png", img, err));

    std::string cmd = "rm -rf '" + d + "'";
    if (std::system(cmd.c_str()) != 0) CHECK(false);
}

int main() {
    CHECK(border_from_name("off") == Border::Off);
    CHECK(border_from_name("glow") == Border::Glow);
    CHECK(border_from_name("frame") == Border::Frame);
    CHECK(border_from_name("nonsense") == Border::Frame);
    CHECK(border_from_name("image") == Border::Image);
    CHECK(next_border(next_border(next_border(next_border(Border::Frame)))) == Border::Frame);
    image_tests();

    // Picture sizes from the consoles: Game Boy, GBA, a 4:3 picture filling the height, the whole screen.
    const Rect rects[] = {{80, 13, 160, 144}, {40, 5, 240, 160}, {46, 0, 227, 170}, {0, 0, 320, 170}};
    const char* ids[] = {"gb", "gbc", "gba", "ws", "nes", "snes", "md", "sms", "pce", "a26", "psx", "test", ""};
    for (const Rect& r : rects) {
        for (const char* id : ids) {
            std::vector<uint16_t> out(W * H, 0x1234);
            draw_frame(out.data(), W, H, r, id);
            bool pic_black = true, bars_drawn = true;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    bool in = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
                    uint16_t c = out[size_t(y * W + x)];
                    if (in && c != 0) pic_black = false;
                    if (!in && c == 0x1234) bars_drawn = false;
                }
            CHECK(pic_black);
            CHECK(bars_drawn);
        }
        // Glow: the bars take the edge colours (dimmed); the picture is not touched.
        std::vector<uint16_t> pic(size_t(r.w * r.h), 0xf800);   // all red
        std::vector<uint16_t> out(W * H, 0x1234);
        draw_glow(out.data(), W, H, r, pic.data());
        bool untouched = true, reddish = true;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                bool in = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
                uint16_t c = out[size_t(y * W + x)];
                if (in && c != 0x1234) untouched = false;
                if (!in && ((c & 0x07ff) != 0 || (c >> 11) == 0)) reddish = false;
            }
        CHECK(untouched);
        CHECK(reddish);
    }
    if (failures) return 1;
    std::printf("ok   border_test\n");
    return 0;
}
