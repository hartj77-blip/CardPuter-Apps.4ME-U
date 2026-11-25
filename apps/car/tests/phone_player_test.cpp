// Checks how Car reads a phone's Bluetooth music player (phone_player.h): the
// same cases as the OS's shell/tests/bt_player_test.cpp, plus the song's
// length and position. Run by ctest.
#include <cstdio>
#include <string>

#include "apps/car/phone_player.h"

using namespace zs::car_phone;

namespace {
int g_failed = 0;

void check(const std::string& got, const std::string& want, const char* what) {
    if (got == want) return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++g_failed;
}
std::string yes(bool b) { return b ? "yes" : "no"; }
}  // namespace

int main() {
    std::string tree =
        "/org/bluez/hci0\n/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0/Filesystem\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0;rm -rf /\n";
    auto p = player_paths(tree);
    check(std::to_string(p.size()), "1", "one player, nothing odd");
    check(address_of("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0"), "AA:BB:CC:DD:EE:FF", "address");

    check(yes(is_music_source("\tIcon: phone\n")), "yes", "a phone");
    check(yes(is_music_source("\tUUID: Audio Sink (0000110b-0000)\n\tIcon: audio-card\n")), "no", "a car");

    Info i = parse_info(
        "s \"playing\"\n"
        "a{sv} 4 \"Title\" s \"Caf\\xc3\\xa9\" \"Artist\" s \"John\\xe2\\x80\\x99s Band\" \"Album\" s \"LP\" "
        "\"Duration\" u 215000\n"
        "u 61000\n");
    check(i.status, "playing", "status");
    check(i.title, "Caf\xc3\xa9", "title, UTF-8");
    check(i.artist, "John\xe2\x80\x99s Band", "a curly apostrophe");
    check(std::to_string(i.duration_ms), "215000", "length");
    check(std::to_string(i.position_ms), "61000", "position");
    check(yes(i.playing()), "yes", "playing");

    Info no_pos = parse_info("s \"paused\"\na{sv} 1 \"Title\" s \"Radio\"\n");
    check(std::to_string(no_pos.duration_ms), "-1", "no length");
    check(std::to_string(no_pos.position_ms), "-1", "no position");
    check(yes(no_pos.playing()), "no", "paused");

    check(method_for(Key::PlayPause, true), "Pause", "play/pause while playing");
    check(method_for(Key::Next, false), "Next", "next");

    if (g_failed) return 1;
    std::printf("car_phone_test: ok\n");
    return 0;
}
