// Reading Winamp skins (wsz.h): zips, bitmaps of every kind skins use, and
// the colour files. cmake ... && ctest, or run media_wsz_test; with a .wsz
// as its argument it also says what it finds in that skin.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "apps/media/wsz.h"

using namespace zs::media::wsz;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void le16(std::string& s, uint32_t v) {
    s += char(v & 255);
    s += char(v >> 8 & 255);
}
static void le32(std::string& s, uint32_t v) {
    le16(s, v & 0xffff);
    le16(s, v >> 16);
}

// A bitmap with a 40-byte header: the palette and the rows as given.
static std::string bmp(int w, int h, int bpp, uint32_t comp, const std::vector<uint32_t>& pal, const std::string& rows) {
    std::string s = "BM";
    uint32_t off = 14 + 40 + uint32_t(pal.size()) * 4;
    le32(s, off + uint32_t(rows.size()));
    le32(s, 0);
    le32(s, off);
    le32(s, 40);
    le32(s, uint32_t(w));
    le32(s, uint32_t(h));
    le16(s, 1);
    le16(s, uint32_t(bpp));
    le32(s, comp);
    le32(s, uint32_t(rows.size()));
    le32(s, 2835);
    le32(s, 2835);
    le32(s, uint32_t(pal.size()));
    le32(s, 0);
    for (uint32_t c : pal) le32(s, c);   // 0x00RRGGBB is B, G, R, 0 in the file
    return s + rows;
}

struct Entry {
    std::string name, data;
    int method;            // 0 stored, 8 deflated
    uint32_t usize;
};

// A zip of the entries (their CRCs left at 0: the reader doesn't check them).
static std::string zip(const std::vector<Entry>& es) {
    std::string z, dir;
    for (auto& e : es) {
        uint32_t local = uint32_t(z.size());
        le32(z, 0x04034b50);
        le16(z, 20);
        le16(z, 0);
        le16(z, uint32_t(e.method));
        le32(z, 0);
        le32(z, 0);
        le32(z, uint32_t(e.data.size()));
        le32(z, e.usize);
        le16(z, uint32_t(e.name.size()));
        le16(z, 0);
        z += e.name + e.data;

        le32(dir, 0x02014b50);
        le16(dir, 20);
        le16(dir, 20);
        le16(dir, 0);
        le16(dir, uint32_t(e.method));
        le32(dir, 0);
        le32(dir, 0);
        le32(dir, uint32_t(e.data.size()));
        le32(dir, e.usize);
        le16(dir, uint32_t(e.name.size()));
        le16(dir, 0);
        le16(dir, 0);
        le16(dir, 0);
        le16(dir, 0);
        le32(dir, 0);
        le32(dir, local);
        dir += e.name;
    }
    uint32_t at = uint32_t(z.size());
    z += dir;
    le32(z, 0x06054b50);
    le16(z, 0);
    le16(z, 0);
    le16(z, uint32_t(es.size()));
    le16(z, uint32_t(es.size()));
    le32(z, uint32_t(dir.size()));
    le32(z, at);
    le16(z, 0);
    return z;
}

static void test_bmp() {
    // 24 bits, 2 x 2, bottom row first, rows padded to 4 bytes.
    std::string rows;
    rows += std::string("\x00\x00\xff" "\x00\xff\x00", 6) + std::string(2, '\0');   // bottom: red, green
    rows += std::string("\xff\x00\x00" "\xff\xff\xff", 6) + std::string(2, '\0');   // top: blue, white
    Image a = decode_bmp(bmp(2, 2, 24, 0, {}, rows));
    CHECK(a.w == 2 && a.h == 2);
    CHECK(a.at(0, 0) == 0x0000ff && a.at(1, 0) == 0xffffff);
    CHECK(a.at(0, 1) == 0xff0000 && a.at(1, 1) == 0x00ff00);

    // 8 bits with a palette: 3 x 1.
    Image b = decode_bmp(bmp(3, 1, 8, 0, {0x112233, 0x445566, 0x778899}, std::string("\x02\x00\x01\x00", 4)));
    CHECK(b.w == 3 && b.at(0, 0) == 0x778899 && b.at(1, 0) == 0x112233 && b.at(2, 0) == 0x445566);

    // 4 bits: 3 x 1, high nibble first.
    Image c = decode_bmp(bmp(3, 1, 4, 0, {0, 0xaa0000, 0x00bb00}, std::string("\x12\x00\x00\x00", 4)));
    CHECK(c.at(0, 0) == 0xaa0000 && c.at(1, 0) == 0x00bb00 && c.at(2, 0) == 0);

    // 1 bit: 10 x 1, the top bit first.
    Image d = decode_bmp(bmp(10, 1, 1, 0, {0x000000, 0xffffff}, std::string("\xa0\x40\x00\x00", 4)));
    CHECK(d.w == 10 && d.at(0, 0) == 0xffffff && d.at(1, 0) == 0 && d.at(2, 0) == 0xffffff && d.at(9, 0) == 0xffffff);

    // RLE8: 4 x 2. Bottom row: a run of 3 of colour 1, then 1 of colour 2;
    // top row: 4 pixels as they are (2, 1, 2, 1).
    std::string rle("\x03\x01\x01\x02\x00\x00" "\x00\x04\x02\x01\x02\x01" "\x00\x01", 14);
    Image e = decode_bmp(bmp(4, 2, 8, 1, {0, 0x0000ff, 0xff0000}, rle));
    CHECK(e.w == 4 && e.h == 2);
    CHECK(e.at(0, 1) == 0x0000ff && e.at(2, 1) == 0x0000ff && e.at(3, 1) == 0xff0000);
    CHECK(e.at(0, 0) == 0xff0000 && e.at(1, 0) == 0x0000ff && e.at(3, 0) == 0x0000ff);

    // RLE4: 4 x 1, a run of 4 alternating colours 1 and 2.
    Image f = decode_bmp(bmp(4, 1, 4, 2, {0, 0x0000ff, 0xff0000}, std::string("\x04\x12\x00\x01", 4)));
    CHECK(f.at(0, 0) == 0x0000ff && f.at(1, 0) == 0xff0000 && f.at(2, 0) == 0x0000ff && f.at(3, 0) == 0xff0000);

    // 32 bits, top-down (a negative height).
    std::string top;
    le32(top, 0x00123456);
    std::string g32 = bmp(1, 1, 32, 0, {}, top);
    g32[22] = char(0xff), g32[23] = char(0xff), g32[24] = char(0xff), g32[25] = char(0xff);   // height -1
    Image g = decode_bmp(g32);
    CHECK(g.w == 1 && g.h == 1 && g.at(0, 0) == 0x123456);

    // Not bitmaps, or cut short.
    CHECK(!decode_bmp("").ok());
    CHECK(!decode_bmp("PNG not a bitmap at all, not at all").ok());
    CHECK(!decode_bmp(bmp(4, 1, 8, 0, {}, "")).ok());   // no palette
    Image cut = decode_bmp(bmp(2, 2, 24, 0, {}, rows.substr(0, 8)));
    CHECK(cut.ok() && cut.at(0, 1) == 0xff0000 && cut.at(0, 0) == 0);   // the rows that are there
}

static void test_zip() {
    std::string pix = bmp(1, 1, 24, 0, {}, std::string("\x10\x20\x30\x00", 4));
    // "hello hello hello winamp skin " four times, deflated (zlib, raw).
    static const unsigned char deflated[] = {203, 72, 205, 201, 201, 87, 200, 64, 34, 203, 51, 243, 18, 115, 11, 20,
                                             138, 179, 51, 243, 80, 196, 169, 43, 11, 0};
    std::string want;
    for (int i = 0; i < 4; ++i) want += "hello hello hello winamp skin ";
    std::string z = zip({
        {"MySkin/", "", 0, 0},
        {"MySkin\\MAIN.BMP", pix, 0, uint32_t(pix.size())},
        {"readme.TXT", std::string((const char*)deflated, sizeof deflated), 8, uint32_t(want.size())},
    });
    Files fs;
    std::string why;
    CHECK(unzip(z, fs, &why));
    CHECK(fs.size() == 2);
    CHECK(fs.count("main.bmp") && fs["main.bmp"] == pix);
    CHECK(fs.count("readme.txt") && fs["readme.txt"] == want);
    CHECK(decode_bmp(fs["main.bmp"]).at(0, 0) == 0x302010);

    // A comment after the directory.
    std::string zc = z;
    zc[zc.size() - 2] = 5;
    zc += "hello";
    Files fc;
    CHECK(unzip(zc, fc) && fc.size() == 2);

    // Not zips, and broken ones.
    Files bad;
    CHECK(!unzip("", bad, &why) && !why.empty());
    CHECK(!unzip(std::string(200, 'x'), bad, &why));
    std::string cut = z;
    cut[cut.size() - 6] = char(0x7f);   // the directory's offset, far past the end
    CHECK(!unzip(cut, bad, &why));
    std::string broken = zip({{"text.txt", "abc", 8, 1000}});   // not deflate data
    Files fb;
    unzip(broken, fb, &why);   // either refused or not an error that crashes
    std::string locked = zip({{"a.txt", "x", 0, 1}});
    locked[6] = 1;   // the local header's "encrypted" flag doesn't matter...
    locked[locked.size() - 22 - 46 - 5 + 8] = 1;   // ...the directory's does
    Files fl;
    CHECK(!unzip(locked, fl, &why) && why.find("password") != std::string::npos);
}

static void test_folder() {
    char tmpl[] = "/tmp/wsz_test_XXXXXX";
    std::string dir = mkdtemp(tmpl);
    std::ofstream(dir + "/Main.Bmp") << "BMx";
    std::ofstream(dir + "/pledit.txt") << "[Text]\nNormal=#123456\n";
    Files fs;
    CHECK(load_files(dir, fs));
    CHECK(fs.count("main.bmp") && fs["main.bmp"] == "BMx");
    CHECK(pledit_colors(fs["pledit.txt"]).normal == 0x123456);
    std::remove((dir + "/Main.Bmp").c_str());
    std::remove((dir + "/pledit.txt").c_str());
    rmdir(dir.c_str());
    Files none;
    std::string why;
    CHECK(!load_files(dir + "/missing.wsz", none, &why) && !why.empty());
}

static void test_colours() {
    auto def = viscolors("");
    CHECK(def.size() == 24 && def[0] == 0 && def[18] == 0xffffff);
    auto v = viscolors("1,2,3, // the background\n\n// a comment\n255,0,128,\n  9 , 8 , 7\n300,-4,0\n");
    CHECK(v[0] == 0x010203 && v[1] == 0xff0080 && v[2] == 0x090807);
    CHECK(v[3] == 0xff0000 || v[3] == 0xff0400);   // clamped, the minus read as a separator
    CHECK(v[4] == def[4]);                         // lines it lacks keep Winamp's own

    auto p = pledit_colors("[Text]\r\nNormal=#00FF00\r\nCurrent= #ABCDEF\r\nnormalbg=101010\r\nSelectedBG=#zzzzzz\r\nFont=Arial\r\n");
    CHECK(p.normal == 0x00ff00 && p.current == 0xabcdef && p.normal_bg == 0x101010);
    CHECK(p.selected_bg == 0x0000ff);   // unreadable: the default stays
    auto q = pledit_colors("[Other]\nNormal=#111111\n");
    CHECK(q.normal == 0x00ff00);        // only the [Text] section counts
}

int main(int argc, char** argv) {
    test_bmp();
    test_zip();
    test_folder();
    test_colours();
    if (argc > 1) {
        Files fs;
        std::string why;
        if (!load_files(argv[1], fs, &why)) {
            std::printf("%s: %s\n", argv[1], why.c_str());
            return 1;
        }
        for (auto& [name, data] : fs) {
            Image im = name.size() > 4 && name.substr(name.size() - 4) == ".bmp" ? decode_bmp(data) : Image{};
            std::printf("%-14s %7zu bytes%s\n", name.c_str(), data.size(),
                        im.ok() ? (" " + std::to_string(im.w) + " x " + std::to_string(im.h)).c_str() : "");
        }
    }
    if (failures) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("ok   Winamp skins: zips, bitmaps, colours\n");
    return 0;
}
