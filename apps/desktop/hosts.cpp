#include "apps/desktop/hosts.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace zs::desktop {

const char* proto_name(Proto p) { return p == Proto::Vnc ? "VNC" : "RDP"; }
int default_port(Proto p) { return p == Proto::Vnc ? 5900 : 3389; }

const std::vector<RdpSize>& rdp_sizes() {
    static const std::vector<RdpSize> sizes = {
        {640, 300, "640 x 300"},
        {960, 450, "960 x 450"},
        {1280, 600, "1280 x 600"},
        {1600, 750, "1600 x 750"},
        {1920, 1080, "1920 x 1080"},
    };
    return sizes;
}

std::string clean_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
        if (ch != '\t' && ch != '\n' && ch != '\r') out += ch;
    return out;
}

namespace {
std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    while (true) {
        size_t t = line.find('\t', start);
        f.push_back(line.substr(start, t == std::string::npos ? std::string::npos : t - start));
        if (t == std::string::npos) break;
        start = t + 1;
    }
    return f;
}
}  // namespace

std::vector<Host> parse_hosts(const std::string& text) {
    std::vector<Host> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f = split_tabs(line);
        if (f.size() < 3 || f[2].empty()) continue;
        Host h;
        h.name = f[0].empty() ? f[2] : f[0];
        h.proto = (f[1] == "vnc" || f[1] == "VNC") ? Proto::Vnc : Proto::Rdp;
        h.address = f[2];
        if (f.size() > 3) h.user = f[3];
        if (f.size() > 4) h.domain = f[4];
        if (f.size() > 5) {
            int s = std::atoi(f[5].c_str());
            if (s >= 0 && s < int(rdp_sizes().size())) h.size = s;
        }
        if (f.size() > 6) h.password = f[6];
        out.push_back(std::move(h));
    }
    return out;
}

std::string format_hosts(const std::vector<Host>& hosts) {
    std::string s = "# Desktop app: name, vnc|rdp, address, user, domain, size, password\n";
    for (const Host& h : hosts) {
        s += clean_field(h.name) + '\t' + (h.proto == Proto::Vnc ? "vnc" : "rdp") + '\t' +
             clean_field(h.address) + '\t' + clean_field(h.user) + '\t' + clean_field(h.domain) + '\t' +
             std::to_string(h.size) + '\t' + clean_field(h.password) + '\n';
    }
    return s;
}

std::string hosts_path(const std::string& home) { return home + "/.config/zeroos/desktop-hosts"; }

std::vector<Host> load_hosts(const std::string& home) {
    std::ifstream f(hosts_path(home));
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_hosts(ss.str());
}

bool save_hosts(const std::string& home, const std::vector<Host>& hosts) {
    std::string dir = home + "/.config";
    mkdir(dir.c_str(), 0755);
    dir += "/zeroos";
    mkdir(dir.c_str(), 0755);
    std::string path = hosts_path(home);
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    fchmod(fd, 0600);
    std::string s = format_hosts(hosts);
    bool ok = write(fd, s.data(), s.size()) == ssize_t(s.size());
    ok = fsync(fd) == 0 && ok;
    close(fd);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool split_address(const std::string& addr_in, int def_port, std::string& host, int& port) {
    std::string addr;
    for (char ch : addr_in)
        if (ch != ' ') addr += ch;
    host.clear();
    port = def_port;
    if (addr.empty()) return false;
    std::string p;
    if (addr[0] == '[') {
        size_t close = addr.find(']');
        if (close == std::string::npos) return false;
        host = addr.substr(1, close - 1);
        if (close + 1 < addr.size()) {
            if (addr[close + 1] != ':') return false;
            p = addr.substr(close + 2);
            if (p.empty()) return false;
        }
    } else {
        size_t colon = addr.find(':');
        if (colon != std::string::npos && addr.find(':', colon + 1) == std::string::npos) {
            host = addr.substr(0, colon);
            p = addr.substr(colon + 1);
            if (p.empty()) return false;
        } else {
            host = addr;   // a name, or an IPv6 address without a port
        }
    }
    if (host.empty()) return false;
    if (!p.empty()) {
        for (char ch : p)
            if (ch < '0' || ch > '9') return false;
        if (p.size() > 5) return false;
        port = std::atoi(p.c_str());
        if (port < 1 || port > 65535) return false;
    }
    return true;
}

std::string vnc_display_arg(const std::string& addr) {
    std::string host;
    int port = 0;
    if (!split_address(addr, 5900, host, port)) return "";
    if (port < 5900 || port > 5999) return "";
    if (host.find(':') != std::string::npos) host = "[" + host + "]";
    return host + ":" + std::to_string(port - 5900);
}

uint32_t keysym_for_key(uint32_t key) {
    switch (key) {
        case 8: return ks::BackSpace;
        case 9: return ks::Tab;
        case 10: case 13: return ks::Return;
        case 27: return ks::Escape;
        case 127: return ks::Delete;
        case 17: return ks::Up;
        case 18: return ks::Down;
        case 19: return ks::Right;
        case 20: return ks::Left;
        case 2: return ks::Home;
        case 3: return ks::End;
        default: break;
    }
    if (key >= 0x20 && key <= 0x7e) return key;
    if (key >= 0xa0 && key <= 0xff) return key;           // Latin-1 keysyms are the code points
    if (key > 0xff && key < 0x110000) return 0x01000000 | key;
    return 0;
}

uint32_t rdp_scancode(uint32_t keysym, bool* needs_shift) {
    bool shift = false;
    uint32_t code = 0;
    constexpr uint32_t E = 0x100;   // extended key
    // US layout rows.
    static const char* const kRow1 = "1234567890-=";
    static const char* const kRow1s = "!@#$%^&*()_+";
    static const char* const kQ = "qwertyuiop[]";
    static const char* const kQs = "QWERTYUIOP{}";
    static const char* const kA = "asdfghjkl;'`";
    static const char* const kAs = "ASDFGHJKL:\"~";
    static const char* const kZ = "zxcvbnm,./";
    static const char* const kZs = "ZXCVBNM<>?";
    auto find = [&](const char* row, const char* srow, uint32_t first) -> bool {
        for (uint32_t i = 0; row[i]; ++i) {
            if (uint32_t((unsigned char)row[i]) == keysym) { code = first + i; return true; }
            if (uint32_t((unsigned char)srow[i]) == keysym) { code = first + i; shift = true; return true; }
        }
        return false;
    };
    if (keysym < 0x80) {
        if (keysym == ' ') code = 0x39;
        else if (keysym == '\\') code = 0x2b;
        else if (keysym == '|') { code = 0x2b; shift = true; }
        else if (find(kRow1, kRow1s, 0x02) || find(kQ, kQs, 0x10) || find(kA, kAs, 0x1e) ||
                 find(kZ, kZs, 0x2c)) {
        }
    } else {
        switch (keysym) {
            case ks::BackSpace: code = 0x0e; break;
            case ks::Tab: code = 0x0f; break;
            case ks::Return: code = 0x1c; break;
            case ks::Escape: code = 0x01; break;
            case ks::Delete: code = E | 0x53; break;
            case ks::Insert: code = E | 0x52; break;
            case ks::Home: code = E | 0x47; break;
            case ks::End: code = E | 0x4f; break;
            case ks::PageUp: code = E | 0x49; break;
            case ks::PageDown: code = E | 0x51; break;
            case ks::Up: code = E | 0x48; break;
            case ks::Down: code = E | 0x50; break;
            case ks::Left: code = E | 0x4b; break;
            case ks::Right: code = E | 0x4d; break;
            case ks::ShiftL: code = 0x2a; break;
            case ks::ControlL: code = 0x1d; break;
            case ks::AltL: code = 0x38; break;
            case ks::SuperL: code = E | 0x5b; break;
            default:
                if (keysym >= ks::F1 && keysym < ks::F1 + 10) code = 0x3b + (keysym - ks::F1);
                else if (keysym == ks::F1 + 10) code = 0x57;   // F11
                else if (keysym == ks::F1 + 11) code = 0x58;   // F12
                break;
        }
    }
    if (needs_shift) *needs_shift = shift;
    return code;
}

}  // namespace zs::desktop
