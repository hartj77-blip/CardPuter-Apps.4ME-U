// Pictures for the Browser: PNG, JPEG, GIF (the first frame) and BMP bytes
// decoded with stb_image, shrunk to fit the screen and flattened onto the
// page colour as RGB565, the screen's own format. No LVGL in here, so it is
// unit tested on any machine (tests/page_test.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs::web {

struct Picture {
    int w = 0, h = 0;
    std::vector<uint16_t> pixels;   // RGB565, w * h, row by row
};

// Decode `bytes` and fit it in max_w x max_h (never enlarged). want_w and
// want_h are the size the page asks for (width= height=, 0 if not given).
// Transparent parts take the colour bg (0xRRGGBB). False if it isn't a
// picture this can read, is too big to decode, or is a tiny icon or tracker.
bool decode_picture(const std::string& bytes, int want_w, int want_h, int max_w, int max_h, uint32_t bg,
                    Picture& out);

// The bytes of a data: URI ("data:image/png;base64,...."); false if it isn't one.
bool data_uri(const std::string& uri, std::string& bytes);

}  // namespace zs::web
