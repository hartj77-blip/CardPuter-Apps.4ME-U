// Desktop's saved computers, and the small pure helpers the app and the
// viewer share. No LVGL here: tests/hosts_test.cpp runs it on any machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs::desktop {

enum class Proto { Vnc, Rdp };

struct Host {
    std::string name;       // what the list shows, e.g. "Office PC"
    Proto proto = Proto::Rdp;
    std::string address;    // host, host:port, [v6]:port
    std::string user;       // RDP user name, or the Mac's for VNC (empty: none)
    std::string domain;     // RDP only, usually empty
    std::string password;   // only if "Remember password" is on
    int size = 1;           // RDP desktop size: index into rdp_sizes()
};

const char* proto_name(Proto p);   // "VNC" / "RDP"
int default_port(Proto p);         // 5900 / 3389

struct RdpSize {
    int w, h;
    const char* label;
};
// What an RDP desktop can be asked to be. The screen is 320 x 150, so the
// smaller ones are easier to read on it.
const std::vector<RdpSize>& rdp_sizes();

// ~/.config/zeroos/desktop-hosts: one computer per line, tab-separated:
//   name proto address user domain size password
// The file is private (0600): it can hold passwords.
std::vector<Host> parse_hosts(const std::string& text);
std::string format_hosts(const std::vector<Host>& hosts);
std::string hosts_path(const std::string& home);
std::vector<Host> load_hosts(const std::string& home);
bool save_hosts(const std::string& home, const std::vector<Host>& hosts);

// "pc", "pc:5901", "10.0.0.2:3390", "[fe80::1]:5900", "fe80::1".
// False if the address is empty or the port isn't a number 1..65535.
bool split_address(const std::string& addr, int def_port, std::string& host, int& port);

// gvncviewer (VNC on the monitor) takes host:display, display = port - 5900.
// Empty if the port can't be written that way.
std::string vnc_display_arg(const std::string& addr);

// X keysyms, which VNC uses as they are and the viewer turns into RDP scan
// codes.
namespace ks {
constexpr uint32_t BackSpace = 0xff08, Tab = 0xff09, Return = 0xff0d, Escape = 0xff1b,
                   Home = 0xff50, Left = 0xff51, Up = 0xff52, Right = 0xff53, Down = 0xff54,
                   PageUp = 0xff55, PageDown = 0xff56, End = 0xff57, Insert = 0xff63,
                   F1 = 0xffbe, ShiftL = 0xffe1, ControlL = 0xffe3, AltL = 0xffe9,
                   SuperL = 0xffeb, Delete = 0xffff;
}

// A key as ZeroShell hands it to an app (a character, or LVGL's navigation
// keys) as a keysym; 0 if it has none.
uint32_t keysym_for_key(uint32_t key);

// The RDP scan code (with 0x100 for the extended ones, FreeRDP's
// MAKE_RDP_SCANCODE) for a keysym on a US keyboard, and whether Shift has
// to be down for it. 0 if there is none.
uint32_t rdp_scancode(uint32_t keysym, bool* needs_shift);

// One text value from a line of the viewer protocol: tabs and newlines out.
std::string clean_field(const std::string& s);

}  // namespace zs::desktop
