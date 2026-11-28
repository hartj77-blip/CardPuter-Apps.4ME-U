// The radio daemon end to end, over real sockets on this machine:
//
//   a stand-in meshtasticd  <->  the daemon (Hub)  <->  the app's Session,
//   (one client at a time,                              and two more clients
//    like the real one)                                 (ZeroShell, a phone)
//
// Several clients at once, each getting its settings; a text from one seen by
// the others and in the history; messages kept while the app is closed; the
// radio restarting under everyone; no radio at all; the GPS given to the radio.
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps/mesh/core/hub.h"
#include "apps/mesh/core/session.h"

using namespace zs::mesh;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

long mono_ms() {
    using namespace std::chrono;
    return long(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

int listen_on(uint16_t& port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    socklen_t len = sizeof a;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
    return fd;
}

int connect_to(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(fd);
        return -1;
    }
    int flags = 1;
    (void)flags;
    return fd;
}

// meshtasticd's API as far as a client can tell: one connection, and a new
// one pushes the old one out. Its clock runs ten times fast, so acks that
// take seconds on the air take tenths here.
struct RadioServer {
    FakeRadio radio;
    uint16_t port = 0;
    int lfd = -1, cfd = -1;
    long start = mono_ms(), down_until = 0;
    int accepted = 0, kicked = 0;

    explicit RadioServer(const FakeRadio::Options& o) : radio(o) { lfd = listen_on(port); }
    ~RadioServer() {
        if (cfd >= 0) ::close(cfd);
        if (lfd >= 0) ::close(lfd);
    }
    void stop() {
        if (cfd >= 0) ::close(cfd);
        if (lfd >= 0) ::close(lfd);
        cfd = lfd = -1;
        down_until = -1;   // until start_again()
    }
    void start_again() {
        down_until = 0;
        lfd = listen_on(port);
        radio.reconnect();
    }
    void step() {
        long now = mono_ms();
        if (lfd < 0) {
            if (down_until < 0 || now < down_until) return;
            start_again();
        }
        int fd = ::accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            ++accepted;
            if (cfd >= 0) {   // "Force close previous TCP connection"
                ::close(cfd);
                ++kicked;
            }
            cfd = fd;
            radio.reconnect();
        }
        if (cfd >= 0) {
            char buf[4096];
            for (;;) {
                ssize_t n = ::recv(cfd, buf, sizeof buf, MSG_DONTWAIT);
                if (n > 0) {
                    radio.feed(buf, size_t(n));
                    continue;
                }
                if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    ::close(cfd);
                    cfd = -1;
                }
                break;
            }
        }
        radio.advance((now - start) * 10);
        if (radio.take_restart()) {
            // The process exits and systemd brings it back a moment later.
            if (cfd >= 0) ::close(cfd);
            ::close(lfd);
            cfd = lfd = -1;
            down_until = now + 400;
            return;
        }
        std::string out = radio.take_out();
        if (cfd >= 0 && !out.empty()) {
            size_t at = 0;
            while (at < out.size()) {
                ssize_t n = ::send(cfd, out.data() + at, out.size() - at, MSG_NOSIGNAL);
                if (n > 0) at += size_t(n);
                else if (errno != EAGAIN && errno != EWOULDBLOCK) break;
            }
        }
    }
};

// A client as ZeroShell or a phone is one: its own socket, its own Client.
struct Other {
    int fd = -1;
    Client client;
    Deframer in;
    int texts_from_us = 0, texts_from_others = 0;
    std::string last_text;
    int queue_refused = 0;

    bool open(uint16_t port) {
        fd = connect_to(port);
        if (fd < 0) return false;
        client.link_up(long(std::time(nullptr)));
        return true;
    }
    void close_it() {
        if (fd >= 0) ::close(fd);
        fd = -1;
        client.link_down();
    }
    bool is_open() const { return fd >= 0; }
    void raw(const std::string& bytes) {
        if (fd >= 0) (void)!::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    }
    void step() {
        if (fd < 0) return;
        long now = long(std::time(nullptr));
        client.tick(now);
        raw(client.take_out());
        char buf[4096];
        for (;;) {
            ssize_t n = ::recv(fd, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                in.feed(buf, size_t(n));
                std::string payload;
                while (in.next(payload)) {
                    FromRadio f;
                    if (decode(payload, f)) {
                        if (f.which == FromRadio::Packet && f.packet.has_decoded && f.packet.decoded.portnum == kPortText) {
                            last_text = f.packet.decoded.payload;
                            if (f.packet.from == client.my_num()) ++texts_from_us;
                            else ++texts_from_others;
                        }
                        if (f.which == FromRadio::QueueStatusMsg && f.queue_status.res != 0) ++queue_refused;
                    }
                    client.on_from_radio(payload, now);
                }
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) close_it();
            break;
        }
    }
};

// gpsd, as much as the daemon uses: a fix a second once it's asked to watch.
struct GpsServer {
    uint16_t port = 0;
    int lfd = -1, cfd = -1;
    bool watching = false;
    double lat = 46.6500, lon = 8.6000;
    long last = 0;
    GpsServer() { lfd = listen_on(port); }
    ~GpsServer() {
        if (cfd >= 0) ::close(cfd);
        if (lfd >= 0) ::close(lfd);
    }
    void step() {
        if (cfd < 0) {
            cfd = ::accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) return;
            const char* hello = "{\"class\":\"VERSION\",\"release\":\"3.25\"}\n";
            (void)!::send(cfd, hello, std::strlen(hello), MSG_NOSIGNAL);
        }
        char buf[512];
        ssize_t n = ::recv(cfd, buf, sizeof buf - 1, MSG_DONTWAIT);
        if (n > 0) {
            buf[n] = 0;
            if (std::strstr(buf, "?WATCH")) watching = true;
        } else if (n == 0) {
            ::close(cfd);
            cfd = -1;
            watching = false;
            return;
        }
        long now = mono_ms();
        if (watching && now - last > 200) {
            last = now;
            char line[256];
            std::snprintf(line, sizeof line,
                          "{\"class\":\"SKY\",\"nSat\":9}\n{\"class\":\"TPV\",\"device\":\"/dev/serial0\",\"mode\":3,"
                          "\"time\":\"2026-11-28T10:00:00.000Z\",\"lat\":%.7f,\"lon\":%.7f,\"altMSL\":1502.300,\"speed\":0.1}\n",
                          lat, lon);
            (void)!::send(cfd, line, std::strlen(line), MSG_NOSIGNAL);
        }
    }
};

struct World {
    std::string dir;
    RadioServer radio;
    GpsServer gps;
    std::unique_ptr<Hub> hub;
    std::unique_ptr<Session> app;
    Other shell, phone;

    explicit World(const FakeRadio::Options& fo) : radio(fo) {
        char tmpl[] = "/tmp/zs-mesh-hub-XXXXXX";
        dir = mkdtemp(tmpl);
        Hub::Options ho;
        ho.listen_addr = "127.0.0.1";
        ho.listen_port = 0;
        ho.radio_port = radio.port;
        ho.data_dir = dir + "/data";
        ho.conf_path = dir + "/mesh.conf";
        ho.gpsd_port = gps.port;
        ho.quiet = true;
        hub = std::make_unique<Hub>(ho);
        std::string why;
        CHECK(hub->start(&why));
        Session::Options so;
        so.port = hub->port();
        so.data_dir = dir + "/data";
        app = std::make_unique<Session>(so);
    }
    ~World() {
        app.reset();
        hub.reset();
        std::string cmd = "rm -rf '" + dir + "'";
        (void)!std::system(cmd.c_str());
    }
    void spin(long ms) {
        long end = mono_ms() + ms;
        while (mono_ms() < end) step();
    }
    void step() {
        radio.step();
        gps.step();
        hub->step(2);
        long now = long(std::time(nullptr));
        app->tick(now, mono_ms());
        shell.step();
        phone.step();
    }
    // Until `done` or the time is up; true if it got there.
    template <typename Fn> bool until(Fn done, long ms = 8000) {
        long end = mono_ms() + ms;
        while (mono_ms() < end) {
            step();
            if (done()) return true;
        }
        return done();
    }
    Client& c() { return app->client(); }
    const Message* last(const std::string& key) {
        const Chat* chat = c().chat(key);
        return chat && !chat->msgs.empty() ? &chat->msgs.back() : nullptr;
    }
};

void test_many_clients() {
    FakeRadio::Options fo;
    fo.chatter = false;
    World w(fo);
    w.app->resume();
    CHECK(w.until([&] { return w.c().ready(); }));
    CHECK(w.hub->radio_connected() && w.hub->my_num() == FakeRadio::kMe);
    CHECK(w.c().my_num() == FakeRadio::kMe && w.c().nodes().size() == 10 && w.c().channel_name(1) == "Friends");

    // ZeroShell and a phone join. The radio still has one connection: ours.
    CHECK(w.shell.open(w.hub->port()) && w.phone.open(w.hub->port()));
    CHECK(w.until([&] { return w.shell.client.ready() && w.phone.client.ready(); }));
    CHECK(w.hub->clients() == 3 && w.hub->clients_synced() == 3);
    CHECK(w.radio.accepted == 1 && w.radio.kicked == 0);
    CHECK(w.radio.radio.config_downloads() == 4);   // the daemon's own, and one each
    CHECK(w.phone.client.nodes().size() == 10 && w.shell.client.lora().region == 3);

    // The app sends: the radio gets it once, the history has it, the others see it.
    uint32_t id = w.c().send_text("c0", "Hello from the app");
    CHECK(id != 0);
    CHECK(w.until([&] { return w.last("c0") && w.last("c0")->state == Delivery::Relayed; }));
    CHECK(w.radio.radio.texts_taken() == 1);
    CHECK(w.last("c0")->mine && w.last("c0")->text == "Hello from the app" && w.last("c0")->id == id);
    CHECK(w.until([&] { return w.shell.texts_from_us == 1 && w.phone.texts_from_us == 1; }));
    CHECK(w.phone.last_text == "Hello from the app");

    // The phone sends: the app's chat shows it as ours, with its delivery.
    w.spin(350);   // (the stand-in radio's two seconds between texts, at its speed)
    uint32_t pid = w.phone.client.send_text("c1", "Hello from the phone");
    CHECK(pid != 0);
    CHECK(w.until([&] { return w.last("c1") && w.last("c1")->state == Delivery::Relayed; }));
    CHECK(w.last("c1")->mine && w.last("c1")->text == "Hello from the phone" && w.last("c1")->id == pid);
    CHECK(w.radio.radio.texts_taken() == 2 && w.radio.radio.sent_to_mesh().back().channel == 1);
    CHECK(w.until([&] { return w.shell.texts_from_us == 2; }));
    CHECK(w.phone.texts_from_us == 1);   // not echoed to the one who sent it

    // Someone on the mesh speaks: everyone hears, the history keeps it.
    uint32_t anna = w.radio.radio.node_by_short("ANNA");
    w.radio.radio.inject_text(anna, kBroadcast, 0, "Hi all");
    CHECK(w.until([&] { return w.last("c0") && w.last("c0")->text == "Hi all"; }));
    CHECK(!w.last("c0")->mine && w.last("c0")->from == anna && w.c().chat("c0")->unread == 1);
    CHECK(w.until([&] { return w.shell.texts_from_others == 1 && w.phone.texts_from_others == 1; }));

    // A direct message with its real ack; a traceroute's answer finds the client that asked.
    w.spin(350);
    std::string ka = chat_key_dm(anna);
    w.c().open_dm(anna);
    CHECK(w.c().send_text(ka, "Just you") != 0);
    CHECK(w.until([&] { return w.last(ka) && w.last(ka)->state == Delivery::Delivered; }));
    uint32_t mrk = w.radio.radio.node_by_short("MRK");
    CHECK(w.c().traceroute(mrk, long(std::time(nullptr))) != 0);
    CHECK(w.until([&] { return w.c().last_trace(mrk) && w.c().last_trace(mrk)->answered; }));
    CHECK(w.c().last_trace(mrk)->route.route.size() == 2);

    // A packet with no id of its own (the radio would number it) is still followed.
    ToRadio t;
    t.which = ToRadio::Packet;
    t.packet.to = kBroadcast;
    t.packet.want_ack = true;
    t.packet.hop_limit = 3;
    t.packet.has_decoded = true;
    t.packet.decoded.portnum = kPortText;
    t.packet.decoded.payload = "No id";
    w.spin(350);
    w.shell.raw(frame(encode(t)));
    CHECK(w.until([&] { return w.last("c0") && w.last("c0")->text == "No id" && w.last("c0")->state == Delivery::Relayed; }));
    CHECK(w.last("c0")->id != 0 && w.radio.radio.last_packet_id() == w.last("c0")->id);

    // The phone hangs up politely: only the phone goes.
    ToRadio bye;
    bye.which = ToRadio::Disconnect;
    w.phone.raw(frame(encode(bye)));
    CHECK(w.until([&] { return !w.phone.is_open(); }));
    w.spin(100);
    CHECK(w.hub->clients() == 2 && w.hub->radio_connected() && w.radio.accepted == 1);

    // The history is a file, private to its owner, that a new reader gets whole.
    struct stat st {};
    CHECK(::stat((w.dir + "/data/messages.log").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(::stat((w.dir + "/data").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    Journal j(w.dir + "/data/messages.log");
    std::vector<Record> all;
    bool reset = false;
    j.poll(all, reset);
    std::vector<Record> msgs = fold_records(all, 0);
    CHECK(msgs.size() == 5);
    if (msgs.size() == 5) {
        CHECK(msgs[0].text == "Hello from the app" && msgs[0].mine && msgs[0].state == Delivery::Relayed);
        CHECK(msgs[1].text == "Hello from the phone" && msgs[1].channel == 1);
        CHECK(msgs[2].text == "Hi all" && !msgs[2].mine && msgs[2].from == anna && msgs[2].hops == 0);
        CHECK(msgs[3].text == "Just you" && msgs[3].state == Delivery::Delivered && msgs[3].chat_key() == ka);
    }
}

void test_closed_app_and_restart() {
    FakeRadio::Options fo;
    fo.chatter = false;
    World w(fo);
    w.app->resume();
    CHECK(w.shell.open(w.hub->port()));
    CHECK(w.until([&] { return w.c().ready() && w.shell.client.ready(); }));
    uint32_t anna = w.radio.radio.node_by_short("ANNA");
    std::string ka = chat_key_dm(anna);

    // The app is closed. Messages keep arriving; the daemon keeps them.
    w.app->suspend();
    w.spin(100);
    CHECK(w.hub->clients() == 1);
    w.radio.radio.inject_text(anna, FakeRadio::kMe, 0, "Are you there?");
    w.radio.radio.inject_text(anna, kBroadcast, 0, "Anyone?");
    CHECK(w.until([&] { return w.shell.texts_from_others == 2; }));
    CHECK(!w.c().chat(ka));
    w.app->resume();
    CHECK(w.until([&] { return w.c().ready() && w.c().chat(ka) && w.last("c0"); }));
    CHECK(w.last(ka)->text == "Are you there?" && w.last("c0")->text == "Anyone?");
    CHECK(w.c().total_unread() == 2);
    w.c().mark_read(ka, long(std::time(nullptr)));
    w.app->save_marks();

    // A change to the radio settings restarts it under everyone. They all
    // come back by themselves, with the new settings.
    LoRaConfig l = w.c().lora();
    l.hop_limit = 5;
    w.c().set_lora(l);
    CHECK(w.until([&] { return w.radio.radio.restarts() == 1; }));
    CHECK(w.until([&] { return !w.hub->radio_connected(); }));
    CHECK(w.until([&] { return !w.shell.is_open(); }));
    CHECK(w.until([&] { return w.hub->radio_connected(); }));
    CHECK(w.shell.open(w.hub->port()));
    CHECK(w.until([&] { return w.c().ready() && w.shell.client.ready(); }, 15000));
    CHECK(w.c().lora().hop_limit == 5 && w.shell.client.lora().hop_limit == 5);
    CHECK(w.radio.kicked == 0);
    // Nothing was lost on the way, and what was read stays read.
    CHECK(w.last(ka) && w.last(ka)->text == "Are you there?" && w.c().chat(ka)->unread == 0 && w.c().total_unread() == 1);

    // Another run of the app (a restart of ZeroShell): the same history and marks.
    Session::Options so;
    so.port = w.hub->port();
    so.data_dir = w.dir + "/data";
    Session again(so);
    again.resume();
    long end = mono_ms() + 5000;
    while (mono_ms() < end && !(again.client().ready() && again.client().chat(ka))) {
        w.step();
        again.tick(long(std::time(nullptr)), mono_ms());
    }
    CHECK(again.client().ready() && again.client().chat(ka) && again.client().chat(ka)->msgs.size() == 1);
    CHECK(again.client().chat(ka)->unread == 0 && again.client().total_unread() == 1);

    // Deleting a chat in one is gone in the other, and stays gone.
    again.forget_chat(ka, long(std::time(nullptr)));
    CHECK(w.until([&] { return !w.c().chat(ka); }));
    CHECK(w.c().chat("c0") && w.c().chat("c0")->msgs.size() == 1);
}

void test_no_radio() {
    FakeRadio::Options fo;
    fo.chatter = false;
    World w(fo);
    w.radio.stop();   // meshtasticd isn't running at all
    w.app->resume();
    CHECK(w.shell.open(w.hub->port()));
    w.spin(1500);
    // Clients are let in and wait: nothing is refused, nothing is ready.
    CHECK(w.hub->clients() == 2 && !w.hub->radio_connected() && w.hub->radio_refused());
    CHECK(w.c().linked() && !w.c().ready() && w.app->waiting(long(std::time(nullptr)) + 30) >= 30);
    CHECK(w.c().send_text("c0", "into the void") == 0);
    // A client that sends anyway is told no, and the history says why.
    ToRadio t;
    t.which = ToRadio::Packet;
    t.packet.to = kBroadcast;
    t.packet.id = 0x5151;
    t.packet.want_ack = true;
    t.packet.has_decoded = true;
    t.packet.decoded.portnum = kPortText;
    t.packet.decoded.payload = "Hello?";
    w.shell.raw(frame(encode(t)));
    CHECK(w.until([&] { return w.shell.queue_refused == 1; }));
    CHECK(w.until([&] { return w.last("c0") != nullptr; }));
    CHECK(w.last("c0")->state == Delivery::Failed && w.last("c0")->error == kErrLocalNoRadio);

    // It starts: those who were waiting get their settings without asking again.
    w.radio.start_again();
    CHECK(w.until([&] { return w.c().ready() && w.shell.client.ready(); }, 15000));
    CHECK(w.hub->radio_connected() && !w.hub->radio_refused() && w.app->waiting(long(std::time(nullptr))) == 0);
    CHECK(w.c().send_text("c0", "now it works") != 0);
    CHECK(w.until([&] { return w.last("c0")->state == Delivery::Relayed; }));
}

void test_share_position() {
    FakeRadio::Options fo;
    fo.chatter = false;
    World w(fo);
    w.app->resume();
    CHECK(w.until([&] { return w.c().ready(); }));
    double lat = 0, lon = 0;
    // Off unless asked for: gpsd isn't even connected to.
    w.spin(1500);
    CHECK(!w.gps.watching && w.hub->positions_sent() == 0 && !w.radio.radio.own_position(lat, lon));
    {
        std::ofstream conf(w.dir + "/mesh.conf");
        conf << "# written by Mesh\nshare_position=1\n";
    }
    CHECK(w.until([&] { return w.hub->positions_sent() == 1; }, 12000));
    CHECK(w.gps.watching);
    CHECK(w.until([&] { return w.radio.radio.own_position(lat, lon); }));
    CHECK(lat > 46.6499 && lat < 46.6501 && lon > 8.5999 && lon < 8.6001);
    // Standing still: not sent again and again.
    w.spin(2500);
    CHECK(w.hub->positions_sent() == 1);
    // Off again: the GPS is let go.
    {
        std::ofstream conf(w.dir + "/mesh.conf");
        conf << "share_position=0\n";
    }
    CHECK(w.until([&] { return !w.gps.watching; }, 12000));

    double v = 0;
    CHECK(json_number("{\"class\":\"TPV\",\"mode\":3,\"lat\":-33.8688,\"lon\":151.2093}", "lat", v) && v < -33.86 && v > -33.87);
    CHECK(json_number("{\"mode\":3}", "mode", v) && v == 3);
    CHECK(!json_number("{\"mode\":3}", "lat", v) && !json_number("{\"lat\":\"x\"}", "lat", v));
}

void test_port_taken() {
    uint16_t port = 0;
    int fd = listen_on(port);
    Hub::Options o;
    o.listen_addr = "127.0.0.1";
    o.listen_port = port;
    o.quiet = true;
    Hub hub(o);
    std::string why;
    CHECK(!hub.start(&why) && !why.empty());
    ::close(fd);
}

}  // namespace

int main() {
    test_many_clients();
    test_closed_app_and_restart();
    test_no_radio();
    test_share_position();
    test_port_taken();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("mesh hub: ok\n");
    return 0;
}
