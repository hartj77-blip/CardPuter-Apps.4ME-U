// Players (pad.h): which gamepad plays which player, swapping, a Bluetooth
// gamepad coming back; and where the picture goes on a TV (tv.h). The
// gamepads are pipes of input_events, so no devices are needed.
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "apps/games/libretro_api.h"
#include "apps/games/pad.h"
#include "apps/games/tv.h"

using namespace zs::games;

// LVGL's clock, for Pad (its headers come with the lvgl target; the library
// itself isn't needed here).
extern "C" uint32_t lv_tick_get(void) { return 0; }
extern "C" uint32_t lv_tick_elaps(uint32_t prev) { return 0 - prev; }

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct FakeDev {
    int rd = -1, wr = -1;
    FakeDev() {
        int p[2];
        if (pipe(p) != 0) return;
        rd = p[0];
        wr = p[1];
        fcntl(rd, F_SETFL, O_NONBLOCK);
    }
    void send(uint16_t type, uint16_t code, int32_t value) {
        input_event e{};
        e.type = type;
        e.code = code;
        e.value = value;
        if (write(wr, &e, sizeof e) != ssize_t(sizeof e)) std::perror("write");
    }
    void key(uint16_t code, bool down) { send(EV_KEY, code, down ? 1 : 0); }
    void unplug() {
        if (wr >= 0) close(wr);
        wr = -1;
    }
};

uint16_t bit(unsigned b) { return uint16_t(1u << b); }

void check_rect(int mw, int mh, int w, int h, double aspect, TvRect want, const char* what) {
    TvRect r = tv_picture_rect(mw, mh, w, h, aspect);
    bool ok = r.x == want.x && r.y == want.y && r.w == want.w && r.h == want.h;
    char got[96];
    std::snprintf(got, sizeof got, " -> %d,%d %dx%d", r.x, r.y, r.w, r.h);
    check(ok, std::string(what) + got);
}
}  // namespace

int main() {
    using namespace zs::retro;
    {
        Pad pad;
        FakeDev kb, one, two;
        pad.add_device(kb.rd, false, "");
        pad.add_device(one.rd, true, "aa:aa");
        check(pad.take_joined() == 1, "the first gamepad is player 1");
        pad.add_device(two.rd, true, "bb:bb");
        check(pad.take_joined() == 2, "the second gamepad is player 2");
        check(pad.gamepads() == 2, "two gamepads");

        two.key(BTN_EAST, true);   // A on gamepad 2
        one.send(EV_ABS, ABS_HAT0X, 1);   // right on gamepad 1
        kb.key(KEY_K, true);       // A on the keyboard
        pad.poll();
        check(pad.buttons(1) == bit(kA), "player 2 holds A, and only A");
        check(pad.buttons(0) == (bit(kRight) | bit(kA)), "player 1: gamepad 1's right and the keyboard's A");
        check(pad.buttons(2) == 0 && pad.buttons(3) == 0, "players 3 and 4 hold nothing");
        check(pad.gamepad_buttons() == (bit(kRight) | bit(kA)), "gamepad buttons together (the menu)");
        check(pad.players_text() == "P1 keys+pad, P2 pad", "players text: " + pad.players_text());

        pad.swap_players();
        check(pad.buttons(0) == (bit(kA)) && pad.buttons(1) == bit(kRight), "swapped: gamepad 2 is player 1");

        two.key(BTN_EAST, false);
        one.send(EV_ABS, ABS_HAT0X, 0);
        kb.key(KEY_K, false);
        pad.poll();
        check(pad.buttons(0) == 0 && pad.buttons(1) == 0, "let go");

        // Gamepad 1 (player 2 since the swap) drops out and comes back.
        one.unplug();
        pad.poll();
        check(pad.gamepads() == 1, "a gamepad went away");
        FakeDev three;
        pad.add_device(three.rd, true, "cc:cc");
        check(pad.take_joined() == 2, "a new gamepad takes the free player 2");
        FakeDev one_again;
        pad.add_device(one_again.rd, true, "aa:aa");
        check(pad.take_joined() == 3, "the old one's player is taken: it gets player 3");
        three.unplug();
        one_again.unplug();
        pad.poll();
        FakeDev one_back;
        pad.add_device(one_back.rd, true, "aa:aa");
        check(pad.take_joined() == 3, "back again: it gets its last player (3)");

        two.key(BTN_THUMBR, true);
        pad.poll();
        check(pad.fast_forward(), "right stick click: fast forward");
        two.key(BTN_THUMBR, false);
        pad.poll();
        check(!pad.fast_forward(), "let go: no fast forward");
        pad.close();
    }

    // A 720p TV.
    check_rect(1280, 720, 256, 240, 4.0 / 3, {160, 0, 960, 720}, "NES at 3x, 4:3");
    check_rect(1280, 720, 160, 144, 10.0 / 9, {240, 0, 800, 720}, "Game Boy at 5x");
    check_rect(1280, 720, 240, 160, 1.5, {160, 40, 960, 640}, "GBA at 4x");
    check_rect(1280, 720, 640, 480, 4.0 / 3, {160, 0, 960, 720}, "PlayStation 480 lines: fills the height");
    check_rect(1280, 720, 320, 240, 0, {160, 0, 960, 720}, "square pixels when there's no aspect");
    check_rect(1280, 720, 320, 50, 6.4, {0, 260, 1280, 200}, "very wide: fits the width");
    check_rect(1920, 1080, 256, 224, 4.0 / 3, {362, 92, 1195, 896}, "SNES at 4x on 1080p");
    check_rect(1280, 720, 0, 0, 1, {0, 0, 0, 0}, "no picture");

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
