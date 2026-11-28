#include "apps/mesh/core/session.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps/mesh/core/text.h"

namespace zs::mesh {

Session::Session(const Options& o) : opt_(o) {
    if (!opt_.data_dir.empty()) {
        journal_.set_path(opt_.data_dir + "/messages.log");
        marks_path_ = opt_.data_dir + "/read.tsv";
        std::ifstream in(marks_path_);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            marks_saved_ = ss.str();
            client_.set_read_marks(marks_saved_);
        }
    }
}

Session::~Session() {
    save_marks();
    close_socket();
}

void Session::resume() {
    if (running_) return;
    running_ = true;
    started_ = 0;
    retry_at_ms_ = 0;
}

void Session::suspend() {
    if (!running_) return;
    running_ = false;
    save_marks();
    if (opt_.simulated) return;   // the made-up radio lives in this process: nothing to let go of
    close_socket();
    client_.link_down();
}

long Session::waiting(long now) const {
    if (!running_ || client_.ready() || !started_) return 0;
    return now - started_;
}

void Session::tick(long now, long now_ms) {
    if (!running_) return;
    if (!started_) started_ = now;
    if (client_.ready()) started_ = now;
    if (opt_.simulated) {
        pump_fake(now, now_ms);
    } else {
        pump_socket(now, now_ms);
        if (now_ms - journal_at_ms_ >= 200 || journal_at_ms_ == 0) {
            journal_at_ms_ = now_ms;
            read_journal();
        }
    }
    client_.tick(now);
}

// ---- The device: a socket and the journal

void Session::close_socket() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    connecting_ = false;
    out_.clear();
    in_.clear();
}

void Session::open_socket(long now_ms) {
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(opt_.port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        connecting_ = false;
        client_.link_up(std::time(nullptr));
    } else if (errno == EINPROGRESS) {
        connecting_ = true;
        connect_started_ms_ = now_ms;
    } else {
        close_socket();
    }
}

void Session::pump_socket(long now, long now_ms) {
    if (fd_ < 0) {
        if (now_ms < retry_at_ms_) return;
        retry_at_ms_ = now_ms + 2000;
        open_socket(now_ms);
        if (fd_ < 0) return;
    }
    if (connecting_) {
        int err = 0;
        socklen_t len = sizeof err;
        // Writable means the connect finished, one way or the other.
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd_, &w);
        timeval tv{0, 0};
        int r = fd_ < FD_SETSIZE ? ::select(fd_ + 1, nullptr, &w, nullptr, &tv) : -1;
        if (r == 0) {
            if (now_ms - connect_started_ms_ > 5000) {
                close_socket();
                client_.link_down();
            }
            return;
        }
        if (r < 0 || ::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            close_socket();
            return;
        }
        connecting_ = false;
        client_.link_up(now);
    }
    out_ += client_.take_out();
    while (!out_.empty()) {
        ssize_t n = ::send(fd_, out_.data(), out_.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            out_.erase(0, size_t(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            break;
        } else {
            close_socket();
            client_.link_down();
            return;
        }
    }
    char buf[4096];
    for (int rounds = 0; rounds < 16; ++rounds) {   // a bounded bite each tick: the screen comes first
        ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            in_.feed(buf, size_t(n));
            std::string payload;
            while (in_.next(payload)) client_.on_from_radio(payload, now);
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            close_socket();
            client_.link_down();
            retry_at_ms_ = now_ms + 1500;
        }
        break;
    }
}

void Session::read_journal() {
    if (journal_.path().empty()) return;
    std::vector<Record> recs;
    bool reset = false;
    journal_.poll(recs, reset);
    if (reset) client_.clear_chats();
    for (const Record& r : recs) client_.apply(r);
}

// ---- The simulator

void Session::seed(long now) {
    // Yesterday's talk, so the first thing on the screen isn't an empty list.
    uint32_t anna = fake_->node_by_short("ANNA"), base = fake_->node_by_short("BASE"), hill = fake_->node_by_short("HILL");
    uint32_t me = FakeRadio::kMe;
    uint32_t id = 0x100;
    auto msg = [&](long ago, uint32_t from, uint32_t to, uint32_t chan, const char* text, Delivery d = Delivery::Received) {
        Record r;
        r.kind = Record::Msg;
        r.time = now - ago;
        r.id = ++id;
        r.from = from;
        r.to = to;
        r.channel = chan;
        r.mine = from == me;
        r.state = r.mine ? d : Delivery::Received;
        r.text = text;
        r.hops = r.mine ? -1 : (from == base ? 1 : 0);
        r.snr_q = from == anna ? -13 : from == hill ? 26 : 0;
        r.rssi = from == anna ? -103 : from == hill ? -84 : 0;
        client_.apply(r);
        return r.id;
    };
    if (!anna || !base) return;
    msg(26 * 3600, base, kBroadcast, 0, "Morning all. Base Camp is on the air.");
    msg(25 * 3600, me, kBroadcast, 0, "Morning! Reading you fine from the valley.", Delivery::Relayed);
    uint32_t q = msg(5 * 3600, anna, kBroadcast, 0, "Anyone heading up to the hut today?");
    msg(5 * 3600 - 120, me, kBroadcast, 0, "Leaving at noon, back before dark.", Delivery::Relayed);
    Record like;
    like.kind = Record::Msg;
    like.time = now - 5 * 3600 + 200;
    like.id = ++id;
    like.from = anna;
    like.to = kBroadcast;
    like.reaction = true;
    like.reply_id = id - 1;
    like.text = "\xF0\x9F\x91\x8D";
    client_.apply(like);
    (void)q;
    msg(3 * 3600, anna, me, 0, "Bring the spare battery if you can.");
    msg(3 * 3600 - 60, me, anna, 0, "Will do.", Delivery::Delivered);
    msg(40 * 60, base, kBroadcast, 1, "Dinner at 7, bring your own mug.");
    client_.mark_read(chat_key_channel(0), now);
}

void Session::pump_fake(long now, long now_ms) {
    if (!fake_) {
        FakeRadio::Options fo = opt_.fake;
        fo.epoch = now;
        fake_ = std::make_unique<FakeRadio>(fo);
        fake_start_ms_ = now_ms;
        fake_up_at_ms_ = now_ms + 300;   // "connecting" for a moment, as on the device
    }
    long ms = now_ms - fake_start_ms_;
    std::vector<Record> recs;
    if (!fake_linked_) {
        if (now_ms < fake_up_at_ms_) return;
        fake_->reconnect();
        fake_linked_ = true;
        fake_in_.clear();
        client_.link_up(now);
    }
    std::string out = client_.take_out();
    if (!out.empty()) {
        Deframer d;
        d.feed(out.data(), out.size());
        std::string payload;
        while (d.next(payload)) {
            ToRadio t;
            if (decode(payload, t)) recorder_.on_to_radio(t, now, recs);
        }
        fake_->feed(out);
    }
    fake_->advance(ms);
    std::string in = fake_->take_out();
    if (!in.empty()) {
        fake_in_.feed(in.data(), in.size());
        std::string payload;
        while (fake_in_.next(payload)) {
            FromRadio f;
            if (decode(payload, f)) recorder_.on_from_radio(f, now, recs);
            client_.on_from_radio(payload, now);
        }
    }
    if (fake_->take_restart()) {
        fake_linked_ = false;
        fake_up_at_ms_ = now_ms + 2500;   // about what meshtasticd takes to come back
        recorder_.link_lost(now, recs);
        client_.link_down();
    }
    recorder_.tick(now, recs);
    for (const Record& r : recs) client_.apply(r);
    if (!seeded_ && client_.ready()) {
        seeded_ = true;
        if (opt_.seed_history) seed(now);
    }
}

// ---- History

void Session::forget_chat(const std::string& key, long now) {
    Record r;
    r.kind = Record::Deleted;
    r.time = now;
    r.key = key;
    if (!opt_.simulated) journal_.append(r);
    client_.apply(r);
}

void Session::save_marks() {
    if (marks_path_.empty()) return;
    std::string marks = client_.read_marks();
    if (marks == marks_saved_) return;
    std::string tmp = marks_path_ + ".tmp";
    ::mkdir(opt_.data_dir.c_str(), 0700);
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return;
    bool ok = ::write(fd, marks.data(), marks.size()) == ssize_t(marks.size());
    ::close(fd);
    if (ok && ::rename(tmp.c_str(), marks_path_.c_str()) == 0) marks_saved_ = marks;
    else ::unlink(tmp.c_str());
}

std::string Session::export_text(const std::string& key, long now) const {
    std::string out;
    for (const Chat& c : client_.chats()) {
        if (key != "*" && c.key != key) continue;
        if (c.msgs.empty()) continue;
        out += "== " + client_.chat_title(c) + (c.dm ? " (direct)" : " (channel)") + " ==\n";
        for (const Message& m : c.msgs) {
            time_t t = time_t(m.time);
            struct tm tmv {};
            localtime_r(&t, &tmv);
            char when[32];
            std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
            out += std::string(when) + "  " + (m.mine ? "You" : client_.node_full_name(m.from)) + ": " + m.text;
            if (m.mine && m.state == Delivery::Failed) out += std::string("  [not sent: ") + delivery_error_text(m.error) + "]";
            out += "\n";
            for (const Reaction& r : m.reactions)
                out += "                    " + client_.node_full_name(r.from) + " reacted " + r.text + "\n";
        }
        out += "\n";
    }
    (void)now;
    return out;
}

}  // namespace zs::mesh
