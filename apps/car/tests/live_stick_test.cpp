// Checks USB live sound's made-up stick (live/live_stick.h): that the
// partition table, the FAT32 volume and the three songs hold together, that
// a car gets whole MP3 frames from a few seconds back, and that its buttons
// are read rightly from how it reads. Run by ctest; tests/car-live-test.sh
// checks the same stick with fsck and a real MP3 player.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apps/car/live/live_stick.h"

using namespace car_live;

namespace {
int g_failed = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                \
        }                                                              \
    } while (0)

uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { return get16(p) | get16(p + 2) << 16; }

const uint64_t kData0 = uint64_t(kPartStart + kDataStart + kClusterSectors) * kSector;
uint64_t song(int file, uint64_t at) { return kData0 + uint64_t(file) * kFileBytes + at; }

// A 320 kbit/s frame whose bytes after the header all hold `number`, so a
// test can tell which frame it was given.
constexpr int kFrame = 1044;
void push_frames(Stream& s, int first, int count) {
    for (int i = first; i < first + count; ++i) {
        std::vector<uint8_t> f(kFrame, uint8_t(i % 251));
        f[0] = 0xff; f[1] = 0xfb; f[2] = 0xe0; f[3] = 0x44;
        s.push(f.data(), f.size());
    }
}

// A stick with a car in front of it. Time and the encoder only move when
// the test says so.
struct Rig {
    Stream stream;
    Config cfg;
    LiveStick stick;
    int frames = 0;
    int64_t now = 0;
    std::vector<Event> seen;

    Rig() : cfg(make_config()), stick(stream, cfg) { make(400); }   // about 10 s of sound already
    static Config make_config() {
        Config c;
        c.patience_ms = 0;   // never wait in a test
        return c;
    }
    void make(int n) {
        push_frames(stream, frames, n);
        frames += n;
    }
    // The car reads `n` bytes of a song; the encoder keeps up.
    std::vector<uint8_t> read(int file, uint64_t at, size_t n) {
        make(int(n / kFrame) + 1);
        now += int64_t(n) * 1000 / 40000;
        std::vector<uint8_t> out(n);
        stick.read(song(file, at), out.data(), n, now);
        for (Event e : stick.take_events()) seen.push_back(e);
        return out;
    }
    // The car plays a song from `at` for `n` bytes, in 16 KiB reads.
    uint64_t play(int file, uint64_t at, size_t n) {
        for (size_t done = 0; done < n; done += 16384) read(file, at + done, 16384);
        return at + (n + 16383) / 16384 * 16384;
    }
    std::string events() {
        std::string s;
        for (Event e : seen)
            s += e == Event::Next ? "N" : e == Event::Previous ? "B" : e == Event::Pause ? "p" : "P";
        seen.clear();
        return s;
    }
};

void check_events(Rig& r, const char* want, const char* what) {
    std::string got = r.events();
    if (got == want) return;
    std::printf("FAIL %s\n  got:  \"%s\"\n  want: \"%s\"\n", what, got.c_str(), want);
    ++g_failed;
}

void test_frames() {
    const uint8_t a[] = {0xff, 0xfb, 0xe0, 0x44}, b[] = {0xff, 0xfb, 0xe2, 0x44}, c[] = {0xff, 0xfb, 0x90, 0x44};
    CHECK(mp3_frame_bytes(a) == 1044);   // 320 kbit/s, 44.1 kHz
    CHECK(mp3_frame_bytes(b) == 1045);   // the same, padded
    CHECK(mp3_frame_bytes(c) == 417);    // 128 kbit/s
    const uint8_t bad1[] = {0xff, 0xfb, 0xf0, 0x44}, bad2[] = {0x49, 0x44, 0x33, 0x03}, bad3[] = {0xff, 0xfb, 0x00, 0x44};
    CHECK(mp3_frame_bytes(bad1) == 0);
    CHECK(mp3_frame_bytes(bad2) == 0);
    CHECK(mp3_frame_bytes(bad3) == 0);
    for (int kbps : {128, 192, 320}) {
        std::vector<uint8_t> f = silent_frame(kbps);
        CHECK(mp3_frame_bytes(f.data()) == int(f.size()));
        CHECK(int(f.size()) == 144000 * kbps / 44100);
    }
}

void test_stream() {
    Stream s(8192);   // small, so it wraps
    push_frames(s, 0, 3);
    CHECK(s.head() == 3 * kFrame);
    CHECK(s.frame_before(0) == 0);
    CHECK(s.frame_before(kFrame - 1) == 0);
    CHECK(s.frame_before(kFrame) == uint64_t(kFrame));
    CHECK(s.frame_before(3 * kFrame + 500) == uint64_t(2 * kFrame));
    push_frames(s, 3, 20);
    uint64_t head = s.head();
    CHECK(head == 23 * kFrame);
    uint8_t b[8];
    s.read(head - kFrame, b, 8);   // the last frame: number 22
    CHECK(b[0] == 0xff && b[1] == 0xfb && b[4] == 22);
    s.read(0, b, 8);               // long gone
    CHECK(b[0] == 0 && b[4] == 0);
    // Only frames still held are offered.
    CHECK(s.frame_before(100) >= head - 8192);
    CHECK(s.frame_before(head) == head - kFrame);
    CHECK(!s.wait_head(head + 1, 0));
    CHECK(s.wait_head(head, 0));
    // Rubbish in the stream: the frames after it are found again.
    Stream t;
    const uint8_t junk[] = {1, 2, 0xff, 4, 5};
    t.push(junk, sizeof junk);
    push_frames(t, 0, 2);
    CHECK(t.frame_before(5 + kFrame + 10) == uint64_t(5 + kFrame));
    CHECK(t.frame_before(5 + 10) == 5);
    CHECK(t.frame_before(2) == 5);
    CHECK(Stream().frame_before(7) == 7);
}

void test_layout() {
    Rig r;
    std::vector<uint8_t> b(kSector);
    auto sector = [&](uint64_t s) {
        r.stick.read(s * kSector, b.data(), kSector, 0);
        return b.data();
    };
    CHECK(r.stick.size() == kImageBytes && kImageBytes % kSector == 0);
    CHECK(kClusters >= 65525);   // fewer and it isn't FAT32
    // The partition table.
    const uint8_t* m = sector(0);
    CHECK(m[510] == 0x55 && m[511] == 0xaa && m[446 + 4] == 0x0c);
    CHECK(get32(m + 446 + 8) == kPartStart && get32(m + 446 + 12) == kPartSectors);
    CHECK(uint64_t(kPartStart + kPartSectors) * kSector == kImageBytes);
    // The boot sector and its copy.
    for (uint32_t at : {0u, 6u}) {
        const uint8_t* v = sector(kPartStart + at);
        CHECK(get16(v + 11) == 512 && v[13] == kClusterSectors && get16(v + 14) == kReserved && v[16] == 2);
        CHECK(get32(v + 32) == kPartSectors && get32(v + 36) == kFatSectors && get32(v + 44) == 2);
        CHECK(memcmp(v + 82, "FAT32   ", 8) == 0 && v[510] == 0x55 && v[511] == 0xaa);
    }
    CHECK(get32(sector(kPartStart + 1)) == 0x41615252);
    // Both FATs are the same, and each file is one chain of the right length.
    auto fat = [&](int copy, uint32_t cluster) {
        const uint8_t* f = sector(kPartStart + kReserved + uint32_t(copy) * kFatSectors + cluster / 128);
        return get32(f + cluster % 128 * 4);
    };
    CHECK(fat(0, 0) == 0x0ffffff8 && fat(0, 1) == 0x0fffffff && fat(0, 2) == 0x0fffffff);
    for (int f = 0; f < kFiles; ++f) {
        uint32_t c = file_first_cluster(f), n = 1;
        while (fat(0, c) < 0x0ffffff8 && n <= kFileClusters) {
            CHECK(fat(0, c) == c + 1);
            if (c % 4099 == 0) CHECK(fat(1, c) == fat(0, c));
            c = fat(0, c);
            ++n;
        }
        CHECK(n == kFileClusters);
        CHECK(c == file_first_cluster(f) + kFileClusters - 1);
    }
    CHECK(fat(0, file_first_cluster(kFiles)) == 0);          // spare
    CHECK(fat(0, kClusters + 1) == 0 && fat(0, kClusters + 2) == 0);
    // The root folder.
    const uint8_t* d = sector(kPartStart + kDataStart);
    CHECK(memcmp(d, "LIVE       ", 11) == 0 && d[11] == 0x08);
    for (int f = 0; f < kFiles; ++f) {
        const uint8_t* e = d + 32 * (f + 1);
        std::string name(reinterpret_cast<const char*>(e), 11);
        CHECK(name == std::string("LIVE") + char('1' + f) + "   MP3");
        CHECK((get16(e + 20) << 16 | get16(e + 26)) == file_first_cluster(f));
        CHECK(get32(e + 28) == kFileBytes);
    }
    CHECK(d[32 * (kFiles + 1)] == 0);   // and nothing else
    // A read across the folder's end and into the first song.
    std::vector<uint8_t> x(2 * kSector);
    r.stick.read(kData0 - kSector, x.data(), x.size(), 0);
    CHECK(x[0] == 0 && memcmp(x.data() + kSector, "ID3", 3) == 0);
}

void test_song() {
    Rig r;
    // The start of a song: the name tag, then whole frames from three
    // seconds (120,000 bytes) back.
    std::vector<uint8_t> a = r.read(0, 0, 4096);
    uint64_t head = r.stream.head();   // as it was when the car read
    CHECK(memcmp(a.data(), "ID3\x03", 4) == 0);
    CHECK(std::string(reinterpret_cast<char*>(a.data()) + 21, 10) == "Live sound");
    const uint8_t* f = a.data() + kTagBytes;
    CHECK(f[0] == 0xff && f[1] == 0xfb);
    int number = f[4];
    uint64_t want = (head - 120000) / kFrame;
    CHECK(number == int(want % 251));
    // Reading on gets the stream without a gap.
    std::vector<uint8_t> b = r.read(0, 4096, 4096);
    size_t rest = kFrame - (4096 - kTagBytes) % kFrame;   // to the next frame's start
    CHECK(b[rest] == 0xff && b[rest + 1] == 0xfb && b[rest + 4] == uint8_t((want + 4) % 251));
    CHECK(b[0] == uint8_t((want + 3) % 251));
    // The third song, from the middle: whole frames too.
    std::vector<uint8_t> c = r.read(2, 5 * 1048576 + 17, 4096);
    CHECK(c[0] == 0xff && c[1] == 0xfb);
    // The very end of a song, and across into the next one.
    std::vector<uint8_t> e(1024);
    r.stick.read(song(1, kFileBytes - 512), e.data(), e.size(), r.now);
    CHECK(memcmp(e.data() + 512, "ID3", 3) == 0);
    // An encoder that has stopped: the read returns, with zeros.
    Stream quiet;
    Config cfg = Rig::make_config();
    LiveStick dry(quiet, cfg);
    std::vector<uint8_t> z(4096, 7);
    dry.read(song(0, 512), z.data(), z.size(), 0);
    CHECK(z[0] == 0 && z[4095] == 0);
    // After a long silence from the car it gets the sound from three
    // seconds back again, not from where it stopped.
    Rig p;
    uint64_t at = p.play(0, 0, 200000);
    p.make(2000);   // 52 seconds go by
    p.now += 52000;
    std::vector<uint8_t> g = p.read(0, at, 4096);
    CHECK(g[0] == 0xff && g[1] == 0xfb);
    CHECK(g[4] == uint8_t((p.stream.head() - 120000) / kFrame % 251));
}

void test_buttons() {
    const size_t kLook = 16384, kPlay = 400000;
    {   // The car reads the stick's songs when it is plugged in, then plays the first.
        Rig r;
        for (int f = 0; f < kFiles; ++f) {
            r.read(f, 0, kLook);
            r.read(f, kFileBytes - 4096, 4096);
        }
        CHECK(!r.stick.car_playing());
        r.play(0, 0, kPlay);
        CHECK(r.stick.car_playing());
        check_events(r, "", "looking, then playing: no buttons");
    }
    {   // Next, then next again past the last song, then previous.
        Rig r;
        r.play(0, 0, kPlay);
        r.play(1, 0, kPlay);
        check_events(r, "N", "next");
        r.play(2, 0, kPlay);
        r.play(0, 0, kPlay);
        check_events(r, "NN", "next, and round to the first");
        r.play(2, 0, kPlay);
        check_events(r, "B", "previous, round to the last");
        r.play(2, 0, kPlay);
        check_events(r, "B", "the same song from its start: previous");
    }
    {   // The button isn't sent before the car plays the new song.
        Rig r;
        r.play(0, 0, kPlay);
        r.read(1, 0, kLook);
        check_events(r, "", "one read isn't playing yet");
        r.play(1, kLook, kPlay);
        check_events(r, "N", "now it is");
    }
    {   // A look at the next song's name while playing, then on with this one.
        Rig r;
        uint64_t at = r.play(0, 0, kPlay);
        r.read(1, 0, kLook);
        r.read(1, kFileBytes - 4096, 4096);
        at = r.play(0, at, kPlay);
        check_events(r, "", "a look at the next song");
        CHECK(r.stick.car_playing());
        r.play(1, 0, kPlay);
        check_events(r, "N", "and a real next after it");
    }
    {   // Two quick presses pass through a song.
        Rig r;
        r.play(0, 0, kPlay);
        r.read(1, 0, kLook);
        r.play(2, 0, kPlay);
        check_events(r, "NN", "next twice");
        r.read(1, 0, kLook);
        r.play(0, 0, kPlay);
        check_events(r, "BB", "previous twice");
    }
    {   // A car that opens a song, reads its start, and starts over to play it.
        Rig r;
        r.play(0, 0, kPlay);
        r.read(1, 0, kLook);
        r.read(1, 0, kLook);
        r.play(1, kLook, kPlay);
        check_events(r, "N", "next, with the start read twice");
    }
    {   // Winding forward in a song isn't a button.
        Rig r;
        r.play(0, 0, kPlay);
        r.play(0, 50 * 1048576, kPlay);
        check_events(r, "", "a jump within the song");
    }
    {   // A song that runs out goes on to the next by itself.
        Rig r;
        r.play(0, kFileBytes - kPlay - (kFileBytes - kPlay) % 16384, kPlay + 16384);
        r.play(1, 0, kPlay);
        check_events(r, "", "the song ran out");
        r.play(2, 0, kPlay);
        check_events(r, "N", "a button after it still counts");
    }
    {   // The car stops reading: pause. It reads on: play.
        Rig r;
        uint64_t at = r.play(0, 0, kPlay);
        r.now += 3000;
        r.stick.tick(r.now);
        check_events(r, "", "three seconds isn't a pause");
        r.now += 3000;
        r.stick.tick(r.now);
        r.stick.tick(r.now + 100);
        for (Event e : r.stick.take_events()) r.seen.push_back(e);
        check_events(r, "p", "six seconds is, once");
        CHECK(!r.stick.car_playing());
        r.make(300);
        r.read(0, at, kLook);
        check_events(r, "", "one read isn't playing again yet");
        r.play(0, at + kLook, kPlay);
        check_events(r, "P", "play");
    }
    {   // Switched off and on again: the car looks at every song, then
        // plays one. That is play, and no skipping.
        Rig r;
        r.play(1, 0, kPlay);
        r.now += 60000;
        r.make(2000);
        r.stick.tick(r.now);
        for (Event e : r.stick.take_events()) r.seen.push_back(e);
        check_events(r, "p", "the car went off");
        for (int f = 0; f < kFiles; ++f) r.read(f, 0, kLook);
        r.play(1, 3 * 1048576, kPlay);
        check_events(r, "P", "back on: play only");
    }
    {   // A press, then the car goes quiet before it plays: pause, and the
        // press is forgotten.
        Rig r;
        r.play(0, 0, kPlay);
        r.read(1, 0, kLook);
        r.now += 10000;
        r.stick.tick(r.now);
        for (Event e : r.stick.take_events()) r.seen.push_back(e);
        check_events(r, "p", "quiet after a jump");
        r.make(400);
        r.play(1, kLook, kPlay);
        check_events(r, "P", "and no late skip");
    }
}
}  // namespace

int main() {
    test_frames();
    test_stream();
    test_layout();
    test_song();
    test_buttons();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("car live stick: all checks passed\n");
    return 0;
}
