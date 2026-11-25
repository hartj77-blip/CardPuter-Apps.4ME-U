// A phone's music over Bluetooth, for Car's "Music from your phone"
// (README.md). The phone's player (AVRCP) is an org.bluez.MediaPlayer1
// object under its device, e.g. /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0;
// busctl reads and drives it.
//
// The parsing is a copy of the OS's shell/src/hal/bt_player.h (tested by
// its shell/tests/bt_player_test.cpp), in a namespace of its own: the app
// then runs on a ZeroOS without that file, and the two never clash inside
// ZeroShell. Keep them the same; this copy also reads the song's length and
// where it is (tests/phone_player_test.cpp).
#pragma once

#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace zs::car_phone {


// "busctl --list tree org.bluez": the player objects, in the order listed.
inline std::vector<std::string> player_paths(const std::string& tree) {
    std::vector<std::string> v;
    size_t at = 0;
    while (at < tree.size()) {
        size_t nl = tree.find('\n', at);
        std::string l = tree.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? tree.size() : nl + 1;
        while (!l.empty() && std::isspace((unsigned char)l.back())) l.pop_back();
        size_t s = l.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        l = l.substr(s);
        // /org/bluez/hciN/dev_XX_XX_XX_XX_XX_XX/playerN and nothing below it
        if (l.rfind("/org/bluez/hci", 0) != 0) continue;
        size_t dev = l.find("/dev_");
        size_t pl = l.find("/player");
        if (dev == std::string::npos || pl != dev + 5 + 17) continue;
        bool ok = pl + 7 < l.size();
        for (size_t i = pl + 7; ok && i < l.size(); ++i) ok = std::isdigit((unsigned char)l[i]);
        for (char ch : l) ok = ok && (std::isalnum((unsigned char)ch) || ch == '/' || ch == '_');
        if (ok) v.push_back(l);
    }
    return v;
}

// "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0" -> "AA:BB:CC:DD:EE:FF"
inline std::string address_of(const std::string& path) {
    size_t dev = path.find("/dev_");
    if (dev == std::string::npos || path.size() < dev + 5 + 17) return "";
    std::string a = path.substr(dev + 5, 17);
    for (auto& ch : a)
        if (ch == '_') ch = ':';
    return a;
}

// "bluetoothctl info <address>": a phone, or anything else that sends music
// (Audio Source). Cars, speakers and headphones receive it (Audio Sink) and
// are left alone: their own players, if any, aren't what the keys are for.
inline bool is_music_source(const std::string& info) {
    return info.find("Audio Source (0000110a") != std::string::npos || info.find("Icon: phone") != std::string::npos;
}

// busctl's text form of a value, split into words; a quoted string is one
// word with its C escapes undone (busctl writes UTF-8 bytes as \xNN).
inline std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> v;
    size_t i = 0, n = line.size();
    auto hex = [](char ch) { return std::isdigit((unsigned char)ch) ? ch - '0' : (std::tolower((unsigned char)ch) - 'a' + 10); };
    while (i < n) {
        while (i < n && std::isspace((unsigned char)line[i])) ++i;
        if (i >= n) break;
        std::string w;
        if (line[i] != '"') {
            while (i < n && !std::isspace((unsigned char)line[i])) w += line[i++];
            v.push_back(w);
            continue;
        }
        ++i;
        while (i < n && line[i] != '"') {
            char ch = line[i++];
            if (ch != '\\' || i >= n) {
                w += ch;
                continue;
            }
            char e = line[i++];
            switch (e) {
                case 'n': w += '\n'; break;
                case 't': w += '\t'; break;
                case 'r': w += '\r'; break;
                case 'a': w += '\a'; break;
                case 'b': w += '\b'; break;
                case 'f': w += '\f'; break;
                case 'v': w += '\v'; break;
                case 'x':
                    if (i + 1 < n && std::isxdigit((unsigned char)line[i]) && std::isxdigit((unsigned char)line[i + 1])) {
                        w += char(hex(line[i]) * 16 + hex(line[i + 1]));
                        i += 2;
                    }
                    break;
                default:
                    if (e >= '0' && e <= '7' && i + 1 < n && line[i] >= '0' && line[i] <= '7' && line[i + 1] >= '0' &&
                        line[i + 1] <= '7') {
                        w += char((e - '0') * 64 + (line[i] - '0') * 8 + (line[i + 1] - '0'));
                        i += 2;
                    } else {
                        w += e;   // \\ \" \' and anything else: the character itself
                    }
            }
        }
        if (i < n) ++i;   // the closing quote
        v.push_back(w);
    }
    return v;
}

struct Info {
    std::string status;   // playing, paused, stopped, forward-seek, reverse-seek, error; "" if unknown
    std::string title, artist;
    int duration_ms = -1, position_ms = -1;   // -1: the phone didn't say
    bool playing() const { return status == "playing" || status == "forward-seek" || status == "reverse-seek"; }
};

// "busctl get-property org.bluez <player> org.bluez.MediaPlayer1 Status Track":
//   s "playing"
//   a{sv} 3 "Title" s "Song" "Artist" s "Band" "Duration" u 215000
//   u 61000            (Position, when asked for: milliseconds in)
inline Info parse_info(const std::string& out) {
    Info info;
    size_t at = 0;
    while (at < out.size()) {
        size_t nl = out.find('\n', at);
        std::vector<std::string> w = words(out.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? out.size() : nl + 1;
        if (w.size() == 2 && w[0] == "s") info.status = w[1];
        if (w.size() == 2 && w[0] == "u") info.position_ms = std::atoi(w[1].c_str());
        if (w.size() < 2 || w[0] != "a{sv}") continue;
        // Pairs of name, type, value. Every Track field is a string or a number.
        for (size_t i = 2; i + 2 < w.size(); i += 3) {
            if (w[i] == "Duration" && w[i + 1] == "u") info.duration_ms = std::atoi(w[i + 2].c_str());
            if (w[i + 1] != "s") continue;
            if (w[i] == "Title") info.title = w[i + 2];
            if (w[i] == "Artist") info.artist = w[i + 2];
        }
    }
    return info;
}

enum class Key { PlayPause, Play, Pause, Stop, Next, Previous };

// The MediaPlayer1 method for a key. PlayPause looks at what the phone is
// doing; Stop pauses, because the phone's Stop can end its queue.
inline const char* method_for(Key k, bool playing) {
    switch (k) {
        case Key::PlayPause: return playing ? "Pause" : "Play";
        case Key::Play: return "Play";
        case Key::Pause:
        case Key::Stop: return "Pause";
        case Key::Next: return "Next";
        case Key::Previous: return "Previous";
    }
    return "";
}

// --- Running busctl, bluetoothctl and systemctl (as the UI's user).

inline std::string run(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r")) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
    }
    return out;
}

inline bool is_address(const std::string& a) {
    if (a.size() != 17) return false;
    for (size_t i = 0; i < 17; ++i)
        if (i % 3 == 2 ? a[i] != ':' : !std::isxdigit((unsigned char)a[i])) return false;
    return true;
}

// A paired device that sends music: a phone, a tablet.
inline bool is_phone(const std::string& address) {
    return is_address(address) && is_music_source(run("bluetoothctl info " + address));
}

// The phone's player, or "" while it has none (just connected, or nothing
// playing yet on some phones).
inline std::string player_of(const std::string& address) {
    for (const std::string& p : player_paths(run("busctl --list tree org.bluez")))
        if (address_of(p) == address) return p;
    return "";
}

inline Info read_player(const std::string& path) {
    if (path.empty()) return {};
    // Position last: a phone without it still gives the other two.
    return parse_info(run("busctl get-property org.bluez " + path + " org.bluez.MediaPlayer1 Status Track Position"));
}

// path is from player_paths(): letters, digits, '_' and '/' only.
inline void send(const std::string& path, Key k, bool playing) {
    if (path.empty()) return;
    run(std::string("busctl call org.bluez ") + path + " org.bluez.MediaPlayer1 " + method_for(k, playing));
}

// The settings in /etc/wireplumber/wireplumber.conf.d/60-zeroos-car.conf
// apply when WirePlumber starts. Right after installing (or updating) Car
// it is still running with the old ones, so restart it once, before a
// phone pairs: until then the phone would also be offered a hands-free kit.
// The file's ctime is when dpkg put it there (its mtime is from the build).
inline void apply_sound_settings() {
    struct stat st;
    if (stat("/etc/wireplumber/wireplumber.conf.d/60-zeroos-car.conf", &st) != 0) return;
    std::string up = run("ps -o etimes= -C wireplumber");   // seconds since it started
    if (up.find_first_of("0123456789") == std::string::npos) return;
    long started = long(std::time(nullptr)) - std::strtol(up.c_str(), nullptr, 10);
    if (started > long(st.st_ctime)) return;
    run("systemctl --user restart wireplumber");
}

}  // namespace zs::car_phone
