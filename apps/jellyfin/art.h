// Cover art for the Jellyfin app: JPEG or PNG bytes from the server turned
// into exactly the pixels a card shows, in RGB565, the screen's own format;
// the small files those pixels are kept in between visits; and the pictures
// the simulator draws, which has no server to ask.
//
// No LVGL in here, so it is unit tested on any machine (tests/art_test.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "apps/jellyfin/api.h"

namespace zs::jfapp {

struct Pixels {
    int w = 0, h = 0;
    std::vector<uint16_t> px;   // RGB565, w * h, row by row
};

// Decodes bytes (JPEG or PNG) and scales the picture to cover w x h
// exactly: it keeps its shape and what hangs over the edges is cropped
// evenly, as a card in Jellyfin's own apps does. False if the bytes aren't
// a picture this can read, or it is too large to decode.
bool decode_cover(const std::string& bytes, int w, int h, Pixels& out);

// The same from RGB bytes already decoded (3 per pixel).
void cover_from_rgb(const uint8_t* rgb, int sw, int sh, int w, int h, Pixels& out);

// A picture made up from a name, the same every time: for the simulator.
// kind picks what is drawn, so an album doesn't look like a film.
void demo_picture(const std::string& name, Kind kind, int w, int h, Pixels& out);

// The file a picture at a size is kept in: letters, digits, '-' and '.'
// only, whatever the server sent. "" for no picture.
std::string cache_name(const Pic& pic, int w, int h);
// The contents of that file, and reading them back (false if it isn't one
// of ours, or not w x h).
std::string cache_encode(const Pixels& p);
bool cache_decode(const std::string& bytes, int w, int h, Pixels& out);

// The colour of a card with no picture, from its name: one of Jellyfin's
// five, darkened to sit on the app's black.
uint32_t tint_for(const std::string& name);

}  // namespace zs::jfapp
