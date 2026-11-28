#include "apps/mesh/core/store.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apps/mesh/core/proto.h"

namespace zs::mesh {

const char* delivery_error_text(uint32_t error) {
    switch (error) {
    case kErrLocalRadioOff: return "Radio isn't transmitting: set your region";
    case kErrLocalNoRadio: return "No radio to send on";
    case kErrLocalQueue: return "The radio couldn't take it";
    case kErrLocalNoAnswer: return "No one confirmed it";
    default: return routing_error_text(error);
    }
}

std::string chat_key_channel(uint32_t channel) { return "c" + std::to_string(channel); }
std::string chat_key_dm(uint32_t node) {
    char b[16];
    std::snprintf(b, sizeof b, "d%08x", node);
    return b;
}
std::string Record::chat_key() const {
    if (kind == Deleted) return key;
    if (!direct()) return chat_key_channel(channel);
    return chat_key_dm(mine ? to : from);
}

static void escape_into(std::string& out, const std::string& s) {
    for (char ch : s) {
        switch (ch) {
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\\': out += "\\\\"; break;
        case '\0': break;
        default: out.push_back(ch);
        }
    }
}
static std::string unescape(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        char n = s[++i];
        out.push_back(n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n);
    }
    return out;
}

std::string format_record(const Record& r) {
    char b[160];
    std::string out;
    if (r.kind == Record::Msg) {
        std::string flags;
        if (r.mine) flags += 'm';
        if (r.pki) flags += 'p';
        if (r.via_mqtt) flags += 'q';
        if (r.reaction) flags += 'e';
        if (r.bell) flags += 'b';
        if (r.waypoint) flags += 'w';
        if (flags.empty()) flags = "-";
        std::snprintf(b, sizeof b, "M\t%ld\t%08x\t%08x\t%08x\t%u\t%s\t%d\t%u\t%d\t%d\t%d\t%08x\t", r.time, r.id, r.from,
                      r.to, r.channel, flags.c_str(), int(r.state), r.error, r.snr_q, r.rssi, r.hops, r.reply_id);
        out = b;
        escape_into(out, r.text);
    } else if (r.kind == Record::Ack) {
        std::snprintf(b, sizeof b, "A\t%ld\t%08x\t%d\t%u\t%08x", r.time, r.id, int(r.state), r.error, r.acker);
        out = b;
    } else {
        std::snprintf(b, sizeof b, "D\t%ld\t", r.time);
        out = b;
        escape_into(out, r.key);
    }
    out.push_back('\n');
    return out;
}

static std::vector<std::string_view> split_tabs(std::string_view line, size_t max) {
    std::vector<std::string_view> v;
    size_t at = 0;
    while (v.size() + 1 < max) {
        size_t t = line.find('\t', at);
        if (t == std::string_view::npos) break;
        v.push_back(line.substr(at, t - at));
        at = t + 1;
    }
    v.push_back(line.substr(at));
    return v;
}
static bool to_long(std::string_view s, long& v, int base = 10) {
    if (s.empty() || s.size() > 20) return false;
    char buf[24];
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    char* end = nullptr;
    errno = 0;
    v = std::strtol(buf, &end, base);
    return errno == 0 && end && *end == 0;
}
static bool to_u32_hex(std::string_view s, uint32_t& v) {
    if (s.empty() || s.size() > 8) return false;
    char buf[12];
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    char* end = nullptr;
    unsigned long x = std::strtoul(buf, &end, 16);
    v = uint32_t(x);
    return end && *end == 0;
}

bool parse_record(std::string_view line, Record& r) {
    r = Record{};
    if (line.size() < 3 || line[1] != '\t') return false;
    long n;
    if (line[0] == 'M') {
        auto f = split_tabs(line, 14);
        if (f.size() != 14) return false;
        long state, error, snr, rssi, hops, chan;
        if (!to_long(f[1], r.time) || !to_u32_hex(f[2], r.id) || !to_u32_hex(f[3], r.from) || !to_u32_hex(f[4], r.to) ||
            !to_long(f[5], chan) || !to_long(f[7], state) || !to_long(f[8], error) || !to_long(f[9], snr) ||
            !to_long(f[10], rssi) || !to_long(f[11], hops) || !to_u32_hex(f[12], r.reply_id))
            return false;
        if (state < 0 || state > int(Delivery::Failed) || chan < 0 || chan > 255) return false;
        r.kind = Record::Msg;
        r.channel = uint32_t(chan);
        r.state = Delivery(state);
        r.error = uint32_t(error);
        r.snr_q = int(snr);
        r.rssi = int(rssi);
        r.hops = int(hops);
        for (char ch : f[6]) {
            switch (ch) {
            case 'm': r.mine = true; break;
            case 'p': r.pki = true; break;
            case 'q': r.via_mqtt = true; break;
            case 'e': r.reaction = true; break;
            case 'b': r.bell = true; break;
            case 'w': r.waypoint = true; break;
            default: break;
            }
        }
        r.text = unescape(f[13]);
        return true;
    }
    if (line[0] == 'A') {
        auto f = split_tabs(line, 6);
        if (f.size() != 6) return false;
        long state, error;
        if (!to_long(f[1], r.time) || !to_u32_hex(f[2], r.id) || !to_long(f[3], state) || !to_long(f[4], error) ||
            !to_u32_hex(f[5], r.acker))
            return false;
        if (state < 0 || state > int(Delivery::Failed)) return false;
        r.kind = Record::Ack;
        r.state = Delivery(state);
        r.error = uint32_t(error);
        return true;
    }
    if (line[0] == 'D') {
        auto f = split_tabs(line, 3);
        if (f.size() != 3 || !to_long(f[1], n)) return false;
        r.kind = Record::Deleted;
        r.time = n;
        r.key = unescape(f[2]);
        return !r.key.empty();
    }
    return false;
}

bool advance_delivery(Delivery& state, uint32_t& error, Delivery news, uint32_t news_error) {
    if (news == Delivery::Received || state == Delivery::Received) return false;
    if (state == Delivery::Delivered) return false;                      // nothing beats it
    if (news == Delivery::Failed) {
        // Someone already passed it on: a later "no one confirmed" doesn't undo that.
        if (state == Delivery::Relayed || state == Delivery::Failed) return false;
        state = Delivery::Failed;
        error = news_error;
        return true;
    }
    if (state == Delivery::Failed) {
        if (news != Delivery::Delivered && news != Delivery::Relayed) return false;   // a late ack counts
    } else if (int(news) <= int(state)) {
        return false;
    }
    state = news;
    error = 0;
    return true;
}

std::vector<Record> fold_records(const std::vector<Record>& in, size_t keep_per_chat) {
    std::vector<Record> msgs;
    std::map<uint32_t, size_t> ours;   // id -> index in msgs, for acks
    for (const Record& r : in) {
        if (r.kind == Record::Msg) {
            if (r.mine) ours[r.id] = msgs.size();
            msgs.push_back(r);
        } else if (r.kind == Record::Ack) {
            auto it = ours.find(r.id);
            if (it != ours.end()) advance_delivery(msgs[it->second].state, msgs[it->second].error, r.state, r.error);
        } else if (r.kind == Record::Deleted) {
            for (Record& m : msgs)
                if (m.kind == Record::Msg && (r.key == "*" || m.chat_key() == r.key)) m.kind = 0;   // dropped below
        }
    }
    std::map<std::string, size_t> count;
    std::vector<char> keep(msgs.size(), 0);
    for (size_t i = msgs.size(); i-- > 0;) {
        if (msgs[i].kind != Record::Msg) continue;
        size_t& n = count[msgs[i].chat_key()];
        if (keep_per_chat == 0 || n < keep_per_chat) keep[i] = 1;
        ++n;
    }
    std::vector<Record> out;
    for (size_t i = 0; i < msgs.size(); ++i)
        if (keep[i]) out.push_back(std::move(msgs[i]));
    return out;
}

// ---- Journal
Journal::~Journal() { close_reader(); }

void Journal::set_path(std::string path) {
    close_reader();
    path_ = std::move(path);
}

void Journal::close_reader() {
    if (rfd_ >= 0) ::close(rfd_);
    rfd_ = -1;
    rpos_ = 0;
    rino_ = 0;
    partial_.clear();
}

static void make_parents(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        std::string dir = path.substr(0, i);
        ::mkdir(dir.c_str(), 0700);   // exists already, nearly always
    }
}

bool Journal::append(const Record& r) {
    if (path_.empty()) return false;
    std::string line = format_record(r);
    int fd = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0 && errno == ENOENT) {
        make_parents(path_);
        fd = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    }
    if (fd < 0) return false;
    // One write per line: with O_APPEND two writers never interleave.
    ssize_t n = ::write(fd, line.data(), line.size());
    ::close(fd);
    return n == ssize_t(line.size());
}

void Journal::poll(std::vector<Record>& out, bool& reset) {
    reset = false;
    if (path_.empty()) return;
    struct stat st {};
    if (::stat(path_.c_str(), &st) != 0) {
        if (rfd_ >= 0) {   // it was there and now isn't
            close_reader();
            reset = true;
        }
        return;
    }
    if (rfd_ >= 0 && (rino_ != (unsigned long long)st.st_ino || st.st_size < rpos_)) {
        close_reader();   // replaced (compacted) or cut short
        reset = true;
    }
    if (rfd_ < 0) {
        rfd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        if (rfd_ < 0) return;
        struct stat fs {};
        ::fstat(rfd_, &fs);
        rino_ = (unsigned long long)fs.st_ino;
        rpos_ = 0;
    }
    if (st.st_size == rpos_) return;
    char buf[8192];
    for (;;) {
        ssize_t n = ::pread(rfd_, buf, sizeof buf, rpos_);
        if (n <= 0) break;
        rpos_ += n;
        partial_.append(buf, size_t(n));
        size_t at = 0;
        for (;;) {
            size_t nl = partial_.find('\n', at);
            if (nl == std::string::npos) break;
            Record r;
            if (parse_record(std::string_view(partial_).substr(at, nl - at), r)) out.push_back(std::move(r));
            at = nl + 1;
        }
        partial_.erase(0, at);
        if (partial_.size() > 65536) partial_.clear();   // not a line: not our file
    }
}

bool Journal::compact(size_t keep, size_t min_bytes) {
    if (path_.empty()) return false;
    struct stat st {};
    if (::stat(path_.c_str(), &st) != 0 || size_t(st.st_size) < min_bytes) return false;
    Journal reader(path_);
    std::vector<Record> all;
    bool reset = false;
    reader.poll(all, reset);
    std::vector<Record> kept = fold_records(all, keep);
    std::string tmp = path_ + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::string buf;
    bool ok = true;
    for (const Record& r : kept) {
        buf += format_record(r);
        if (buf.size() > 32768) {
            ok = ok && ::write(fd, buf.data(), buf.size()) == ssize_t(buf.size());
            buf.clear();
        }
    }
    ok = ok && ::write(fd, buf.data(), buf.size()) == ssize_t(buf.size());
    ok = ok && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), path_.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace zs::mesh
