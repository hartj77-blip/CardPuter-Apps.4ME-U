#include "apps/games/tv.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

#if defined(__linux__) && !defined(__EMSCRIPTEN__) && __has_include(<drm/drm.h>)
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include <fstream>
#define ZS_KMS 1
#endif

namespace zs::games {
namespace {
using namespace retro;

#ifdef ZS_KMS
std::string read_line(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') out.push_back(e->d_name);
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The card with the HDMI connector, e.g. "card1" (the LCD is another card).
std::string hdmi_card() {
    auto names = list_dir("/sys/class/drm");
    for (auto& c : names) {
        if (c.rfind("card", 0) != 0 || c.find('-') != std::string::npos) continue;
        for (auto& n : names)
            if (n.rfind(c + "-HDMI-A-", 0) == 0) return c;
    }
    return "";
}

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

template <class T>
uint64_t ptr(T* p) {
    return uint64_t(reinterpret_cast<uintptr_t>(p));
}
#endif

inline uint16_t to565(uint32_t c) {
    return uint16_t(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
}
inline uint16_t from1555(uint16_t c) {
    uint16_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    return uint16_t((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
}
}  // namespace

bool Tv::plugged() {
#ifdef ZS_KMS
    for (auto& n : list_dir("/sys/class/drm"))
        if (n.find("-HDMI-A-") != std::string::npos && read_line("/sys/class/drm/" + n + "/status") == "connected")
            return true;
#endif
    return false;
}

bool Tv::open(std::string& error) {
    close();
#ifndef ZS_KMS
    error = "No HDMI output here";
    return false;
#else
    std::lock_guard<std::mutex> lock(mu_);
    std::string card = hdmi_card();
    if (card.empty()) {
        error = "No HDMI output found";
        return false;
    }
    fd_ = ::open(("/dev/dri/" + card).c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        error = "Can't open the HDMI output (/dev/dri/" + card + "): " + std::strerror(errno);
        return false;
    }
    auto fail = [&](const std::string& why) {
        for (auto& b : buf_) free_buffer(b);
        ::close(fd_);
        fd_ = -1;
        error = why;
        return false;
    };
    // The first to open the card is its master; if someone else is, this fails.
    if (xioctl(fd_, DRM_IOCTL_SET_MASTER, nullptr) != 0)
        return fail("Something else is using the TV (Dock mode?)");

    drm_mode_card_res res{};
    if (xioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return fail("The HDMI output can't be set up");
    std::vector<uint32_t> crtcs(res.count_crtcs), conns(res.count_connectors), encs(res.count_encoders);
    std::vector<uint32_t> fbs(res.count_fbs);
    res.crtc_id_ptr = ptr(crtcs.data());
    res.connector_id_ptr = ptr(conns.data());
    res.encoder_id_ptr = ptr(encs.data());
    res.fb_id_ptr = ptr(fbs.data());
    if (xioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return fail("The HDMI output can't be set up");
    conns.resize(std::min<size_t>(conns.size(), res.count_connectors));
    crtcs.resize(std::min<size_t>(crtcs.size(), res.count_crtcs));

    // The connected HDMI connector, its modes and encoders.
    drm_mode_modeinfo mode{};
    bool found = false;
    for (uint32_t id : conns) {
        std::vector<drm_mode_modeinfo> modes;
        std::vector<uint32_t> cenc;
        drm_mode_get_connector c{};
        for (int tries = 0; tries < 3; ++tries) {
            c = {};
            c.connector_id = id;
            if (xioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR, &c) != 0) break;
            uint32_t nm = c.count_modes, ne = c.count_encoders;
            modes.assign(nm, {});
            cenc.assign(ne, 0);
            std::vector<uint32_t> props(c.count_props);
            std::vector<uint64_t> vals(c.count_props);
            c.modes_ptr = ptr(modes.data());
            c.encoders_ptr = ptr(cenc.data());
            c.props_ptr = ptr(props.data());
            c.prop_values_ptr = ptr(vals.data());
            if (xioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR, &c) != 0) break;
            if (c.count_modes <= nm && c.count_encoders <= ne) {   // didn't change in between
                modes.resize(c.count_modes);
                cenc.resize(c.count_encoders);
                break;
            }
        }
        if (c.connector_type != DRM_MODE_CONNECTOR_HDMIA || c.connection != 1 || modes.empty()) continue;

        // 720p: sharp enough on a TV, and the least to draw. Else the
        // monitor's preferred mode up to 1080p, else the biggest up to 1080p.
        const drm_mode_modeinfo* pick = nullptr;
        auto better = [](const drm_mode_modeinfo& a, const drm_mode_modeinfo* b) {
            if (!b) return true;
            if (a.hdisplay * a.vdisplay != b->hdisplay * b->vdisplay)
                return a.hdisplay * a.vdisplay > b->hdisplay * b->vdisplay;
            return std::abs(int(a.vrefresh) - 60) < std::abs(int(b->vrefresh) - 60);
        };
        for (auto& m : modes)
            if (m.hdisplay == 1280 && m.vdisplay == 720 && (!pick || std::abs(int(m.vrefresh) - 60) < std::abs(int(pick->vrefresh) - 60)))
                pick = &m;
        if (!pick)
            for (auto& m : modes)
                if ((m.type & DRM_MODE_TYPE_PREFERRED) && m.hdisplay <= 1920 && m.vdisplay <= 1080) pick = &m;
        if (!pick)
            for (auto& m : modes)
                if (m.hdisplay <= 1920 && m.vdisplay <= 1080 && better(m, pick)) pick = &m;
        if (!pick) pick = &modes[0];
        mode = *pick;

        // A CRTC: the one the connector's encoder drives now, else any the encoders can use.
        for (size_t i = 0; i <= cenc.size() && !crtc_; ++i) {
            drm_mode_get_encoder e{};
            e.encoder_id = i == 0 ? c.encoder_id : cenc[i - 1];
            if (!e.encoder_id || xioctl(fd_, DRM_IOCTL_MODE_GETENCODER, &e) != 0) continue;
            if (i == 0 && e.crtc_id) {
                crtc_ = e.crtc_id;
                break;
            }
            for (size_t k = 0; k < crtcs.size(); ++k)
                if (e.possible_crtcs & (1u << k)) {
                    crtc_ = crtcs[k];
                    break;
                }
        }
        if (!crtc_) continue;
        connector_ = id;
        found = true;
        break;
    }
    if (!found) return fail("No monitor on the HDMI port");
    mode_w_ = mode.hdisplay;
    mode_h_ = mode.vdisplay;

    for (auto& b : buf_)
        if (!make_buffer(b)) return fail("Not enough video memory for the TV");
    front_ = 0;
    flip_pending_ = false;
    drm_mode_crtc set{};
    set.crtc_id = crtc_;
    set.fb_id = buf_[0].fb;
    set.set_connectors_ptr = ptr(&connector_);
    set.count_connectors = 1;
    set.mode = mode;
    set.mode_valid = 1;
    if (xioctl(fd_, DRM_IOCTL_MODE_SETCRTC, &set) != 0)
        return fail(std::string("The TV didn't take the picture: ") + std::strerror(errno));
    return true;
#endif
}

void Tv::close() {
    std::lock_guard<std::mutex> lock(mu_);
#ifdef ZS_KMS
    if (fd_ < 0) return;
    wait_flip(50);
    for (auto& b : buf_) free_buffer(b);
    ::close(fd_);   // gives the card back (the console's picture comes back)
#endif
    fd_ = -1;
    crtc_ = connector_ = 0;
    mode_w_ = mode_h_ = 0;
    flip_pending_ = false;
    last_.clear();   // the next time starts clean
    last_w_ = last_h_ = 0;
    over_[0] = over_[1] = Overlay{};
}

bool Tv::make_buffer(Buf& b) {
#ifdef ZS_KMS
    b = Buf{};
    drm_mode_create_dumb c{};
    c.width = uint32_t(mode_w_);
    c.height = uint32_t(mode_h_);
    c.bpp = 16;
    if (xioctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &c) != 0) return false;
    b.handle = c.handle;
    b.pitch = c.pitch;
    b.size = size_t(c.size);
    drm_mode_fb_cmd f{};
    f.width = uint32_t(mode_w_);
    f.height = uint32_t(mode_h_);
    f.pitch = c.pitch;
    f.bpp = 16;
    f.depth = 16;   // RGB565
    f.handle = c.handle;
    if (xioctl(fd_, DRM_IOCTL_MODE_ADDFB, &f) != 0) return false;
    b.fb = f.fb_id;
    drm_mode_map_dumb m{};
    m.handle = c.handle;
    if (xioctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &m) != 0) return false;
    void* p = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, off_t(m.offset));
    if (p == MAP_FAILED) return false;
    b.map = static_cast<uint8_t*>(p);
    std::memset(b.map, 0, b.size);
    return true;
#else
    (void)b;
    return false;
#endif
}

void Tv::free_buffer(Buf& b) {
#ifdef ZS_KMS
    if (b.map) munmap(b.map, b.size);
    if (b.fb) {
        uint32_t id = b.fb;
        xioctl(fd_, DRM_IOCTL_MODE_RMFB, &id);
    }
    if (b.handle) {
        drm_mode_destroy_dumb d{};
        d.handle = b.handle;
        xioctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
#endif
    b = Buf{};
}

void Tv::wait_flip(int timeout_ms) {
#ifdef ZS_KMS
    while (flip_pending_ && fd_ >= 0) {
        pollfd p{fd_, POLLIN, 0};
        if (::poll(&p, 1, timeout_ms) <= 0) return;
        char buf[256];
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n <= 0) return;
        for (ssize_t at = 0; at + ssize_t(sizeof(drm_event)) <= n;) {
            drm_event e;
            std::memcpy(&e, buf + at, sizeof e);
            if (e.type == DRM_EVENT_FLIP_COMPLETE) flip_pending_ = false;
            if (e.length == 0) break;
            at += e.length;
        }
    }
#else
    (void)timeout_ms;
#endif
}

void Tv::show(const void* data, unsigned w, unsigned h, size_t pitch, PixelFormat fmt, double aspect) {
    if (!data || !w || !h) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ < 0) return;
    // Into last_ as RGB565 at the core's own size: a quick pass over a small
    // picture, and the menu can draw over it again later.
    last_.resize(size_t(w) * h);
    last_w_ = int(w);
    last_h_ = int(h);
    aspect_ = aspect;
    const auto* base = static_cast<const uint8_t*>(data);
    uint16_t* out = last_.data();
    for (unsigned y = 0; y < h; ++y, out += w) {
        const uint8_t* row = base + size_t(y) * pitch;
        if (fmt == kRGB565) {
            std::memcpy(out, row, size_t(w) * 2);
        } else if (fmt == kXRGB8888) {
            auto* p = reinterpret_cast<const uint32_t*>(row);
            for (unsigned x = 0; x < w; ++x) out[x] = to565(p[x]);
        } else {
            auto* p = reinterpret_cast<const uint16_t*>(row);
            for (unsigned x = 0; x < w; ++x) out[x] = from1555(p[x]);
        }
    }
    compose();
}

void Tv::redraw() {
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ < 0) return;
    compose();
}

void Tv::overlay(Place where, const uint16_t* pix, int w, int h, int stride) {
    std::lock_guard<std::mutex> lock(mu_);
    Overlay& o = over_[where == Place::Middle ? 0 : 1];
    if (!pix || w <= 0 || h <= 0) {
        o = Overlay{};
        return;
    }
    o.w = w;
    o.h = h;
    o.pix.resize(size_t(w) * h);
    for (int y = 0; y < h; ++y) std::memcpy(o.pix.data() + size_t(y) * w, pix + size_t(y) * stride, size_t(w) * 2);
}

TvRect tv_picture_rect(int mode_w, int mode_h, int w, int h, double aspect) {
    TvRect r;
    if (mode_w <= 0 || mode_h <= 0 || w <= 0 || h <= 0) return r;
    if (aspect <= 0) aspect = double(w) / h;
    int s = mode_h / h;
    r.h = s >= 2 ? s * h : mode_h;
    r.w = int(std::lround(r.h * aspect));
    if (r.w > mode_w) {
        r.w = mode_w;
        r.h = std::min(mode_h, int(std::lround(r.w / aspect)));
    }
    r.w = std::max(1, r.w);
    r.h = std::max(1, r.h);
    r.x = (mode_w - r.w) / 2;
    r.y = (mode_h - r.h) / 2;
    return r;
}

void Tv::compose() {
#ifdef ZS_KMS
    wait_flip(20);
    if (flip_pending_) return;   // the TV hasn't shown the last one yet: skip this one
    Buf& b = buf_[1 - front_];
    if (!b.map) return;
    TvRect r = tv_picture_rect(mode_w_, mode_h_, last_w_, last_h_, aspect_);   // 0 x 0: no picture yet
    int px = r.x, py = r.y, pw = r.w, ph = r.h;
    if (px != b.x || py != b.y || pw != b.w || ph != b.h || b.dirty_bars) {
        std::memset(b.map, 0, b.size);   // new size, or an overlay was in the bars
        b.x = px, b.y = py, b.w = pw, b.h = ph;
        b.dirty_bars = false;
    }
    // Rows are built in normal memory and copied out: the buffer is
    // write-combined, fast to write in order and slow to read back.
    line_.resize(size_t(std::max(pw, mode_w_)));
    int built = -1;
    for (int y = 0; y < ph; ++y) {
        int sy = std::min(last_h_ - 1, int((int64_t(2 * y + 1) * last_h_) / (2 * ph)));
        if (sy != built) {
            const uint16_t* src = last_.data() + size_t(sy) * last_w_;
            for (int x = 0; x < pw; ++x)
                line_[size_t(x)] = src[std::min(last_w_ - 1, int((int64_t(2 * x + 1) * last_w_) / (2 * pw)))];
            built = sy;
        }
        std::memcpy(b.map + size_t(py + y) * b.pitch + size_t(px) * 2, line_.data(), size_t(pw) * 2);
    }
    // The menu and notes, a whole number bigger (3x at 720p).
    for (int i = 0; i < 2; ++i) {
        const Overlay& o = over_[i];
        if (o.w <= 0) continue;
        int k = std::max(1, mode_h_ / 240);
        while (k > 1 && (o.w * k > mode_w_ || o.h * k > mode_h_)) --k;
        int ow = std::min(o.w * k, mode_w_), oh = std::min(o.h * k, mode_h_);
        int ox = (mode_w_ - ow) / 2;
        int oy = i == 0 ? (mode_h_ - oh) / 2 : std::max(0, mode_h_ - oh - mode_h_ / 20);
        if (ox < px || oy < py || ox + ow > px + pw || oy + oh > py + ph) b.dirty_bars = true;
        for (int x = 0; x < ow; ++x) line_[size_t(x)] = 0;
        for (int y = 0; y < oh; ++y) {
            if (y % k == 0) {
                const uint16_t* src = o.pix.data() + size_t(y / k) * o.w;
                for (int x = 0; x < ow; ++x) line_[size_t(x)] = src[x / k];
            }
            std::memcpy(b.map + size_t(oy + y) * b.pitch + size_t(ox) * 2, line_.data(), size_t(ow) * 2);
        }
    }
    drm_mode_crtc_page_flip f{};
    f.crtc_id = crtc_;
    f.fb_id = b.fb;
    f.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (xioctl(fd_, DRM_IOCTL_MODE_PAGE_FLIP, &f) == 0) {
        flip_pending_ = true;
        front_ = 1 - front_;
    }
#endif
}

}  // namespace zs::games
