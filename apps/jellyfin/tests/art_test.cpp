// Cover art (art.h): pictures decoded and cropped to a card, the files they
// are kept in, the simulator's pictures. cmake ... && ctest, or run
// jellyfin_art_test.
#include <cstdio>
#include <string>
#include <vector>

#include "apps/jellyfin/art.h"

using namespace zs::jfapp;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

// A minimal PNG writer (RGB, uncompressed deflate), to make test pictures.
static uint32_t crc(const std::string& d, size_t from) {
    uint32_t c = 0xffffffffu;
    for (size_t i = from; i < d.size(); ++i) {
        c ^= uint8_t(d[i]);
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
static void be32(std::string& d, uint32_t v) {
    for (int i = 3; i >= 0; --i) d += char(v >> (8 * i));
}
static void chunk(std::string& f, const char* type, const std::string& data) {
    be32(f, uint32_t(data.size()));
    size_t at = f.size();
    f.append(type, 4);
    f += data;
    be32(f, crc(f, at));
}
static std::string png(int w, int h, const std::vector<uint8_t>& rgb) {
    std::string raw;
    for (int y = 0; y < h; ++y) {
        raw += char(0);   // filter: none
        raw.append(reinterpret_cast<const char*>(rgb.data()) + size_t(y) * size_t(w) * 3, size_t(w) * 3);
    }
    std::string z("\x78\x01", 2);
    uint32_t a = 1, b = 0;
    for (unsigned char ch : raw) {
        a = (a + ch) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw.size();) {
        size_t n = std::min<size_t>(65535, raw.size() - at);
        z += char(at + n == raw.size() ? 1 : 0);
        z += char(n & 0xFF);
        z += char(n >> 8);
        z += char(~n & 0xFF);
        z += char((~n >> 8) & 0xFF);
        z.append(raw, at, n);
        at += n;
    }
    be32(z, (b << 16) | a);
    std::string f("\x89PNG\r\n\x1a\n", 8), head;
    be32(head, uint32_t(w));
    be32(head, uint32_t(h));
    head.append("\x08\x02\x00\x00\x00", 5);   // 8 bits, RGB
    chunk(f, "IHDR", head);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", "");
    return f;
}

// A small JPEG (16 x 16, mid grey), as a server would send.
static const unsigned char kJpeg[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x0d, 0x09, 0x0a, 0x0b, 0x0a, 0x08, 0x0d, 0x0b, 0x0a, 0x0b, 0x0e,
    0x0e, 0x0d, 0x0f, 0x13, 0x20, 0x15, 0x13, 0x12, 0x12, 0x13, 0x27, 0x1c, 0x1e, 0x17, 0x20, 0x2e, 0x29, 0x31,
    0x30, 0x2e, 0x29, 0x2d, 0x2c, 0x33, 0x3a, 0x4a, 0x3e, 0x33, 0x36, 0x46, 0x37, 0x2c, 0x2d, 0x40, 0x57, 0x41,
    0x46, 0x4c, 0x4e, 0x52, 0x53, 0x52, 0x32, 0x3e, 0x5a, 0x61, 0x5a, 0x50, 0x60, 0x4a, 0x51, 0x52, 0x4f, 0xff,
    0xc0, 0x00, 0x0b, 0x08, 0x00, 0x10, 0x00, 0x10, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
    0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01,
    0x00, 0x00, 0x3f, 0x00, 0x28, 0xa2, 0x8a, 0xff, 0xd9};

static int r5(uint16_t v) { return v >> 11; }
static int g6(uint16_t v) { return (v >> 5) & 0x3F; }
static int b5(uint16_t v) { return v & 0x1F; }

static void test_cover() {
    // 40 x 20: the left half red, the right half blue, a green line along the top.
    const int sw = 40, sh = 20;
    std::vector<uint8_t> rgb(size_t(sw) * sh * 3);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            uint8_t* p = &rgb[size_t(y * sw + x) * 3];
            if (y == 0) p[1] = 255;
            else if (x < sw / 2) p[0] = 255;
            else p[2] = 255;
        }
    Pixels out;
    // A square card: the picture keeps its shape, the sides are cropped evenly.
    CHECK(decode_cover(png(sw, sh, rgb), 10, 10, out));
    CHECK(out.w == 10 && out.h == 10 && out.px.size() == 100);
    if (out.px.size() == 100) {
        CHECK(r5(out.px[5 * 10 + 0]) >= 30 && b5(out.px[5 * 10 + 0]) <= 1);   // left: red
        CHECK(b5(out.px[5 * 10 + 9]) >= 30 && r5(out.px[5 * 10 + 9]) <= 1);   // right: blue
        CHECK(g6(out.px[0]) >= 20);   // the top line is still at the top (averaged over two rows)
    }
    // A tall card from a wide picture: only its middle shows, where the colours meet.
    CHECK(decode_cover(png(sw, sh, rgb), 6, 18, out));
    CHECK(out.w == 6 && out.h == 18);
    if (out.px.size() == 108) {
        CHECK(r5(out.px[9 * 6 + 0]) >= 30);
        CHECK(b5(out.px[9 * 6 + 5]) >= 30);
    }
    // A picture smaller than the card is enlarged to fill it.
    CHECK(decode_cover(png(sw, sh, rgb), 80, 40, out));
    CHECK(out.w == 80 && out.h == 40 && out.px.size() == 3200);
    if (out.px.size() == 3200) CHECK(r5(out.px[20 * 80 + 10]) >= 30 && b5(out.px[20 * 80 + 70]) >= 30);

    // A JPEG.
    std::string jpeg(reinterpret_cast<const char*>(kJpeg), sizeof kJpeg);
    CHECK(decode_cover(jpeg, 8, 8, out));
    CHECK(out.w == 8 && out.h == 8);
    if (out.px.size() == 64) CHECK(r5(out.px[27]) >= 8 && r5(out.px[27]) <= 24);   // grey, not black or white

    // Not pictures: an error page, a picture cut short, nothing, a size that makes no sense.
    CHECK(!decode_cover("<html>404</html>", 10, 10, out));
    CHECK(!decode_cover(png(sw, sh, rgb).substr(0, 60), 10, 10, out));
    CHECK(!decode_cover(jpeg.substr(0, 100), 10, 10, out));
    CHECK(!decode_cover("", 10, 10, out));
    CHECK(!decode_cover(png(sw, sh, rgb), 0, 10, out));
    // A header that claims a picture too large to decode.
    std::string huge("\x89PNG\r\n\x1a\n", 8), head;
    be32(head, 60000);
    be32(head, 60000);
    head.append("\x08\x02\x00\x00\x00", 5);
    chunk(huge, "IHDR", head);
    CHECK(!decode_cover(huge, 10, 10, out));
}

static void test_cache() {
    Pic pic{"0123456789abcdef0123456789abcdef", "Primary", "tag99"};
    CHECK(cache_name(pic, 46, 69) == "0123456789abcdef0123456789abcdef-p-46x69-tag99.565");
    pic.type = "Backdrop";
    pic.tag = "we/ird..tag\n";
    // Only plain characters reach a file name.
    CHECK(cache_name(pic, 100, 56) == "0123456789abcdef0123456789abcdef-b-100x56-weirdtag.565");
    pic.tag.clear();
    CHECK(cache_name(pic, 100, 56) == "0123456789abcdef0123456789abcdef-b-100x56.565");
    pic.id = "../../etc/passwd";
    CHECK(cache_name(pic, 100, 56).empty());
    CHECK(cache_name(Pic{}, 10, 10).empty());

    Pixels p;
    p.w = 3;
    p.h = 2;
    p.px = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x1234};
    std::string file = cache_encode(p);
    Pixels back;
    CHECK(cache_decode(file, 3, 2, back));
    CHECK(back.w == 3 && back.h == 2 && back.px == p.px);
    // Another size, a file cut short, somebody else's file: not ours.
    CHECK(!cache_decode(file, 2, 3, back));
    CHECK(!cache_decode(file.substr(0, file.size() - 1), 3, 2, back));
    CHECK(!cache_decode(file + "x", 3, 2, back));
    CHECK(!cache_decode("GIF89a........", 3, 2, back));
    CHECK(!cache_decode("", 3, 2, back));
}

static void test_demo() {
    Pixels a, b, c;
    demo_picture("Bench Tapes Vol. 2", Kind::Album, 48, 48, a);
    demo_picture("Bench Tapes Vol. 2", Kind::Album, 48, 48, b);
    demo_picture("Night Drive", Kind::Album, 48, 48, c);
    CHECK(a.w == 48 && a.h == 48 && a.px.size() == 48 * 48);
    CHECK(a.px == b.px);   // the same every time
    CHECK(a.px != c.px);   // and its own
    demo_picture("Sintel", Kind::Movie, 46, 69, a);
    CHECK(a.w == 46 && a.h == 69 && a.px.size() == 46 * 69);
    demo_picture("x", Kind::Folder, 0, 10, a);
    CHECK(a.px.empty());

    // One of five colours, the same for a name every time.
    CHECK(tint_for("Albums") == tint_for("Albums"));
    bool differ = false;
    for (const char* n : {"a", "b", "c", "d", "e", "f", "g", "h"})
        if (tint_for(n) != tint_for("Albums")) differ = true;
    CHECK(differ);
}

int main() {
    test_cover();
    test_cache();
    test_demo();
    if (failures) return 1;
    std::printf("ok   jellyfin_art_test\n");
    return 0;
}
