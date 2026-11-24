// Borders: what fills the screen around the game's picture. A Game Boy
// picture (160 x 144) leaves 80 pixels empty on each side of the 320 x 170
// screen, an NES one about 45; this draws something there instead of black.
//
//   Off    black, as before
//   Frame  a console-coloured case around a recessed screen, a power light
//          and speaker slots on the handhelds, trim lines on the others
//   Glow   the colours at the picture's edges, spread out and dimmed into
//          the bars, like a TV with ambient light (follows the game)
#pragma once

#include <cstdint>
#include <string>

namespace zs::games {

enum class Border { Off, Frame, Glow };

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

// Draw the Glow border from the picture (pic.w x pic.h RGB565) into out
// (sw x sh RGB565). Only the bars around pic are written.
void draw_glow(uint16_t* out, int sw, int sh, Rect pic, const uint16_t* pixels);

}  // namespace zs::games
