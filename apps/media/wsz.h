// Reading a classic Winamp skin (README.md, "Winamp skins"): a .wsz file,
// which is a zip of Windows bitmaps and a few text files, or the same files
// unpacked in a folder. No LVGL and no screen here, so it is tested on its
// own (tests/wsz_test.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zs::media::wsz {

// A picture, 0xRRGGBB per pixel, the top row first.
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool ok() const { return w > 0 && h > 0; }
    uint32_t at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};

// The files of a skin, by their names in lower case without folders
// ("main.bmp"), as Winamp finds them whatever their case.
using Files = std::map<std::string, std::string>;

// The files in a zip held in memory: stored and deflated entries (the two
// kinds skins use). False, with a reason, if it isn't a zip we can read.
bool unzip(const std::string& zip, Files& out, std::string* why = nullptr);

// A .wsz file, or a folder holding the skin's files.
bool load_files(const std::string& path, Files& out, std::string* why = nullptr);

// A Windows bitmap: 1, 4, 8, 16, 24 and 32 bits, RLE4 and RLE8 too.
Image decode_bmp(const std::string& data);

// viscolor.txt: the visualiser's 24 colours (0 background, 1 its dots,
// 2-17 the analyser from the top down, 18-22 the oscilloscope, 23 the
// peaks). Lines it lacks keep Winamp's own colours.
std::vector<uint32_t> viscolors(const std::string& text);

// pledit.txt: the playlist's colours, from its [Text] section.
struct PlColors {
    uint32_t normal = 0x00ff00, current = 0xffffff, normal_bg = 0x000000, selected_bg = 0x0000ff;
};
PlColors pledit_colors(const std::string& text);

}  // namespace zs::media::wsz
