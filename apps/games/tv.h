// Libretro on a TV or monitor: the HDMI port, driven directly with kernel
// modesetting (DRM/KMS dumb buffers), no GPU and no desktop needed.
//
// The Cardputer Zero has two DRM cards: the built-in LCD (mipi-dbi, which
// ZeroShell draws to) and vc4 with the HDMI connector. While a game is on the
// TV, Libretro holds vc4 (it becomes DRM master by being the only one that has
// it open; Dock mode's labwc must not be running) and page-flips between two
// RGB565 buffers at the monitor's 720p mode (or its preferred mode up to
// 1080p). The game's picture is scaled up by a whole number where that fills
// most of the screen (sharp pixels), else to fit; the bars stay black.
//
// The menu and notes are LVGL objects on the built-in screen: overlay()
// copies a snapshot of one, scaled up, over the middle (or bottom) of the TV.
//
// Only one thread draws at a time (a mutex inside): show() on the emulator
// thread, overlay() on the UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "apps/games/libretro_api.h"

namespace zs::games {

// Where a w x h picture goes on a mode_w x mode_h TV: a whole-number scale
// of its height when that's at least 2x (sharp pixels), else as tall as the
// screen; as wide as its shape (aspect, <= 0 for square pixels) says.
struct TvRect {
    int x = 0, y = 0, w = 0, h = 0;
};
TvRect tv_picture_rect(int mode_w, int mode_h, int w, int h, double aspect);

class Tv {
public:
    ~Tv() { close(); }

    // A monitor is on the HDMI port now (sysfs; cheap, call it once a second).
    static bool plugged();
    // Take the HDMI output. False, with why, if it can't (no DRM here, or
    // something else such as Dock mode has it).
    bool open(std::string& error);
    void close();
    bool is_open() const { return fd_ >= 0; }
    int width() const { return mode_w_; }
    int height() const { return mode_h_; }

    // A new picture from the core: w x h in fmt, pitch bytes a row. aspect is
    // its shape on a TV (the core's aspect ratio), <= 0 for square pixels.
    void show(const void* data, unsigned w, unsigned h, size_t pitch, retro::PixelFormat fmt, double aspect);
    // Draw the last picture again, with the overlays (the game is paused).
    void redraw();

    enum class Place { Middle, Bottom };
    // Put w x h RGB565 pixels (stride in pixels) over the picture, scaled up
    // for the TV; nullptr / 0 size removes it. Shown from the next picture,
    // or now with redraw().
    void overlay(Place where, const uint16_t* pix, int w, int h, int stride);

private:
    struct Buf {
        uint32_t handle = 0, fb = 0, pitch = 0;
        size_t size = 0;
        uint8_t* map = nullptr;
        int x = 0, y = 0, w = 0, h = 0;    // where the picture was drawn (the rest is black)
        bool dirty_bars = false;           // an overlay went past the picture: clear it all next time
    };
    bool make_buffer(Buf& b);
    void free_buffer(Buf& b);
    void compose();                        // last_ + overlays into the back buffer, then flip (mu_ held)
    void wait_flip(int timeout_ms);

    std::mutex mu_;
    int fd_ = -1;
    uint32_t crtc_ = 0, connector_ = 0;
    int mode_w_ = 0, mode_h_ = 0;
    Buf buf_[2];
    int front_ = 0;
    bool flip_pending_ = false;

    std::vector<uint16_t> last_;           // the newest picture, RGB565 at the core's size
    int last_w_ = 0, last_h_ = 0;
    double aspect_ = 0;
    std::vector<uint16_t> line_;           // one scaled row, built in normal memory

    struct Overlay {
        std::vector<uint16_t> pix;
        int w = 0, h = 0;
    };
    Overlay over_[2];
};

}  // namespace zs::games
