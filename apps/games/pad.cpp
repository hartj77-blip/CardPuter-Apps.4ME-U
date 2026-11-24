#include "apps/games/pad.h"

#include <algorithm>
#include <cerrno>

#include "apps/games/libretro_api.h"
#include "lvgl.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define ZS_EVDEV 1
#endif

namespace zs::games {
namespace {
using namespace retro;

// Linux key codes (linux/input-event-codes.h), kept here so this builds anywhere.
enum : uint16_t {
    E_BACKSPACE = 14, E_TAB = 15, E_Q = 16, E_W = 17, E_E = 18, E_U = 22, E_I = 23, E_ENTER = 28, E_A = 30, E_S = 31,
    E_D = 32, E_J = 36, E_K = 37, E_Z = 44, E_X = 45, E_SPACE = 57, E_KPENTER = 96, E_UP = 103,
    E_LEFT = 105, E_RIGHT = 106, E_DOWN = 108,
    B_SOUTH = 0x130, B_EAST = 0x131, B_NORTH = 0x133, B_WEST = 0x134, B_TL = 0x136, B_TR = 0x137,
    B_TL2 = 0x138, B_TR2 = 0x139, B_SELECT = 0x13a, B_START = 0x13b, B_MODE = 0x13c, B_THUMBL = 0x13d,
    B_THUMBR = 0x13e, B_DPAD_UP = 0x220, B_DPAD_DOWN = 0x221, B_DPAD_LEFT = 0x222, B_DPAD_RIGHT = 0x223,
    A_X = 0x00, A_Y = 0x01, A_HAT0X = 0x10, A_HAT0Y = 0x11,
};

struct Map {
    uint16_t code;
    unsigned button;
};

// Keyboard and gamepad buttons -> RetroPad. RetroPad uses the SNES layout:
// B bottom, A right, Y left, X top.
const Map kMap[] = {
    {E_W, kUp}, {E_UP, kUp}, {E_S, kDown}, {E_DOWN, kDown}, {E_A, kLeft}, {E_LEFT, kLeft},
    {E_D, kRight}, {E_RIGHT, kRight}, {E_K, kA}, {E_X, kA}, {E_J, kB}, {E_Z, kB}, {E_I, kX},
    {E_U, kY}, {E_Q, kL}, {E_E, kR}, {E_ENTER, kStart}, {E_KPENTER, kStart}, {E_SPACE, kSelect},
    {E_TAB, kSelect},
    {B_SOUTH, kB}, {B_EAST, kA}, {B_WEST, kY}, {B_NORTH, kX}, {B_TL, kL}, {B_TR, kR}, {B_TL2, kL2},
    {B_TR2, kR2}, {B_SELECT, kSelect}, {B_START, kStart}, {B_THUMBL, kL3}, {B_THUMBR, kR3},
    {B_DPAD_UP, kUp}, {B_DPAD_DOWN, kDown}, {B_DPAD_LEFT, kLeft}, {B_DPAD_RIGHT, kRight},
};

#ifdef ZS_EVDEV
bool bit(const unsigned long* bits, int n) {
    return bits[n / (8 * sizeof(long))] & (1UL << (n % (8 * sizeof(long))));
}
#endif
}  // namespace

Pad::~Pad() { close(); }

bool Pad::open() {
    close();
    {
        std::lock_guard<std::mutex> lock(mu_);
        scan();
        update();
    }
    joined_ = 0;   // the ones there at the start aren't news
    return has_devices();
}

void Pad::close() {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef ZS_EVDEV
    for (auto& d : devs_)
        if (d.fd >= 0) ::close(d.fd);
#endif
    devs_.clear();
    update();
}

// The player for a gamepad that just connected: the one it had before if
// that's free, else the first free one (player 4 if all are taken).
int Pad::free_player(const std::string& uniq) const {
    bool taken[kPlayers] = {};
    for (auto& d : devs_)
        if (d.gamepad) taken[d.player] = true;
    if (!uniq.empty())
        if (auto it = last_player_.find(uniq); it != last_player_.end() && !taken[it->second]) return it->second;
    for (int p = 0; p < kPlayers; ++p)
        if (!taken[p]) return p;
    return kPlayers - 1;
}

void Pad::scan() {
    last_scan_ = lv_tick_get();
#ifdef ZS_EVDEV
    for (int i = 0; i < 32; ++i) {
        std::string path = "/dev/input/event" + std::to_string(i);
        bool known = false;
        for (auto& d : devs_) known |= d.path == path;
        if (known) continue;
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        bool keyboard = bit(keys, KEY_A) && bit(keys, KEY_ENTER);
        bool gamepad = bit(keys, BTN_GAMEPAD) || bit(keys, BTN_SOUTH) || bit(keys, BTN_JOYSTICK);
        if (!keyboard && !gamepad) {
            ::close(fd);
            continue;
        }
        char uniq[64] = {0};
        if (ioctl(fd, EVIOCGUNIQ(sizeof uniq - 1), uniq) <= 0) uniq[0] = 0;
        attach(fd, path, gamepad && !keyboard, uniq);
    }
#endif
}

void Pad::attach(int fd, const std::string& path, bool gamepad, const std::string& uniq) {
    Dev d;
    d.fd = fd;
    d.path = path;
    d.gamepad = gamepad;
    d.uniq = uniq;
#ifdef ZS_EVDEV
    for (int a = 0; a < 2; ++a) {
        input_absinfo ai{};
        if (ioctl(fd, EVIOCGABS(a), &ai) == 0) {
            d.abs_min[a] = ai.minimum;
            d.abs_max[a] = ai.maximum;
        }
    }
#endif
    if (d.gamepad) {
        d.player = free_player(d.uniq);
        if (!d.uniq.empty()) last_player_[d.uniq] = d.player;
        joined_ = d.player + 1;
    }
    devs_.push_back(std::move(d));
}

void Pad::add_device(int fd, bool gamepad, const std::string& uniq) {
    std::lock_guard<std::mutex> lock(mu_);
    attach(fd, "test:" + std::to_string(fd), gamepad, uniq);
    update();
}

void Pad::handle(Dev& d, uint16_t type, uint16_t code, int32_t value) {
#ifdef ZS_EVDEV
    if (type == EV_KEY) {
        if (value == 2) return;   // kernel auto-repeat
        if (code < d.keys.size()) d.keys[code] = value ? 1 : 0;
        if (code == B_MODE && value) menu_ = true;
        activity_ = true;
    } else if (type == EV_ABS) {
        auto dir = [](int32_t v, int lo, int hi) {
            if (hi <= lo) return v < 0 ? -1 : v > 0 ? 1 : 0;
            int mid = (lo + hi) / 2, dead = (hi - lo) / 4;   // a quarter of the way out
            return v < mid - dead ? -1 : v > mid + dead ? 1 : 0;
        };
        if (code == A_HAT0X) d.hat_x = value < 0 ? -1 : value > 0 ? 1 : 0;
        else if (code == A_HAT0Y) d.hat_y = value < 0 ? -1 : value > 0 ? 1 : 0;
        else if (code == A_X && d.gamepad) d.stick_x = dir(value, d.abs_min[0], d.abs_max[0]);
        else if (code == A_Y && d.gamepad) d.stick_y = dir(value, d.abs_min[1], d.abs_max[1]);
        else return;
        activity_ = true;
    }
#else
    (void)d;
    (void)type;
    (void)code;
    (void)value;
#endif
}

void Pad::poll() {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef ZS_EVDEV
    for (size_t i = 0; i < devs_.size();) {
        Dev& d = devs_[i];
        input_event ev[32];
        ssize_t n;
        while ((n = read(d.fd, ev, sizeof ev)) > 0) {
            for (size_t k = 0; k < size_t(n) / sizeof(input_event); ++k) {
                if (ev[k].type == EV_SYN && ev[k].code == SYN_DROPPED) {
                    std::fill(d.keys.begin(), d.keys.end(), 0);
                    d.hat_x = d.hat_y = d.stick_x = d.stick_y = 0;
                } else {
                    handle(d, ev[k].type, ev[k].code, ev[k].value);
                }
            }
        }
        if (n == 0 || (n < 0 && errno == ENODEV)) {   // unplugged, or Bluetooth went away
            ::close(d.fd);
            devs_.erase(devs_.begin() + long(i));
            continue;
        }
        ++i;
    }
    // A Bluetooth gamepad that connects mid-game.
    if (lv_tick_elaps(last_scan_) > 2000) scan();
#endif
    update();
}

uint16_t Pad::dev_buttons(const Dev& d) {
    uint16_t m = 0;
    for (const Map& k : kMap)
        if (k.code < d.keys.size() && d.keys[k.code]) m |= uint16_t(1u << k.button);
    int x = d.hat_x ? d.hat_x : d.stick_x, y = d.hat_y ? d.hat_y : d.stick_y;
    if (x < 0) m |= 1u << kLeft;
    if (x > 0) m |= 1u << kRight;
    if (y < 0) m |= 1u << kUp;
    if (y > 0) m |= 1u << kDown;
    return m;
}

void Pad::update() {
    uint16_t held[kPlayers] = {};
    uint16_t any_pad = 0;
    bool fast = false;
    int pads = 0, kb = 0;   // keyboards: player 1
    for (auto& d : devs_) {
        uint16_t b = dev_buttons(d);
        held[d.gamepad ? d.player : kb] |= b;
        if (d.gamepad) {
            any_pad |= b;
            ++pads;
        }
        fast |= d.keys[E_BACKSPACE] || d.keys[B_THUMBR];
    }
    for (int p = 0; p < kPlayers; ++p) held_[p] = held[p];
    pad_any_ = any_pad;
    fast_ = fast;
    devices_ = int(devs_.size());
    gamepads_ = pads;
}

uint16_t Pad::buttons(int player) const {
    if (player < 0 || player >= kPlayers) return 0;
    uint16_t m = held_[player].load();
    if (player == 0) {   // the simulator's keys: the keyboard's
        uint32_t now = lv_tick_get();
        for (unsigned b = 0; b < 16; ++b) {
            uint32_t until = sim_until_[b].load();
            if (until && int32_t(until - now) > 0) m |= uint16_t(1u << b);
        }
    }
    return m;
}

bool Pad::fast_forward() const {
    if (fast_.load()) return true;
    uint32_t until = sim_fast_until_.load();
    return until && int32_t(until - lv_tick_get()) > 0;
}

void Pad::swap_players() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& d : devs_) {
        if (!d.gamepad || d.player > 1) continue;
        d.player = 1 - d.player;
        if (!d.uniq.empty()) last_player_[d.uniq] = d.player;
    }
    update();
}

std::string Pad::players_text() const {
    std::lock_guard<std::mutex> lock(mu_);
    int pads[kPlayers] = {};
    for (auto& d : devs_)
        if (d.gamepad) ++pads[d.player];
    int kb = 0;
    std::string s;
    for (int p = 0; p < kPlayers; ++p) {
        std::string who;
        if (p == kb) who = "keys";
        if (pads[p]) who += std::string(who.empty() ? "" : "+") + (pads[p] > 1 ? std::to_string(pads[p]) + " pads" : "pad");
        if (who.empty()) continue;
        s += (s.empty() ? "" : ", ") + std::string("P") + std::to_string(p + 1) + " " + who;
    }
    return s;
}

void Pad::press_key(uint32_t key) {
    if (key == LV_KEY_BACKSPACE) {
        sim_fast_until_ = lv_tick_get() + 500;
        return;
    }
    int b = -1;
    switch (key) {
        case 'w': case 'W': case LV_KEY_UP: b = kUp; break;
        case 's': case 'S': case LV_KEY_DOWN: b = kDown; break;
        case 'a': case 'A': case LV_KEY_LEFT: b = kLeft; break;
        case 'd': case 'D': case LV_KEY_RIGHT: b = kRight; break;
        case 'k': case 'K': case 'x': case 'X': b = kA; break;
        case 'j': case 'J': case 'z': case 'Z': b = kB; break;
        case 'i': case 'I': b = kX; break;
        case 'u': case 'U': b = kY; break;
        case 'q': case 'Q': b = kL; break;
        case 'e': case 'E': b = kR; break;
        case LV_KEY_ENTER: b = kStart; break;
        case ' ': case LV_KEY_NEXT: b = kSelect; break;
        default: return;
    }
    sim_until_[b] = lv_tick_get() + 150;
}

}  // namespace zs::games
