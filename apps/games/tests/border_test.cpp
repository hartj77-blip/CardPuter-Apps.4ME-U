// The borders around a game's picture (border.h): they fill the bars, never
// the picture. cmake ... && ctest, or run border_test.
#include <cstdio>
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

int main() {
    CHECK(border_from_name("off") == Border::Off);
    CHECK(border_from_name("glow") == Border::Glow);
    CHECK(border_from_name("frame") == Border::Frame);
    CHECK(border_from_name("nonsense") == Border::Frame);
    CHECK(next_border(next_border(next_border(Border::Frame))) == Border::Frame);

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
