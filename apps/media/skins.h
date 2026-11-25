// Media's skins (README.md, "Skins"): whole-screen looks drawn pixel by
// pixel into one canvas, the way the old desktop players' skins were. Each
// has a player screen and a playlist screen. The app hands a skin a View of
// what is playing and what the explorer lists; the skin only draws.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"

namespace zs::media {

// One row of the explorer, as a skin shows it.
struct Row {
    std::string text;            // the name (a track's number and title)
    std::string right;           // the note on the right: a format, "DRIVE"...
    std::string path;            // to mark the track that is playing
    const char* icon = nullptr;  // LV_SYMBOL_* for places, folders, playlists
    bool folder = false;         // a folder or a place: drawn in its own colour
    bool note = false;           // a message, not something to open
};

struct View {
    float t = 0;                 // seconds, for what moves
    bool stopped = true, paused = false;
    int pos_ms = 0, dur_ms = -1;
    std::string title, artist, album, format, playing_path;
    int q_at = -1, q_n = 0;      // place in the queue, 0-based, and its length
    bool shuffle = false, muted = false;
    int volume = 50;             // 0..100
    int flash = -1;              // a transport button pressed just now: 0..4
    const std::vector<Row>* rows = nullptr;
    int sel = -1, top = 0;       // the selected row; the first row shown
    std::string crumb;           // where the explorer is
    bool list = false;           // the playlist screen, else the player
};

class Skin {
public:
    virtual ~Skin() = default;
    virtual const char* name() const = 0;
    // Rows the playlist screen shows at once.
    virtual int rows_shown() const = 0;
    // One frame into the canvas's RGB565 buffer (kScreenW x kBodyH).
    virtual void draw(lv_obj_t* canvas, uint16_t* buf, const View& v) = 0;
};

std::unique_ptr<Skin> make_hifi_skin();
std::unique_ptr<Skin> make_chrome_skin();
// A classic Winamp skin (winamp.cpp): a .wsz file, or a folder of its files.
// Null, with a reason, if it can't be read.
std::unique_ptr<Skin> make_winamp_skin(const std::string& path, std::string* why);

}  // namespace zs::media
