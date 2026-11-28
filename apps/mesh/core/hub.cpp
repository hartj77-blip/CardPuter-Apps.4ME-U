#include "apps/mesh/core/hub.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps/mesh/core/pb.h"
#include "apps/mesh/core/text.h"

namespace zs::mesh {

namespace {
constexpr size_t kMaxClientBacklog = 512 * 1024;   // a client that stopped reading
constexpr size_t kMaxRadioBacklog = 256 * 1024;
constexpr long kAskTimeoutMs = 30000;
constexpr long kHistoryKeep = 2000;                // messages per chat
constexpr long kCompactOver = 2 * 1024 * 1024;     // bytes

int connect_local(uint16_t port, bool& in_progress) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    in_progress = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) return fd;
    if (errno == EINPROGRESS) {
        in_progress = true;
        return fd;
    }
    ::close(fd);
    return -1;
}

// Send what the socket takes; false if the connection is gone.
bool flush(int fd, std::string& out) {
    while (!out.empty()) {
        ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            out.erase(0, size_t(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}
}  // namespace

bool json_number(const std::string& line, const char* key, double& value) {
    std::string k = std::string("\"") + key + "\":";
    size_t at = line.find(k);
    if (at == std::string::npos) return false;
    const char* p = line.c_str() + at + k.size();
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return false;
    value = v;
    return true;
}

Hub::Hub(const Options& o) : opt_(o) {
    if (!opt_.data_dir.empty()) journal_.set_path(opt_.data_dir + "/messages.log");
    std::random_device rd;
    packet_id_ = uint32_t(rd()) | 0x80000000u;
}

Hub::~Hub() {
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (up_fd_ >= 0) ::close(up_fd_);
    for (auto& c : clients_)
        if (c->fd >= 0) ::close(c->fd);
    gps_close();
}

void Hub::log(const char* fmt, ...) {
    if (opt_.quiet) return;
    va_list ap;
    va_start(ap, fmt);
    std::fputs("zeroos-meshd: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

long Hub::now_s() const { return long(std::time(nullptr)); }
long Hub::now_ms() const {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return long(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

int Hub::clients_synced() const {
    int n = 0;
    for (const auto& c : clients_)
        if (c->synced) ++n;
    return n;
}

bool Hub::start(std::string* why) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        if (why) *why = std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(opt_.listen_port);
    if (::inet_pton(AF_INET, opt_.listen_addr.c_str(), &a.sin_addr) != 1) a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(listen_fd_, 8) != 0) {
        if (why) *why = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    socklen_t len = sizeof a;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    // Tidy the history before anyone reads it.
    if (journal_.compact(size_t(kHistoryKeep), size_t(kCompactOver))) log("history compacted");
    last_compact_s_ = now_s();
    read_conf();
    log("listening on port %u, radio on port %u", unsigned(port_), unsigned(opt_.radio_port));
    return true;
}

// ---- The radio side

void Hub::radio_connect() {
    up_fd_ = connect_local(opt_.radio_port, up_connecting_);
    up_connect_ms_ = now_ms();
    if (up_fd_ < 0) {
        up_refused_ = true;
        up_retry_ms_ = now_ms() + 1000;
        return;
    }
    if (!up_connecting_) {
        up_refused_ = false;
        asks_.push_front({0, 0});
        next_ask();
    }
}

void Hub::radio_lost(const char* why) {
    if (up_fd_ >= 0) ::close(up_fd_);
    bool was = up_ready_;
    up_fd_ = -1;
    up_connecting_ = false;
    up_ready_ = false;
    up_in_.clear();
    up_out_.clear();
    asking_ = false;
    asks_.clear();
    up_retry_ms_ = now_ms() + 1000;
    // Everyone starts again from a clean slate when it's back: a client that
    // reconnects asks for the settings afresh, which may have changed.
    for (auto& c : clients_) c->closing = true;
    std::vector<Record> recs;
    recorder_.link_lost(now_s(), recs);
    record(recs);
    if (was) log("radio connection lost (%s)", why);
}

void Hub::radio_send(const std::string& fr) {
    if (up_fd_ < 0 || up_connecting_) return;
    if (up_out_.size() + fr.size() > kMaxRadioBacklog) {
        radio_lost("it stopped reading");
        return;
    }
    up_out_ += fr;
    up_last_tx_s_ = now_s();
    radio_write();
}

void Hub::radio_write() {
    if (up_fd_ < 0 || up_connecting_) return;
    if (!flush(up_fd_, up_out_)) radio_lost("write failed");
}

void Hub::radio_read() {
    char buf[8192];
    for (int rounds = 0; rounds < 32 && up_fd_ >= 0; ++rounds) {
        ssize_t n = ::recv(up_fd_, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            up_in_.feed(buf, size_t(n));
            std::string payload;
            while (up_fd_ >= 0 && up_in_.next(payload)) on_radio(payload);
            continue;
        }
        if (n == 0) radio_lost("closed");
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) radio_lost(std::strerror(errno));
        break;
    }
}

void Hub::next_ask() {
    if (asking_ || up_fd_ < 0 || up_connecting_) return;
    while (!asks_.empty()) {
        Ask a = asks_.front();
        // Clients wait until the daemon itself knows the radio.
        if (a.client != 0 && !up_ready_) return;
        asks_.pop_front();
        if (a.client != 0 && !peer(a.client)) continue;
        if (a.client == 0) {
            std::random_device rd;
            own_nonce_ = (uint32_t(rd()) & 0x7fffffffu) | 0x10000u;   // never 69420 or 69421
            a.nonce = own_nonce_;
        }
        ToRadio t;
        t.which = ToRadio::WantConfig;
        t.want_config_id = a.nonce;
        current_ = a;
        asking_ = true;
        ask_started_ms_ = now_ms();
        radio_send(frame(encode(t)));
        return;
    }
}

void Hub::on_radio(const std::string& payload) {
    FromRadio f;
    if (!decode(payload, f)) return;
    std::vector<Record> recs;
    recorder_.on_from_radio(f, now_s(), recs);
    record(recs);
    std::string fr = frame(payload);
    if (f.is_config() && asking_) {
        bool done = f.which == FromRadio::ConfigComplete;
        if (current_.client == 0) {
            if (done && f.config_complete_id == own_nonce_) {
                up_ready_ = true;
                asking_ = false;
                log("radio ready: node %s", node_id(recorder_.my_num()).c_str());
                next_ask();
            }
            return;
        }
        Peer* p = peer(current_.client);
        if (p) client_send(*p, fr);
        if (done) {
            if (p) p->synced = true;
            asking_ = false;
            next_ask();
        }
        return;
    }
    for (auto& c : clients_)
        if (c->synced && !c->closing) client_send(*c, fr);
}

// ---- The client side

Hub::Peer* Hub::peer(int id) {
    for (auto& c : clients_)
        if (c->id == id && !c->closing) return c.get();
    return nullptr;
}

void Hub::accept_client() {
    for (;;) {
        int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        if (int(clients_.size()) >= opt_.max_clients) {
            ::close(fd);
            continue;
        }
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        auto p = std::make_unique<Peer>();
        p->fd = fd;
        p->id = next_client_++;
        clients_.push_back(std::move(p));
    }
}

void Hub::client_send(Peer& c, const std::string& fr) {
    if (c.closing) return;
    if (c.out.size() + fr.size() > kMaxClientBacklog) {
        c.closing = true;   // it stopped reading; it can connect again
        return;
    }
    c.out += fr;
    client_write(c);
}

void Hub::client_write(Peer& c) {
    if (c.fd < 0 || c.closing) return;
    if (!flush(c.fd, c.out)) c.closing = true;
}

void Hub::client_read(Peer& c) {
    char buf[4096];
    for (int rounds = 0; rounds < 8 && !c.closing; ++rounds) {
        ssize_t n = ::recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            c.in.feed(buf, size_t(n));
            std::string payload;
            while (!c.closing && c.in.next(payload)) on_client(c, payload);
            if (c.in.pending() > 64 * 1024) c.closing = true;   // not speaking the protocol
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) c.closing = true;
        break;
    }
}

void Hub::on_client(Peer& c, const std::string& payload) {
    ToRadio t;
    if (!decode(payload, t)) return;
    switch (t.which) {
    case ToRadio::WantConfig:
        asks_.push_back({c.id, t.want_config_id});
        next_ask();
        return;
    case ToRadio::Disconnect:
        // For this client only: passed on, it would end our own connection.
        c.closing = true;
        return;
    case ToRadio::Packet: {
        std::string body = payload;
        if (t.packet.id == 0) {
            // The radio would number it itself; we need the number to follow
            // what becomes of it. Set it in the bytes as they are.
            if (++packet_id_ == 0) ++packet_id_;
            t.packet.id = packet_id_;
            pb::Writer w;
            w.bytes_always(1, pb::with_fixed32(pb::find_bytes(payload, 1), 6, t.packet.id));
            body = w.take();
        }
        std::vector<Record> recs;
        recorder_.on_to_radio(t, now_s(), recs);
        if (up_fd_ < 0 || up_connecting_) {
            // No radio to give it to: say so, as the radio's queue would.
            FromRadio q;
            q.which = FromRadio::QueueStatusMsg;
            q.queue_status.res = 33;
            q.queue_status.mesh_packet_id = t.packet.id;
            recorder_.on_from_radio(q, now_s(), recs);
            record(recs);
            client_send(c, frame(encode(q)));
            return;
        }
        record(recs);
        radio_send(frame(body));
        // The other clients see what this one said (the radio doesn't tell them).
        if (t.packet.has_decoded && is_chat_port(t.packet.decoded.portnum) && recorder_.my_num()) {
            std::string pkt(pb::find_bytes(body, 1));
            pkt = pb::with_fixed32(pkt, 1, recorder_.my_num());
            pkt = pb::with_fixed32(pkt, 7, uint32_t(now_s()));
            pb::Writer w;
            w.bytes_always(2, pkt);
            std::string echo = frame(w.str());
            for (auto& o : clients_)
                if (o.get() != &c && o->synced && !o->closing) client_send(*o, echo);
        }
        return;
    }
    default:
        radio_send(frame(payload));   // a heartbeat, a file transfer, MQTT through the client
        return;
    }
}

void Hub::drop_closing() {
    for (auto& c : clients_) {
        if (!c->closing) continue;
        if (c->fd >= 0) ::close(c->fd);
        c->fd = -1;
        if (asking_ && current_.client == c->id) {
            // The radio is still answering it; let that finish, unheard.
        }
    }
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const std::unique_ptr<Peer>& c) { return c->closing; }),
                   clients_.end());
}

// ---- History, settings, GPS

void Hub::record(std::vector<Record>& recs) {
    for (const Record& r : recs) journal_.append(r);
    recs.clear();
}

void Hub::read_conf() {
    conf_checked_s_ = now_s();
    if (opt_.conf_path.empty()) return;
    struct stat st {};
    if (::stat(opt_.conf_path.c_str(), &st) != 0) {
        if (conf_mtime_ != 0) {
            conf_mtime_ = 0;
            share_position_ = false;
        }
        return;
    }
    long stamp = long(st.st_mtime) * 1000 + long(st.st_mtim.tv_nsec / 1000000) + long(st.st_size);
    if (stamp == conf_mtime_) return;
    conf_mtime_ = stamp;
    bool share = false;
    std::ifstream in(opt_.conf_path);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("share_position=", 0) == 0) share = line.size() > 15 && line[15] == '1';
    }
    if (share != share_position_) log("share my position: %s", share ? "on" : "off");
    share_position_ = share;
    if (share) gps_retry_s_ = 0;
}

void Hub::gps_close() {
    if (gps_fd_ >= 0) ::close(gps_fd_);
    gps_fd_ = -1;
    gps_connecting_ = false;
    gps_in_.clear();
    gps_fix_ = false;
}

void Hub::gps_line(const std::string& line) {
    if (line.find("\"class\":\"TPV\"") == std::string::npos) return;
    double mode = 0, lat = 0, lon = 0, alt = 0;
    if (!json_number(line, "mode", mode) || mode < 2 || !json_number(line, "lat", lat) || !json_number(line, "lon", lon)) {
        if (json_number(line, "mode", mode) && mode < 2) gps_fix_ = false;
        return;
    }
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) return;
    gps_fix_ = true;
    gps_fix_s_ = now_s();
    gps_lat_ = lat;
    gps_lon_ = lon;
    gps_has_alt_ = json_number(line, "altMSL", alt) || json_number(line, "alt", alt);
    gps_alt_ = alt;
}

void Hub::gps_step() {
    if (!share_position_) {
        if (gps_fd_ >= 0) gps_close();
        return;
    }
    long now = now_s();
    if (gps_fd_ < 0) {
        if (now < gps_retry_s_) return;
        gps_retry_s_ = now + 30;
        gps_fd_ = connect_local(opt_.gpsd_port, gps_connecting_);
        if (gps_fd_ < 0) return;
        if (!gps_connecting_) {
            std::string watch = "?WATCH={\"enable\":true,\"json\":true}\n";
            flush(gps_fd_, watch);
        }
        return;
    }
    if (!up_ready_ || !gps_fix_ || now - gps_fix_s_ > 30 || !recorder_.my_num()) return;
    // Every two minutes, or half a minute after moving a hundred metres.
    long since = now - gps_sent_s_;
    bool moved = gps_sent_any_ && distance_m(sent_lat_, sent_lon_, gps_lat_, gps_lon_) > 100;
    if (gps_sent_any_ && since < 30) return;
    if (gps_sent_any_ && since < 120 && !moved) return;
    Position p;
    p.has_latlon = true;
    p.lat_i = int32_t(std::lround(gps_lat_ * 1e7));
    p.lon_i = int32_t(std::lround(gps_lon_ * 1e7));
    p.has_alt = gps_has_alt_;
    p.alt = int32_t(std::lround(gps_alt_));
    p.time = uint32_t(now);
    p.location_source = 3;   // LOC_EXTERNAL
    ToRadio t;
    t.which = ToRadio::Packet;
    t.packet.to = recorder_.my_num();   // to our own node: "this is where you are"
    if (++packet_id_ == 0) ++packet_id_;
    t.packet.id = packet_id_;
    t.packet.priority = 10;
    t.packet.has_decoded = true;
    t.packet.decoded.portnum = kPortPosition;
    t.packet.decoded.payload = encode(p);
    radio_send(frame(encode(t)));
    gps_sent_s_ = now;
    gps_sent_any_ = true;
    sent_lat_ = gps_lat_;
    sent_lon_ = gps_lon_;
    ++positions_sent_;
}

void Hub::housekeeping() {
    long now = now_s();
    last_house_s_ = now;
    std::vector<Record> recs;
    recorder_.tick(now, recs);
    record(recs);
    // The radio drops a connection that says nothing for 15 minutes.
    if (up_ready_ && now - up_last_tx_s_ >= 120) {
        ToRadio t;
        t.which = ToRadio::Heartbeat;
        radio_send(frame(encode(t)));
    }
    if (now - conf_checked_s_ >= 3) read_conf();
    gps_step();
    if (now - last_compact_s_ >= 3600) {
        last_compact_s_ = now;
        if (journal_.compact(size_t(kHistoryKeep), size_t(kCompactOver))) log("history compacted");
    }
}

// ---- The loop

void Hub::step(int timeout_ms) {
    long ms = now_ms();
    if (up_fd_ < 0 && ms >= up_retry_ms_) radio_connect();
    if (asking_ && ms - ask_started_ms_ > kAskTimeoutMs) {
        // The radio never finished an answer: start over with it.
        radio_lost("no answer to a settings request");
    }

    std::vector<pollfd> fds;
    fds.push_back({listen_fd_, POLLIN, 0});
    size_t up_at = 0, gps_at = 0;
    if (up_fd_ >= 0) {
        up_at = fds.size();
        fds.push_back({up_fd_, short(POLLIN | ((up_connecting_ || !up_out_.empty()) ? POLLOUT : 0)), 0});
    }
    if (gps_fd_ >= 0) {
        gps_at = fds.size();
        fds.push_back({gps_fd_, short(POLLIN | (gps_connecting_ ? POLLOUT : 0)), 0});
    }
    size_t first_client = fds.size();
    for (auto& c : clients_) fds.push_back({c->fd, short(POLLIN | (c->out.empty() ? 0 : POLLOUT)), 0});
    size_t n_clients = clients_.size();

    int wait = timeout_ms;
    if (up_fd_ < 0) wait = std::min<long>(wait, std::max<long>(0, up_retry_ms_ - ms));
    ::poll(fds.data(), nfds_t(fds.size()), wait);

    if (fds[0].revents & POLLIN) accept_client();

    if (up_at && up_fd_ >= 0) {
        short ev = fds[up_at].revents;
        if (up_connecting_) {
            if (ev & (POLLOUT | POLLERR | POLLHUP)) {
                int err = 0;
                socklen_t len = sizeof err;
                ::getsockopt(up_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err != 0) {
                    ::close(up_fd_);
                    up_fd_ = -1;
                    up_connecting_ = false;
                    up_refused_ = true;
                    up_retry_ms_ = now_ms() + 1000;
                } else {
                    up_connecting_ = false;
                    up_refused_ = false;
                    asks_.push_front({0, 0});
                    next_ask();
                }
            } else if (now_ms() - up_connect_ms_ > 5000) {
                ::close(up_fd_);
                up_fd_ = -1;
                up_connecting_ = false;
                up_retry_ms_ = now_ms() + 1000;
            }
        } else {
            if (ev & (POLLIN | POLLERR | POLLHUP)) radio_read();
            if (up_fd_ >= 0 && (ev & POLLOUT)) radio_write();
        }
    }

    if (gps_at && gps_fd_ >= 0) {
        short ev = fds[gps_at].revents;
        if (gps_connecting_) {
            if (ev & (POLLOUT | POLLERR | POLLHUP)) {
                int err = 0;
                socklen_t len = sizeof err;
                ::getsockopt(gps_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err != 0) {
                    gps_close();
                } else {
                    gps_connecting_ = false;
                    std::string watch = "?WATCH={\"enable\":true,\"json\":true}\n";
                    flush(gps_fd_, watch);
                }
            }
        } else if (ev & (POLLIN | POLLERR | POLLHUP)) {
            char buf[4096];
            ssize_t n = ::recv(gps_fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                gps_in_.append(buf, size_t(n));
                size_t at = 0;
                for (;;) {
                    size_t nl = gps_in_.find('\n', at);
                    if (nl == std::string::npos) break;
                    gps_line(gps_in_.substr(at, nl - at));
                    at = nl + 1;
                }
                gps_in_.erase(0, at);
                if (gps_in_.size() > 65536) gps_in_.clear();
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                gps_close();
            }
        }
    }

    // The clients that were there when we polled (accept may have added some).
    for (size_t i = 0; i < n_clients && i < clients_.size(); ++i) {
        Peer& c = *clients_[i];
        short ev = fds[first_client + i].revents;
        if (c.closing || c.fd != fds[first_client + i].fd) continue;
        if (ev & (POLLIN | POLLERR | POLLHUP)) client_read(c);
        if (!c.closing && (ev & POLLOUT)) client_write(c);
    }
    drop_closing();

    if (now_s() != last_house_s_) housekeeping();
}

}  // namespace zs::mesh
