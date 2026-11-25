// See wsz.h. The zip's deflated entries are inflated by stb_image's zlib
// decoder (public domain, apps/games/third_party); the bitmaps are read here,
// since skins use the RLE and 1- and 4-bit kinds stb_image leaves out.
#include "wsz.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#define STBI_NO_STDIO
#define STBI_ONLY_BMP          // the least stb_image builds; only its zlib is used
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "apps/games/third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace zs::media::wsz {
namespace {

// No skin's file comes near this; it keeps a broken or hostile zip from
// filling the memory.
constexpr uint32_t kMaxFile = 4u << 20;
constexpr size_t kMaxSkin = 16u << 20;   // a .wsz on disk
constexpr int kMaxSide = 2048;           // a bitmap's width or height

uint32_t u16(const std::string& s, size_t at) {
    return at + 2 <= s.size() ? uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 : 0;
}
uint32_t u32(const std::string& s, size_t at) { return at + 4 <= s.size() ? u16(s, at) | u16(s, at + 2) << 16 : 0; }

// "Skin/Main.BMP" -> "main.bmp".
std::string key_of(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    name = name.substr(name.rfind('/') + 1);
    for (auto& c : name) c = char(std::tolower((unsigned char)c));
    return name;
}

bool fail(std::string* why, const char* w) {
    if (why) *why = w;
    return false;
}

bool read_file(const std::string& path, std::string& out, size_t max) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || size_t(n) > max) return false;
    out.resize(size_t(n));
    f.seekg(0);
    return bool(f.read(out.data(), n)) || n == 0;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

}  // namespace

bool unzip(const std::string& z, Files& out, std::string* why) {
    // The end-of-directory record, in the last 64 KiB (it may carry a comment).
    if (z.size() < 22) return fail(why, "not a zip file");
    size_t eocd = std::string::npos;
    size_t lo = z.size() > 65557 ? z.size() - 65557 : 0;
    for (size_t i = z.size() - 22 + 1; i-- > lo;)
        if (u32(z, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) return fail(why, "not a zip file");
    uint32_t count = u16(z, eocd + 10), at = u32(z, eocd + 16);
    for (uint32_t n = 0; n < count; ++n) {
        if (u32(z, at) != 0x02014b50) return fail(why, "the zip is damaged");
        uint32_t flags = u16(z, at + 8), method = u16(z, at + 10);
        uint32_t csize = u32(z, at + 20), usize = u32(z, at + 24);
        uint32_t nlen = u16(z, at + 28), xlen = u16(z, at + 30), clen = u16(z, at + 32);
        uint32_t local = u32(z, at + 42);
        if (size_t(at) + 46 + nlen > z.size()) return fail(why, "the zip is damaged");
        std::string name = z.substr(at + 46, nlen);
        at += 46 + nlen + xlen + clen;

        std::string key = key_of(name);
        if (key.empty() || out.count(key)) continue;   // a folder; or a name seen already
        if (flags & 1) return fail(why, "the zip has a password");
        if (usize > kMaxFile) continue;                // nothing a skin needs
        if (u32(z, local) != 0x04034b50) return fail(why, "the zip is damaged");
        size_t data = size_t(local) + 30 + u16(z, local + 26) + u16(z, local + 28);
        if (data + csize > z.size()) return fail(why, "the zip is cut short");
        if (method == 0) {
            out[key] = z.substr(data, csize);
        } else if (method == 8) {
            std::string u(usize, '\0');
            int got = usize ? stbi_zlib_decode_noheader_buffer(u.data(), int(usize), z.data() + data, int(csize)) : 0;
            if (got < 0) return fail(why, "the zip is damaged");
            u.resize(size_t(got));
            out[key] = std::move(u);
        }
        // Other ways of packing (bzip2, LZMA...) aren't used for skins.
    }
    return true;
}

bool load_files(const std::string& path, Files& out, std::string* why) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return fail(why, "it isn't there");
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return fail(why, "can't open the folder");
        while (dirent* e = readdir(d)) {
            std::string k = key_of(e->d_name);
            if (k.empty() || k[0] == '.') continue;
            std::string data;
            if (read_file(path + "/" + e->d_name, data, kMaxFile)) out[k] = std::move(data);
        }
        closedir(d);
        return true;
    }
    std::string z;
    if (!read_file(path, z, kMaxSkin)) return fail(why, "can't read it");
    return unzip(z, out, why);
}

Image decode_bmp(const std::string& s) {
    Image img;
    if (s.size() < 26 || s[0] != 'B' || s[1] != 'M') return img;
    uint32_t off = u32(s, 10), hs = u32(s, 14);
    int w, h, bpp;
    uint32_t comp = 0, used = 0;
    uint32_t mask[3] = {0, 0, 0};
    size_t pal_at = 14 + hs, pal_entry = 4;
    if (hs == 12) {   // OS/2's header
        w = int(u16(s, 18));
        h = int(int16_t(u16(s, 20)));
        bpp = int(u16(s, 24));
        pal_entry = 3;
    } else if (hs >= 40) {
        w = int(int32_t(u32(s, 18)));
        h = int(int32_t(u32(s, 22)));
        bpp = int(u16(s, 28));
        comp = u32(s, 30);
        used = u32(s, 46);
        if (comp == 3 || comp == 6) {
            // Masks in the header (52 bytes or more), else straight after it.
            size_t m = hs >= 52 ? 54 : 14 + hs;
            for (int i = 0; i < 3; ++i) mask[i] = u32(s, m + size_t(i) * 4);
            if (hs < 52) pal_at += 12;
        }
    } else {
        return img;
    }
    bool top_down = h < 0;
    h = std::abs(h);
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) return img;

    std::vector<uint32_t> pal;
    if (bpp <= 8) {
        size_t n = used ? std::min<size_t>(used, 256) : size_t(1) << bpp;
        for (size_t i = 0; i < n; ++i) {
            size_t p = pal_at + i * pal_entry;
            if (p + 3 > s.size()) break;
            pal.push_back(uint32_t(uint8_t(s[p + 2])) << 16 | uint32_t(uint8_t(s[p + 1])) << 8 | uint8_t(s[p]));
        }
        if (pal.empty()) return img;
    }
    auto color = [&](uint32_t i) { return i < pal.size() ? pal[i] : 0u; };

    img.w = w;
    img.h = h;
    img.px.assign(size_t(w) * size_t(h), 0);
    // Rows in the file go bottom-up unless the height is negative.
    auto put = [&](int x, int row, uint32_t c) {
        if (x < 0 || x >= w || row < 0 || row >= h) return;
        int y = top_down ? row : h - 1 - row;
        img.px[size_t(y) * size_t(w) + size_t(x)] = c;
    };

    if (comp == 1 || comp == 2) {   // RLE8, RLE4
        bool four = comp == 2;
        size_t p = off;
        int x = 0, row = 0;
        while (p + 1 < s.size() && row < h) {
            uint8_t a = uint8_t(s[p]), b = uint8_t(s[p + 1]);
            p += 2;
            if (a) {   // a run of a pixels
                for (int i = 0; i < a; ++i) put(x++, row, color(four ? (i & 1 ? b & 15 : b >> 4) : b));
            } else if (b == 0) {   // end of the line
                x = 0;
                ++row;
            } else if (b == 1) {   // end of the picture
                break;
            } else if (b == 2) {   // move
                if (p + 1 >= s.size()) break;
                x += uint8_t(s[p]);
                row += uint8_t(s[p + 1]);
                p += 2;
            } else {   // b pixels as they are, padded to 16 bits
                size_t bytes = four ? (size_t(b) + 1) / 2 : b;
                for (int i = 0; i < b && p + size_t(four ? i / 2 : i) < s.size(); ++i) {
                    uint8_t v = uint8_t(s[p + size_t(four ? i / 2 : i)]);
                    put(x++, row, color(four ? (i & 1 ? v & 15 : v >> 4) : v));
                }
                p += (bytes + 1) & ~size_t(1);
            }
        }
        return img;
    }
    if (comp != 0 && comp != 3 && comp != 6) return Image{};   // JPEG or PNG inside: not in skins

    size_t stride = ((size_t(w) * size_t(bpp) + 31) / 32) * 4;
    if (bpp == 16 && comp == 0) mask[0] = 0x7c00, mask[1] = 0x03e0, mask[2] = 0x001f;
    if (bpp == 32 && comp == 0) mask[0] = 0xff0000, mask[1] = 0x00ff00, mask[2] = 0x0000ff;
    auto chan = [](uint32_t v, uint32_t m) -> uint32_t {
        if (!m) return 0;
        int sh = 0;
        while (!(m >> sh & 1)) ++sh;
        uint32_t max = m >> sh;
        return ((v & m) >> sh) * 255u / max;
    };
    for (int row = 0; row < h; ++row) {
        size_t r = off + size_t(row) * stride;
        if (r + stride > s.size()) break;   // cut short: the rest stays black
        for (int x = 0; x < w; ++x) {
            uint32_t c = 0;
            switch (bpp) {
                case 1: c = color(uint8_t(s[r + size_t(x) / 8]) >> (7 - x % 8) & 1); break;
                case 4: c = color(uint8_t(s[r + size_t(x) / 2]) >> (x & 1 ? 0 : 4) & 15); break;
                case 8: c = color(uint8_t(s[r + size_t(x)])); break;
                case 24: {
                    size_t q = r + size_t(x) * 3;
                    c = uint32_t(uint8_t(s[q + 2])) << 16 | uint32_t(uint8_t(s[q + 1])) << 8 | uint8_t(s[q]);
                    break;
                }
                case 16:
                case 32: {
                    uint32_t v = bpp == 16 ? u16(s, r + size_t(x) * 2) : u32(s, r + size_t(x) * 4);
                    c = chan(v, mask[0]) << 16 | chan(v, mask[1]) << 8 | chan(v, mask[2]);
                    break;
                }
                default: return Image{};
            }
            put(x, row, c);
        }
    }
    return img;
}

std::vector<uint32_t> viscolors(const std::string& text) {
    // Winamp's own, for a skin without the file.
    std::vector<uint32_t> c = {
        0x000000, 0x182129, 0xef3110, 0xce2910, 0xd65a00, 0xd66600, 0xd67300, 0xc67b08,
        0xdea518, 0xd6b521, 0xbdde29, 0x94de21, 0x29ce10, 0x32be10, 0x39b510, 0x319c08,
        0x299400, 0x188408, 0xffffff, 0xd6d6de, 0xb5bdbd, 0xa0aaaf, 0x949ca5, 0x969696,
    };
    std::istringstream in(text);
    std::string line;
    size_t i = 0;
    while (i < c.size() && std::getline(in, line)) {
        int v[3], n = 0;
        const char* p = line.c_str();
        while (*p && n < 3) {
            if (*p == '/') break;   // a comment
            if (std::isdigit((unsigned char)*p)) {
                char* end;
                v[n++] = int(std::strtol(p, &end, 10));
                p = end;
            } else {
                ++p;
            }
        }
        if (n < 3) continue;   // blank, or a comment only
        auto b = [](int x) { return uint32_t(std::clamp(x, 0, 255)); };
        c[i++] = b(v[0]) << 16 | b(v[1]) << 8 | b(v[2]);
    }
    return c;
}

PlColors pledit_colors(const std::string& text) {
    PlColors pc;
    std::istringstream in(text);
    std::string line;
    bool in_text = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            std::string sec = line;
            for (auto& ch : sec) ch = char(std::tolower((unsigned char)ch));
            in_text = sec.rfind("[text]", 0) == 0;
            continue;
        }
        auto eq = line.find('=');
        if (!in_text || eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        for (auto& ch : k) ch = char(std::tolower((unsigned char)ch));
        if (!v.empty() && v[0] == '#') v = v.substr(1);
        if (v.size() < 6 || !std::all_of(v.begin(), v.begin() + 6, [](char ch) { return std::isxdigit((unsigned char)ch); }))
            continue;
        uint32_t c = uint32_t(std::strtoul(v.substr(0, 6).c_str(), nullptr, 16));
        if (k == "normal") pc.normal = c;
        else if (k == "current") pc.current = c;
        else if (k == "normalbg") pc.normal_bg = c;
        else if (k == "selectedbg") pc.selected_bg = c;
    }
    return pc;
}

}  // namespace zs::media::wsz
