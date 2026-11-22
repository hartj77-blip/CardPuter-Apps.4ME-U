// Desktop's saved computers, addresses and key codes (hosts.cpp).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "apps/desktop/hosts.h"

using namespace zs::desktop;

static int g_fail = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static void addresses() {
    std::string h;
    int p = 0;
    CHECK(split_address("pc", 3389, h, p) && h == "pc" && p == 3389);
    CHECK(split_address("pc:5901", 5900, h, p) && h == "pc" && p == 5901);
    CHECK(split_address(" 10.0.0.2:3390 ", 3389, h, p) && h == "10.0.0.2" && p == 3390);
    CHECK(split_address("[fe80::1]:5900", 3389, h, p) && h == "fe80::1" && p == 5900);
    CHECK(split_address("[fe80::1]", 3389, h, p) && h == "fe80::1" && p == 3389);
    CHECK(split_address("fe80::1", 3389, h, p) && h == "fe80::1" && p == 3389);
    CHECK(split_address("pc:1", 5900, h, p) && p == 1);
    CHECK(!split_address("", 3389, h, p));
    CHECK(!split_address(":3389", 3389, h, p));
    CHECK(!split_address("pc:", 3389, h, p));
    CHECK(!split_address("pc:x", 3389, h, p));
    CHECK(!split_address("pc:70000", 3389, h, p));
    CHECK(!split_address("pc:0", 3389, h, p));
    CHECK(!split_address("[fe80::1", 3389, h, p));
    CHECK(!split_address("[fe80::1]x", 3389, h, p));

    CHECK(vnc_display_arg("mac") == "mac:0");
    CHECK(vnc_display_arg("mac:5901") == "mac:1");
    CHECK(vnc_display_arg("[fe80::1]:5902") == "[fe80::1]:2");
    CHECK(vnc_display_arg("mac:6000").empty());
    CHECK(vnc_display_arg("").empty());
}

static void hosts_file() {
    std::vector<Host> in(2);
    in[0].name = "Office PC";
    in[0].proto = Proto::Rdp;
    in[0].address = "office:3390";
    in[0].user = "jo";
    in[0].domain = "CORP";
    in[0].size = 2;
    in[0].password = "pa ss";
    in[1].name = "Mac\tmini";   // a tab can't get into the file
    in[1].proto = Proto::Vnc;
    in[1].address = "mac";
    std::string text = format_hosts(in);
    std::vector<Host> out = parse_hosts(text);
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].name == "Office PC" && out[0].proto == Proto::Rdp && out[0].address == "office:3390");
        CHECK(out[0].user == "jo" && out[0].domain == "CORP" && out[0].size == 2 && out[0].password == "pa ss");
        CHECK(out[1].name == "Macmini" && out[1].proto == Proto::Vnc && out[1].address == "mac");
        CHECK(out[1].user.empty() && out[1].password.empty() && out[1].size == 1);
    }
    // Short and odd lines.
    out = parse_hosts("# comment\n\nx\tvnc\n\tvnc\tbox\r\nname\trdp\tpc\t\t\t99\n");
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(out[0].name == "box" && out[0].proto == Proto::Vnc);
        CHECK(out[1].size == 1);   // out of range: the default
    }

    // Saved privately, and read back.
    char dir[] = "/tmp/desktop-test-XXXXXX";
    if (mkdtemp(dir)) {
        CHECK(save_hosts(dir, in));
        CHECK(load_hosts(dir).size() == 2);
        std::string cmd = std::string("test \"$(stat -c %a ") + dir + "/.config/zeroos/desktop-hosts)\" = 600";
        CHECK(std::system(cmd.c_str()) == 0);
        CHECK(std::system((std::string("rm -rf ") + dir).c_str()) == 0);
    }
}

static void keys() {
    CHECK(keysym_for_key('a') == 'a');
    CHECK(keysym_for_key('~') == '~');
    CHECK(keysym_for_key(10) == ks::Return);
    CHECK(keysym_for_key(8) == ks::BackSpace);
    CHECK(keysym_for_key(27) == ks::Escape);
    CHECK(keysym_for_key(17) == ks::Up && keysym_for_key(20) == ks::Left);
    CHECK(keysym_for_key(127) == ks::Delete);
    CHECK(keysym_for_key(0x20ac) == 0x010020ac);
    CHECK(keysym_for_key(1) == 0);

    bool shift = true;
    CHECK(rdp_scancode('a', &shift) == 0x1e && !shift);
    CHECK(rdp_scancode('A', &shift) == 0x1e && shift);
    CHECK(rdp_scancode('1', &shift) == 0x02 && !shift);
    CHECK(rdp_scancode('!', &shift) == 0x02 && shift);
    CHECK(rdp_scancode('0', &shift) == 0x0b);
    CHECK(rdp_scancode('p', &shift) == 0x19);
    CHECK(rdp_scancode('l', &shift) == 0x26);
    CHECK(rdp_scancode('m', &shift) == 0x32);
    CHECK(rdp_scancode('/', &shift) == 0x35 && !shift);
    CHECK(rdp_scancode('?', &shift) == 0x35 && shift);
    CHECK(rdp_scancode('"', &shift) == 0x28 && shift);
    CHECK(rdp_scancode('`', &shift) == 0x29 && !shift);
    CHECK(rdp_scancode('|', &shift) == 0x2b && shift);
    CHECK(rdp_scancode(' ', &shift) == 0x39);
    CHECK(rdp_scancode(ks::Return, &shift) == 0x1c);
    CHECK(rdp_scancode(ks::Up, &shift) == 0x148);
    CHECK(rdp_scancode(ks::Delete, &shift) == 0x153);
    CHECK(rdp_scancode(ks::SuperL, &shift) == 0x15b);
    CHECK(rdp_scancode(ks::F1, &shift) == 0x3b);
    CHECK(rdp_scancode(ks::F1 + 9, &shift) == 0x44);
    CHECK(rdp_scancode(ks::F1 + 11, &shift) == 0x58);
    CHECK(rdp_scancode(0x010020ac, &shift) == 0);
}

int main() {
    addresses();
    hosts_file();
    keys();
    if (g_fail) {
        std::fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    std::puts("desktop hosts: ok");
    return 0;
}
