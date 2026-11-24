// See picture.h.
#include "apps/browser/picture.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
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

namespace zs::web {

namespace {

constexpr long kMaxPixels = 16L * 1024 * 1024;   // 64 MB of RGBA while decoding

int b64(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+' || ch == '-') return 62;
    if (ch == '/' || ch == '_') return 63;
    return -1;
}

}  // namespace

bool data_uri(const std::string& uri, std::string& bytes) {
    if (uri.compare(0, 5, "data:") != 0) return false;
    size_t comma = uri.find(',');
    if (comma == std::string::npos) return false;
    std::string head = uri.substr(5, comma - 5);
    bytes.clear();
    if (head.find(";base64") != std::string::npos) {
        uint32_t acc = 0;
        int bits = 0;
        for (size_t i = comma + 1; i < uri.size(); ++i) {
            int v = b64(uri[i]);
            if (v < 0) continue;   // '=', spaces, line breaks
            acc = (acc << 6) | uint32_t(v);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                bytes += char((acc >> bits) & 0xFF);
            }
        }
    } else {   // percent-encoded
        for (size_t i = comma + 1; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit((unsigned char)uri[i + 1]) &&
                std::isxdigit((unsigned char)uri[i + 2])) {
                bytes += char(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                bytes += uri[i];
            }
        }
    }
    return true;
}

bool decode_picture(const std::string& bytes, int want_w, int want_h, int max_w, int max_h, uint32_t bg,
                    Picture& out) {
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF) return false;
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    int len = int(bytes.size());
    int sw = 0, sh = 0, n = 0;
    if (!stbi_info_from_memory(data, len, &sw, &sh, &n)) return false;
    if (sw <= 0 || sh <= 0 || long(sw) * sh > kMaxPixels) return false;
    if (sw <= 16 && sh <= 16) return false;   // an icon or a tracking pixel
    stbi_uc* px = stbi_load_from_memory(data, len, &sw, &sh, &n, 4);
    if (!px) return false;

    // the size the page wants, keeping the shape when it gives one side
    double w = sw, h = sh;
    if (want_w > 0 && want_h > 0) {
        w = want_w;
        h = want_h;
    } else if (want_w > 0) {
        h = h * want_w / w;
        w = want_w;
    } else if (want_h > 0) {
        w = w * want_h / h;
        h = want_h;
    }
    double scale = std::min({1.0, max_w / w, max_h / h});
    int dw = std::max(1, int(w * scale + 0.5)), dh = std::max(1, int(h * scale + 0.5));
    // never larger than the picture itself
    if (dw > sw || dh > sh) {
        double s2 = std::min(double(sw) / dw, double(sh) / dh);
        dw = std::max(1, int(dw * s2));
        dh = std::max(1, int(dh * s2));
    }

    out.w = dw;
    out.h = dh;
    out.pixels.assign(size_t(dw) * size_t(dh), 0);
    const int br = int((bg >> 16) & 0xFF), bgc = int((bg >> 8) & 0xFF), bb = int(bg & 0xFF);
    // each pixel on screen is the average of the pixels it covers (a box filter)
    for (int y = 0; y < dh; ++y) {
        int y0 = int(long(y) * sh / dh), y1 = std::max(y0 + 1, int(long(y + 1) * sh / dh));
        for (int x = 0; x < dw; ++x) {
            int x0 = int(long(x) * sw / dw), x1 = std::max(x0 + 1, int(long(x + 1) * sw / dw));
            long r = 0, g = 0, b = 0, cnt = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const stbi_uc* p = px + (size_t(yy) * size_t(sw) + size_t(x0)) * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4) {
                    int a = p[3];
                    r += (p[0] * a + br * (255 - a)) / 255;
                    g += (p[1] * a + bgc * (255 - a)) / 255;
                    b += (p[2] * a + bb * (255 - a)) / 255;
                    ++cnt;
                }
            }
            int R = int(r / cnt), G = int(g / cnt), B = int(b / cnt);
            out.pixels[size_t(y) * size_t(dw) + size_t(x)] = uint16_t(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
        }
    }
    stbi_image_free(px);
    return true;
}

}  // namespace zs::web
