// Text drawn into a skin's canvas through LVGL, in the system font: what
// the pixel-art skins (skins.cpp) and the Winamp skins (winamp.cpp) share.
#pragma once

#include <algorithm>
#include <deque>
#include <string>

#include "lvgl.h"

namespace zs::media {

class Text {
public:
    explicit Text(lv_obj_t* canvas) : canvas_(canvas) { lv_canvas_init_layer(canvas_, &layer_); }
    ~Text() { lv_canvas_finish_layer(canvas_, &layer_); }
    // Draws s with its top-left at (x, y), clipped to the box; align right
    // puts its right edge at x instead.
    void draw(const std::string& s, int x, int y, const lv_font_t* f, uint32_t rgb, lv_area_t clip, bool right = false) {
        if (s.empty()) return;
        keep_.push_back(s);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.text = keep_.back().c_str();
        d.font = f;
        d.color = lv_color_hex(rgb);
        d.align = right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
        d.flag = LV_TEXT_FLAG_EXPAND;
        lv_area_t a = right ? lv_area_t{x - 2000, y, x, y + lv_font_get_line_height(f)}
                            : lv_area_t{x, y, x + 2000, y + lv_font_get_line_height(f)};
        // The intersection by hand: LVGL renamed its helper between versions.
        lv_area_t was = layer_._clip_area;
        lv_area_t c2{std::max(was.x1, clip.x1), std::max(was.y1, clip.y1), std::min(was.x2, clip.x2), std::min(was.y2, clip.y2)};
        if (c2.x1 > c2.x2 || c2.y1 > c2.y2) return;
        layer_._clip_area = c2;
        lv_draw_label(&layer_, &d, &a);
        layer_._clip_area = was;
    }
    static int width(const std::string& s, const lv_font_t* f) { return lv_text_get_width(s.c_str(), uint32_t(s.size()), f, 0); }

private:
    lv_obj_t* canvas_;
    lv_layer_t layer_{};
    std::deque<std::string> keep_;   // the texts must outlive the layer
};
inline lv_area_t box(int x, int y, int w, int h) { return {x, y, x + w - 1, y + h - 1}; }

}  // namespace zs::media
