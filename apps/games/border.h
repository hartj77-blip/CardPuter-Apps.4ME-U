// Borders: what fills the screen around the game's picture. A Game Boy
// picture (160 x 144) leaves 80 pixels empty on each side of the 320 x 170
// screen, an NES one about 45; this draws something there instead of black.
//
//   Off    black, as before
//   Frame  a console-coloured case around a recessed screen, a power light
//          and speaker slots on the handhelds, trim lines on the others
//   Glow   the colours at the picture's edges, spread out and dimmed into
//          the bars, like a TV with ambient light (follows the game)
//   Image  a ready-made border picture (PNG) from ~/Games/borders: your own,
//          a Super Game Boy border, or a bezel from a pack such as libretro's
//          overlay-borders. If it has a see-through window in the middle, it
//          is scaled so the window sits on the game's picture.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs::games {

enum class Border { Off, Frame, Glow, Image };

const char* border_name(Border b);
Border border_from_name(const std::string& s);   // unknown: Frame
Border next_border(Border b);

// Where the picture sits on the screen.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Draw the Frame border for console `system` (systems.h id: "gb", "nes"...)
// into out (sw x sh RGB565). The picture's own area is left black: the
// picture is drawn on top.
void draw_frame(uint16_t* out, int sw, int sh, Rect pic, const std::string& system);

// A border picture, decoded. hole: its see-through window (w = 0: none).
struct BorderImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    Rect hole;
    bool empty() const { return rgba.empty(); }
};

// Load a PNG border picture (at most 4096 x 4096). On failure, error says why.
bool load_border_image(const std::string& path, BorderImage& out, std::string& error);

// The border picture for a game, in dir (~/Games/borders): "<game>.png",
// then "<system>.png", then "default.png". "" if there is none.
std::string find_border_image(const std::string& dir, const std::string& system, const std::string& game);

// Draw a border picture into out (sw x sh RGB565). With a window, the
// window is scaled to cover pic and centred on it; without one, the picture
// covers the screen. See-through parts are black, and so is pic itself.
void draw_image(uint16_t* out, int sw, int sh, Rect pic, const BorderImage& img);

// Draw the Glow border from the picture (pic.w x pic.h RGB565) into out
// (sw x sh RGB565). Only the bars around pic are written.
void draw_glow(uint16_t* out, int sw, int sh, Rect pic, const uint16_t* pixels);

}  // namespace zs::games
