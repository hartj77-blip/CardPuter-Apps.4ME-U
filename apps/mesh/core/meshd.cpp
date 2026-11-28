// zeroos-meshd: the radio daemon for Mesh (core/hub.h says what it does).
// Started with meshtasticd and stopped with it (files/zeroos-meshd.service).
//
//   zeroos-meshd [--listen PORT] [--radio PORT] [--data DIR] [--conf FILE] [--local]
//
// It runs as the person using the Cardputer (not root): the history is
// theirs, in ~/.local/share/zeroos/mesh, readable by no one else.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps/mesh/core/hub.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

std::string home_dir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    if (passwd* pw = getpwuid(getuid())) return pw->pw_dir;
    return "/tmp";
}

void make_dirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0700);
}

int usage() {
    std::fputs("usage: zeroos-meshd [--listen PORT] [--radio PORT] [--data DIR] [--conf FILE] [--local]\n"
               "  --listen PORT  where clients connect (4403)\n"
               "  --radio PORT   where meshtasticd's API is (4404)\n"
               "  --data DIR     the message history (~/.local/share/zeroos/mesh)\n"
               "  --conf FILE    settings Mesh writes (~/.config/zeroos/mesh.conf)\n"
               "  --local        answer this machine only, whatever the firewall says\n",
               stderr);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    zs::mesh::Hub::Options o;
    std::string home = home_dir();
    o.data_dir = home + "/.local/share/zeroos/mesh";
    o.conf_path = home + "/.config/zeroos/mesh.conf";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--listen" || a == "--radio") {
            const char* v = value();
            int port = v ? std::atoi(v) : 0;
            if (port < 1 || port > 65535) return usage();
            (a == "--listen" ? o.listen_port : o.radio_port) = uint16_t(port);
        } else if (a == "--data") {
            const char* v = value();
            if (!v) return usage();
            o.data_dir = v;
        } else if (a == "--conf") {
            const char* v = value();
            if (!v) return usage();
            o.conf_path = v;
        } else if (a == "--local") {
            o.listen_addr = "127.0.0.1";
        } else {
            return usage();
        }
    }
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    make_dirs(o.data_dir);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    zs::mesh::Hub hub(o);
    std::string why;
    if (!hub.start(&why)) {
        // Port 4403 taken: most likely a meshtasticd still on its old port
        // (it was running when Mesh was updated), or another mesh app's
        // daemon. files/zeroos-meshd-start deals with the first.
        std::fprintf(stderr, "zeroos-meshd: can't listen on port %u: %s\n", unsigned(o.listen_port), why.c_str());
        return 3;
    }
    while (!g_stop) hub.step(1000);
    return 0;
}
