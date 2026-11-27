// USB live sound (README.md): a made-up USB stick whose songs are the sound
// the Cardputer is playing right now.
//
// A car stereo only plays files from a stick, so the stick is invented as
// the car reads it: a partition table, a FAT32 volume and three long MP3
// files, LIVE1.MP3 to LIVE3.MP3. Nothing of it is stored. Whatever part of
// a song the car reads, it gets the MP3 stream the encoder is producing,
// from a few seconds back (the backlog), so the car's first gulp is served
// at once and after that it is fed as fast as the sound is made.
//
// The car's buttons are read from how it reads: starting another file is
// next or previous, starting this one again is previous, and no reads for a
// while is pause.
//
// No USB, no FUSE and no clock in here, so tests/live_stick_test.cpp runs
// it on any machine. main.cpp is the program around it.
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace car_live {

// --- MP3 frames ---------------------------------------------------------

// The length in bytes of the MPEG audio Layer III frame that starts with
// these four bytes, or 0 if they aren't a frame header.
inline int mp3_frame_bytes(const uint8_t* h) {
    if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0) return 0;
    int version = (h[1] >> 3) & 3;   // 3: MPEG-1, 2: MPEG-2, 0: MPEG-2.5
    int layer = (h[1] >> 1) & 3;     // 1: Layer III
    int bitrate = (h[2] >> 4) & 15, rate = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (version == 1 || layer != 1 || bitrate == 0 || bitrate == 15 || rate == 3) return 0;
    static const int kKbps1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int kKbps2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int kRate1[] = {44100, 48000, 32000};
    if (version == 3) return 144000 * kKbps1[bitrate] / kRate1[rate] + pad;
    return 72000 * kKbps2[bitrate] / (kRate1[rate] >> (version == 2 ? 1 : 2)) + pad;
}

// One frame of silence at 44.1 kHz stereo: a header and nothing after it
// (no bits for either granule decodes to silence). Played while the encoder
// has nothing to give, so the car's song never runs dry.
inline std::vector<uint8_t> silent_frame(int kbps) {
    static const int kKbps1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    int index = 9;   // 128, if kbps isn't a real rate
    for (int i = 1; i < 15; ++i)
        if (kKbps1[i] == kbps) index = i;
    std::vector<uint8_t> f(size_t(144000 * kKbps1[index] / 44100), 0);
    f[0] = 0xff;
    f[1] = 0xfb;                      // MPEG-1 Layer III, no checksum
    f[2] = uint8_t(index << 4);       // the bit rate, 44.1 kHz, no padding
    f[3] = 0x44;                      // joint stereo, original
    return f;
}

// --- The encoder's output, the last couple of megabytes of it ------------

// Positions are counted from the first byte ever pushed. One thread pushes
// (the encoder's pipe), another reads (the car).
class Stream {
public:
    explicit Stream(size_t capacity = size_t(1) << 21) : buf_(capacity) {}

    void push(const uint8_t* p, size_t n) {
        std::lock_guard<std::mutex> lock(m_);
        for (size_t i = 0; i < n; ++i) buf_[size_t((head_ + i) % buf_.size())] = p[i];
        head_ += n;
        // Where each frame starts, so a car that jumps in gets whole frames.
        uint8_t h[4];
        while (scan_ + 4 <= head_) {
            if (scan_ < tail_locked()) scan_ = tail_locked();
            for (int i = 0; i < 4; ++i) h[i] = buf_[size_t((scan_ + uint64_t(i)) % buf_.size())];
            int len = mp3_frame_bytes(h);
            if (len > 0) {
                frames_.push_back(scan_);
                scan_ += uint64_t(len);
            } else {
                ++scan_;   // not a frame here: look for the next one
            }
        }
        while (!frames_.empty() && frames_.front() < tail_locked()) frames_.pop_front();
        grown_.notify_all();
    }

    uint64_t head() const {
        std::lock_guard<std::mutex> lock(m_);
        return head_;
    }

    // The last frame that starts at or before `pos`. If `pos` is older than
    // anything still held, the oldest frame held; with no frames at all,
    // `pos` itself.
    uint64_t frame_before(uint64_t pos) const {
        std::lock_guard<std::mutex> lock(m_);
        auto it = std::upper_bound(frames_.begin(), frames_.end(), pos);
        if (it != frames_.begin()) return *(it - 1);
        return frames_.empty() ? std::max(pos, tail_locked()) : frames_.front();
    }

    // Waits until byte `pos` has been pushed. False if it hasn't in time.
    bool wait_head(uint64_t pos, int timeout_ms) const {
        std::unique_lock<std::mutex> lock(m_);
        return grown_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return head_ >= pos; });
    }

    // Copies [pos, pos + n). Bytes not held (too old, or not made yet) are
    // zeros, which a player skips over.
    void read(uint64_t pos, uint8_t* out, size_t n) const {
        std::lock_guard<std::mutex> lock(m_);
        uint64_t tail = tail_locked();
        for (size_t i = 0; i < n; ++i) {
            uint64_t at = pos + i;
            out[i] = at >= tail && at < head_ ? buf_[size_t(at % buf_.size())] : 0;
        }
    }

private:
    uint64_t tail_locked() const { return head_ > buf_.size() ? head_ - buf_.size() : 0; }

    mutable std::mutex m_;
    mutable std::condition_variable grown_;
    std::vector<uint8_t> buf_;
    uint64_t head_ = 0;
    uint64_t scan_ = 0;            // where the next frame header is expected
    std::deque<uint64_t> frames_;  // where the frames still held start
};

// --- The stick ----------------------------------------------------------

struct Config {
    int kbps = 320;          // the encoder's rate; a higher rate also means less delay
    int backlog_ms = 3000;   // how far behind the sound the car starts reading
    int patience_ms = 1500;  // extra wait for the encoder before giving up on a read
    int pause_ms = 5000;     // no reads for this long while playing: the car paused
};

enum class Event { Next, Previous, Pause, Play };

// The geometry. Sectors are 512 bytes.
constexpr uint32_t kSector = 512;
constexpr uint32_t kPartStart = 2048;         // the partition starts at 1 MiB
constexpr uint32_t kReserved = 32;            // sectors before the first FAT
constexpr uint32_t kClusterSectors = 32;      // 16 KiB clusters
constexpr uint32_t kClusterBytes = kClusterSectors * kSector;
constexpr int kFiles = 3;
constexpr uint32_t kFileClusters = 32768;     // 512 MiB a file: 3 h 43 min at 320 kbit/s
constexpr uint32_t kSpareClusters = 63;
constexpr uint32_t kClusters = 1 + kFiles * kFileClusters + kSpareClusters;   // FAT32 needs 65525 or more
constexpr uint32_t kFatSectors = ((kClusters + 2) * 4 + kSector - 1) / kSector;
constexpr uint32_t kDataStart = kReserved + 2 * kFatSectors;                  // in the partition
constexpr uint32_t kPartSectors = kDataStart + kClusters * kClusterSectors;
constexpr uint64_t kFileBytes = uint64_t(kFileClusters) * kClusterBytes;
constexpr uint32_t kTagBytes = 512;           // each file starts with a name tag (ID3v2)
constexpr uint32_t kRootCluster = 2;
constexpr uint64_t kImageBytes = uint64_t(kPartStart + kPartSectors) * kSector;

inline uint32_t file_first_cluster(int file) { return kRootCluster + 1 + uint32_t(file) * kFileClusters; }

class LiveStick {
public:
    LiveStick(Stream& stream, Config config) : stream_(stream), cfg_(config) {}

    uint64_t size() const { return kImageBytes; }

    // Bytes [off, off + n) of the stick. Reading a song may wait for the
    // encoder, about as long as those bytes take to play.
    void read(uint64_t off, uint8_t* out, size_t n, int64_t now_ms) {
        const uint64_t data0 = uint64_t(kPartStart + kDataStart + kClusterSectors) * kSector;   // after the root folder
        const uint64_t data1 = data0 + kFiles * kFileBytes;
        while (n > 0) {
            size_t take;
            if (off >= data0 && off < data1) {
                int file = int((off - data0) / kFileBytes);
                uint64_t in_file = (off - data0) % kFileBytes;
                take = size_t(std::min<uint64_t>(n, kFileBytes - in_file));
                read_song(file, in_file, out, take, now_ms);
            } else {
                take = size_t(std::min<uint64_t>(n, off < data0 ? data0 - off : n));
                read_layout(off, out, take);
            }
            off += take;
            out += take;
            n -= take;
        }
    }

    // Call a few times a second: notices that the car stopped reading.
    void tick(int64_t now_ms) {
        if ((playing_ || armed_) && !paused_ && now_ms - last_read_ms_ > cfg_.pause_ms) {
            paused_ = true;
            playing_ = armed_ = false;
            steps_ = 0;
            run_.bytes = 0;   // carrying on has to be read on end again before it counts
            events_.push_back(Event::Pause);
        }
    }

    std::vector<Event> take_events() {
        std::vector<Event> e;
        e.swap(events_);
        return e;
    }

    // The car is playing a song (reading on and on), not just looking.
    bool car_playing() const { return playing_; }

private:
    int byte_rate() const { return cfg_.kbps * 125; }
    uint64_t backlog_bytes() const { return uint64_t(byte_rate()) * uint64_t(cfg_.backlog_ms) / 1000; }
    // This much read on end is the car playing a song, not a look at its
    // name: the backlog, which it gets at once, and a second more, which it
    // had to wait for. A car that only looks (or a computer reading ahead)
    // stops well before that.
    uint64_t sure_bytes() const { return backlog_bytes() + uint64_t(byte_rate()); }

    // One stretch of a song being read on end.
    struct Run {
        int file = -1;
        uint64_t next = 0;    // where in the file the next read is expected
        uint64_t bytes = 0;   // read on end so far
    };

    void read_song(int file, uint64_t in_file, uint8_t* out, size_t n, int64_t now_ms) {
        // A read from the file's first byte starts a stretch "from the start".
        if (in_file == 0) begin_run(file, 0, true);
        else if (file != run_.file || in_file != run_.next) {
            // The car jumped. Back to where it was playing a moment ago (it
            // looked at another file and came back) carries on as if nothing
            // happened; anything else starts a new stretch.
            if (armed_ && file == left_.file && in_file == left_.next) {
                run_ = left_;
                armed_ = false;
                steps_ = 0;
            } else {
                begin_run(file, in_file, false);
            }
        }
        last_read_ms_ = now_ms;
        run_.next = in_file + n;
        run_.bytes += n;

        // The tag at the start of each file is fixed.
        if (in_file < kTagBytes) {
            size_t t = size_t(std::min<uint64_t>(n, kTagBytes - in_file));
            uint8_t tag[kTagBytes];
            make_tag(tag);
            memcpy(out, tag + in_file, t);
            out += t;
            n -= t;
        }
        if (n > 0) {
            // Fallen behind (the car stopped reading for a while): catch up.
            uint64_t head = stream_.head();
            if (head > cursor_ && head - cursor_ > 2 * backlog_bytes() + uint64_t(byte_rate())) seat_cursor();
            uint64_t want = cursor_ + n;
            head = stream_.head();
            if (head < want) stream_.wait_head(want, int((want - head) * 1000 / uint64_t(byte_rate())) + cfg_.patience_ms);
            stream_.read(cursor_, out, n);
            cursor_ = want;
        }
        if (run_.bytes >= sure_bytes()) car_is_playing();
    }

    // The car starts reading somewhere else. If it was playing, that may be
    // a button: each jump to a file's start counts as a step forward or back
    // (two quick presses of Next pass through a file), and the steps are
    // sent once the car has read enough of where it ends up to be playing it
    // (car_is_playing).
    void begin_run(int file, uint64_t in_file, bool from_start) {
        bool first = playing_;   // the first jump away from the song that was playing
        if (first) {
            left_ = run_;
            armed_ = true;
            steps_ = 0;
            playing_ = false;
        }
        if (armed_ && from_start) {
            int before = run_.file;
            // Read to the end and then another file (or this one again, on
            // repeat): the song ran out, nobody pressed a button.
            bool ran_out = first && run_.next + backlog_bytes() >= kFileBytes;
            if (ran_out) {
            } else if (file == (before + 1) % kFiles) {
                ++steps_;
            } else if (file != before) {
                --steps_;
            } else if (first) {
                --steps_;   // this song again from the start: "previous"
            }
        }
        run_.file = file;
        run_.next = in_file;
        run_.bytes = 0;
        seat_cursor();
    }

    // The car starts `backlog` behind the sound, on a whole frame.
    void seat_cursor() {
        uint64_t head = stream_.head();
        cursor_ = stream_.frame_before(head > backlog_bytes() ? head - backlog_bytes() : 0);
    }

    void car_is_playing() {
        if (playing_) return;
        playing_ = true;
        if (paused_) {
            paused_ = false;
            events_.push_back(Event::Play);
        }
        for (int i = 0; armed_ && i < (steps_ < 0 ? -steps_ : steps_); ++i)
            events_.push_back(steps_ > 0 ? Event::Next : Event::Previous);
        armed_ = false;
        steps_ = 0;
    }

    // --- Everything that isn't a song: computed, the same every time.
    void read_layout(uint64_t off, uint8_t* out, size_t n) const {
        memset(out, 0, n);
        // Sector by sector: the request rarely covers more than a few.
        uint64_t first = off / kSector, last = (off + n - 1) / kSector;
        uint8_t sector[kSector];
        for (uint64_t s = first; s <= last; ++s) {
            if (!make_sector(s, sector)) continue;
            uint64_t s0 = s * kSector;
            uint64_t from = std::max(off, s0), to = std::min(off + n, s0 + kSector);
            memcpy(out + (from - off), sector + (from - s0), size_t(to - from));
        }
    }

    static void put16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
    static void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

    // Sector `s` of the stick into `b`. False if it is all zeros.
    static bool make_sector(uint64_t s, uint8_t* b) {
        memset(b, 0, kSector);
        if (s == 0) {   // the partition table: one FAT32 partition
            put32(b + 440, 0x5a4c4956);
            uint8_t* e = b + 446;
            e[1] = 0xfe; e[2] = 0xff; e[3] = 0xff;   // "use the sector numbers"
            e[4] = 0x0c;                              // FAT32
            e[5] = 0xfe; e[6] = 0xff; e[7] = 0xff;
            put32(e + 8, kPartStart);
            put32(e + 12, kPartSectors);
            b[510] = 0x55; b[511] = 0xaa;
            return true;
        }
        if (s < kPartStart) return false;
        uint64_t p = s - kPartStart;   // the sector within the partition
        if (p == 0 || p == 6) {        // the boot sector, and its copy
            b[0] = 0xeb; b[1] = 0x58; b[2] = 0x90;
            memcpy(b + 3, "MSDOS5.0", 8);
            put16(b + 11, kSector);
            b[13] = uint8_t(kClusterSectors);
            put16(b + 14, kReserved);
            b[16] = 2;                 // two FATs
            b[21] = 0xf8;              // a fixed disk
            put16(b + 24, 63);
            put16(b + 26, 255);
            put32(b + 28, kPartStart);
            put32(b + 32, kPartSectors);
            put32(b + 36, kFatSectors);
            put32(b + 44, kRootCluster);
            put16(b + 48, 1);          // where the FSInfo sector is
            put16(b + 50, 6);          // where the copy of this one is
            b[64] = 0x80;
            b[66] = 0x29;
            put32(b + 67, 0x4c495645);
            memcpy(b + 71, "LIVE       ", 11);
            memcpy(b + 82, "FAT32   ", 8);
            b[510] = 0x55; b[511] = 0xaa;
            return true;
        }
        if (p == 1 || p == 7) {        // FSInfo: how much is free
            put32(b, 0x41615252);
            put32(b + 484, 0x61417272);
            put32(b + 488, kSpareClusters);
            put32(b + 492, kClusters + 2 - kSpareClusters);
            b[510] = 0x55; b[511] = 0xaa;
            return true;
        }
        if (p < kReserved) return false;
        if (p < kDataStart) {          // the FATs: each file is one unbroken chain
            uint32_t first = uint32_t((p - kReserved) % kFatSectors) * (kSector / 4);
            for (uint32_t i = 0; i < kSector / 4; ++i) put32(b + i * 4, fat_entry(first + i));
            return true;
        }
        if (p == kDataStart) {         // the root folder: the volume's name and the songs
            dir_entry(b, "LIVE       ", 0x08, 0, 0);
            for (int f = 0; f < kFiles; ++f) {
                char name[12] = "LIVE1   MP3";
                name[4] = char('1' + f);
                dir_entry(b + 32 * (f + 1), name, 0x21, file_first_cluster(f), uint32_t(kFileBytes));
            }
            return true;
        }
        return false;
    }

    static uint32_t fat_entry(uint32_t cluster) {
        const uint32_t kEnd = 0x0fffffff;
        if (cluster == 0) return 0x0ffffff8;
        if (cluster == 1 || cluster == kRootCluster) return kEnd;
        uint32_t i = cluster - (kRootCluster + 1);
        if (i >= uint32_t(kFiles) * kFileClusters) return 0;   // spare, or past the end
        return (i + 1) % kFileClusters == 0 ? kEnd : cluster + 1;
    }

    static void dir_entry(uint8_t* e, const char* name83, uint8_t attr, uint32_t cluster, uint32_t size) {
        memcpy(e, name83, 11);
        e[11] = attr;
        const uint32_t date = ((2026 - 1980) << 9) | (1 << 5) | 1, time = 12 << 11;   // 2026-01-01 12:00
        put16(e + 14, time); put16(e + 16, date); put16(e + 18, date);
        put16(e + 22, time); put16(e + 24, date);
        put16(e + 20, cluster >> 16);
        put16(e + 26, cluster & 0xffff);
        put32(e + 28, size);
    }

    // An ID3v2.3 tag, so a car's screen says "Live sound" rather than
    // LIVE1.MP3. Padded with zeros to kTagBytes, which the format allows.
    static void make_tag(uint8_t* t) {
        memset(t, 0, kTagBytes);
        memcpy(t, "ID3\x03\x00\x00", 6);
        uint32_t body = kTagBytes - 10;   // four 7-bit digits
        t[6] = uint8_t(body >> 21 & 0x7f); t[7] = uint8_t(body >> 14 & 0x7f);
        t[8] = uint8_t(body >> 7 & 0x7f);  t[9] = uint8_t(body & 0x7f);
        uint8_t* p = t + 10;
        auto text = [&p](const char* id, const char* s) {
            uint32_t len = uint32_t(strlen(s)) + 1;   // one byte says "Latin-1"
            memcpy(p, id, 4);
            p[4] = uint8_t(len >> 24); p[5] = uint8_t(len >> 16); p[6] = uint8_t(len >> 8); p[7] = uint8_t(len);
            memcpy(p + 11, s, len - 1);
            p += 10 + len;
        };
        text("TIT2", "Live sound");
        text("TPE1", "Cardputer");
        text("TALB", "ZeroOS");
    }

    Stream& stream_;
    Config cfg_;
    uint64_t cursor_ = 0;        // the next byte of the stream the car gets
    Run run_;                    // the stretch being read
    Run left_;                   // the stretch it was playing before it jumped
    bool playing_ = false;       // the stretch being read is long enough to be the car playing
    bool armed_ = false;         // it was playing and jumped: steps_ counts what the jumps mean
    int steps_ = 0;              // songs forward (or back, if negative)
    bool paused_ = false;        // we said Pause, and haven't said Play since
    int64_t last_read_ms_ = 0;
    std::vector<Event> events_;
};

}  // namespace car_live
