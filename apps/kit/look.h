// The UI 3 "Monochrome" look for the apps (docs/UX.md in the OS repo, "Look:
// Monochrome"): black, white and greys; the item in focus is lit white with
// black text; pop-ups are lifted sheets with a soft grey gradient; square
// corners. Colour only says what something is (on, update ready, in progress,
// failed) or belongs to content (pictures, maps, a radio spectrum).
//
// The values are the OS's ui/theme.h from UI 3 step 1. They live here too
// because an app is compiled against whichever OS headers its build uses
// (README.md, "App levels"), and the look should not depend on that. When the
// OS's step 5 drops the UI 2 names, this header goes and the apps use theme.h.
//
// Header-only and inline: it calls LVGL and nothing new in ZeroShell, so it
// doesn't change the app level.
//
// Use: include it after ui/theme.h in a file whose code sits in
//
//     namespace zs { namespace { using namespace theme; ... } }
//
// and the role names there (kAccent, kSurface, kMuted, style_panel ...) are
// these, not theme.h's: the using-declarations at the bottom put them in the
// file's unnamed namespace, which is looked in before `using namespace theme`.
// Code in any other namespace writes look::kFocus and so on.
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/theme.h"

namespace zs::kit::look {

// The grey ramp. Every grey is one of these.
constexpr uint32_t kGrey0 = 0x000000;   // the screen
constexpr uint32_t kGrey1 = 0x0d0d0d;   // sheets
constexpr uint32_t kGrey2 = 0x171717;   // rows, cards
constexpr uint32_t kGrey3 = 0x242424;   // dividers, raised controls
constexpr uint32_t kGrey4 = 0x3a3a3a;   // borders
constexpr uint32_t kGrey5 = 0x6b6b6b;   // disabled
constexpr uint32_t kGrey6 = 0xa3a3a3;   // secondary text
constexpr uint32_t kGrey7 = 0xd4d4d4;   // the bottom of the focus gradient
constexpr uint32_t kGrey8 = 0xffffff;   // text, focus

// Roles. Screens use these, not the ramp.
constexpr uint32_t kBg = kGrey0;
constexpr uint32_t kSurface = kGrey2;
constexpr uint32_t kSurface2 = kGrey3;
constexpr uint32_t kBorder = kGrey4;
constexpr uint32_t kHairline = kGrey3;
constexpr uint32_t kText = kGrey8;
constexpr uint32_t kMuted = kGrey6;
constexpr uint32_t kDisabled = kGrey5;
constexpr uint32_t kFocus = kGrey8;       // the lit item: top of its gradient
constexpr uint32_t kFocusLow = kGrey7;    // ... and its bottom
constexpr uint32_t kOnFocus = kGrey0;     // text on the lit item
constexpr uint32_t kRaise = 0x232323;     // pop-ups: top of their gradient
constexpr uint32_t kRaiseLow = 0x0a0a0a;  // ... and its bottom

// State. Only for what something is, never decoration.
constexpr uint32_t kGood = 0x3fb950;   // on, connected, installed
constexpr uint32_t kWarn = 0xf0a020;   // update ready, low battery
constexpr uint32_t kBad = 0xe5484d;    // destructive, error, recording
constexpr uint32_t kInfo = 0x3b9eff;   // in progress, searching

// The UI 2 names. The accent is white now: titles, cursors, the focused
// button; anything on it is black.
constexpr uint32_t kAccent = kFocus;
constexpr uint32_t kOnAccent = kOnFocus;
constexpr uint32_t kTopBar = kGrey0;
constexpr uint32_t kTerminalBg = kGrey0;

inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// The lit white gradient of the item in focus, on `selector` (a state such as
// LV_STATE_FOCUSED). Sets the text black too.
inline void focus_fill(lv_obj_t* o, lv_style_selector_t selector) {
    lv_obj_set_style_bg_color(o, col(kFocus), selector);
    lv_obj_set_style_bg_grad_color(o, col(kFocusLow), selector);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, selector);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, selector);
    lv_obj_set_style_text_color(o, col(kOnFocus), selector);
}

// A button on an app screen: a square raised control, lit when focused, no
// focus ring (the light is the focus).
inline void button(lv_obj_t* b) {
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, col(kSurface2), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, col(kBorder), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_text_color(b, col(kText), 0);
    focus_fill(b, LV_STATE_FOCUSED);
    focus_fill(b, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUS_KEY);
}

// A lifted sheet: a pop-up, a message over a picture, a toast. Grey gradient,
// a 1 px border, a drop shadow. Square.
inline void raised(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, col(kRaise), 0);
    lv_obj_set_style_bg_grad_color(o, col(kRaiseLow), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, col(0x404040), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_color(o, col(kGrey0), 0);
    lv_obj_set_style_shadow_width(o, 14, 0);
    lv_obj_set_style_shadow_offset_y(o, 4, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_80, 0);
}

// A text box: a dark well with a grey border that turns white while typing.
inline void field(lv_obj_t* ta) {
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_bg_color(ta, col(kGrey1), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, col(kText), 0);
    lv_obj_set_style_border_color(ta, col(kBorder), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, col(kFocus), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(ta, col(kFocus), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(ta, col(kMuted), LV_PART_TEXTAREA_PLACEHOLDER);
}

// A card: theme::style_panel with this look's surface and square corners.
inline void style_panel(lv_obj_t* o) {
    theme::style_panel(o);
    lv_obj_set_style_bg_color(o, col(kSurface), 0);
    lv_obj_set_style_radius(o, 0, 0);
}

}  // namespace zs::kit::look

namespace zs {
namespace {
// See the top: these hide theme.h's names in code that says
// `using namespace theme` inside zs's unnamed namespace.
namespace look = kit::look;
using look::kAccent;
using look::kBg;
using look::kBorder;
using look::kDisabled;
using look::kFocus;
using look::kFocusLow;
using look::kGood;
using look::kGrey0;
using look::kGrey1;
using look::kGrey2;
using look::kGrey3;
using look::kGrey4;
using look::kGrey5;
using look::kGrey6;
using look::kGrey7;
using look::kGrey8;
using look::kHairline;
using look::kInfo;
using look::kMuted;
using look::kOnAccent;
using look::kOnFocus;
using look::kRaise;
using look::kRaiseLow;
using look::kSurface;
using look::kSurface2;
using look::kTerminalBg;
using look::kText;
using look::kTopBar;
using look::kWarn;
using look::kBad;
using look::style_panel;
}  // namespace
}  // namespace zs
