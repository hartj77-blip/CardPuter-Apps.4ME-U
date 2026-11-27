// Drawing a .zmap map (zmap.h) into a block of RGB565 pixels. No LVGL in
// here, so it runs in the unit tests as it does on the device; maps.cpp
// puts the pixels on screen and draws the labels this hands back.
#pragma once

#include <cstdint>
#include <vector>

#include "apps/maps/zmap.h"

namespace zmap {

constexpr uint16_t rgb565(uint32_t rgb) {
    return uint16_t(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

struct Theme {
    uint32_t outside, land, sea, forest, park, wetland, sand, airport, water;
    uint32_t stream, boundary, ferry, rail, runway;
    uint32_t road[7], casing[7];   // kService .. kMotorway
    uint32_t text, halo, shield, shield_text, motorway_shield;
    uint32_t marker, marker_edge;
    uint32_t route, route_edge;
};
const Theme& day_theme();
const Theme& night_theme();

// What is on screen: the world point (cx, cy) is drawn at pixel (px, py),
// at mpp metres a pixel, with the compass direction `up` (degrees) at the
// top: 0 is north up.
struct View {
    double cx = 0, cy = 0;
    double mpp = 10;
    double up = 0;
    int w = 320, h = 136;
    int px = 160, py = 68;
};

struct Rect {
    int x0, y0, x1, y1;   // x1 and y1 are just past the end
};

// A name to draw, centred on (x, y).
struct Label {
    int x, y;
    const char* text;
    uint8_t kind;      // a road or place Kind
    bool shield;       // a road number, drawn in a box
};

struct RoadHit {
    bool found = false;
    const char* name = "";
    const char* ref = "";
    uint8_t speed = 0;   // as in FeatureHead
    uint8_t kind = 0;
    double metres = 0;   // how far the road is from the point
};

class Renderer {
public:
    // Draw the view into w * h pixels. `labels` gets the names that fit,
    // most important first; nothing is put inside `keep_out`.
    void draw(const Map& map, const View& v, const Theme& t, uint16_t* pixels, std::vector<Label>& labels,
              const std::vector<Rect>& keep_out = {});
    // The arrow that shows where you are, pointing `screen_deg` clockwise
    // from the top of the screen; a dot if `moving` is false.
    void marker(const View& v, const Theme& t, uint16_t* pixels, double x, double y, double screen_deg, bool moving);
    // A route: a thick line through world points (x, y, x, y...).
    void route(const View& v, const Theme& t, uint16_t* pixels, const std::vector<double>& xy, size_t from_point);
    // A turn arrow (nav_parse.h's Turn) centred at (x, y), `size` pixels across.
    void turn_icon(uint16_t* pixels, int w, int h, int kind, float x, float y, float size, uint16_t colour);
    // A filled box, for panels drawn into the map.
    void box(uint16_t* pixels, int w, int h, int x0, int y0, int x1, int y1, uint16_t colour);
    // A small compass at (x, y): the red end points north.
    void compass(const View& v, const Theme& t, uint16_t* pixels, int x, int y);
    // Where a world point lands on screen.
    static void to_screen(const View& v, double wx, double wy, double& sx, double& sy);
    static void to_world(const View& v, double sx, double sy, double& wx, double& wy);
    // The level drawn at this scale.
    static int level_for(const Map& map, double mpp);
    // How wide a road is drawn, in pixels. 0: not at this scale.
    static double road_width(uint8_t kind, double mpp);
    // Features drawn by the last draw(), for the tests and the status line.
    int drawn() const { return drawn_; }

private:
    struct Item {
        Feature f;
        float m00, m01, m10, m11, bx, by;   // local -> screen
    };
    void fill(const Item& it, uint16_t colour, bool with_tile_rect);
    void lines(const Item& it, uint16_t colour, float width, float dash_on, float dash_off);
    void segment(float x0, float y0, float x1, float y1, float r, uint16_t colour);
    void poly(const float* xy, const uint32_t* counts, int nparts, uint16_t colour);

    uint16_t* px_ = nullptr;
    int w_ = 0, h_ = 0, drawn_ = 0;
    std::vector<Item> items_;
    std::vector<float> pts_;
    std::vector<uint32_t> counts_;
    std::vector<std::vector<float>> rows_;
};

// The road nearest a world point, within max_m metres. With a heading
// (degrees, or < 0 for none), roads running that way are preferred, so a
// side street at a junction doesn't win over the road you are driving on.
RoadHit nearest_road(const Map& map, double x, double y, double heading, double max_m);

// Metres between two world points close to each other.
double metres_between(double x0, double y0, double x1, double y1);

}  // namespace zmap
