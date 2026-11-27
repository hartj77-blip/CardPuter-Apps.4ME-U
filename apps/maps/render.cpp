// The map renderer (render.h): areas by scanline, lines as anti-aliased
// strokes, and a list of names for the app to draw.
#include "apps/maps/render.h"

#include <algorithm>
#include <cmath>

namespace zmap {
namespace {

const Theme kDay = {
    0xcfd2d6, 0xf3f0e8, 0x9ccbe6, 0xb9d8a6, 0xc9e9b5, 0xc5ddd0, 0xf3e3b3, 0xdcd8e6, 0x9ccbe6,
    0x7fb6dc, 0x9b6bb3, 0x4f86c6, 0x777777, 0xb8b4c8,
    {0xffffff, 0xffffff, 0xffffff, 0xfff6b3, 0xffe08a, 0xffc266, 0xf79a5a},
    {0xc4c0b8, 0xb5b1a8, 0xa9a59c, 0xc0a94a, 0xc49a3c, 0xc47f2a, 0xb85a22},
    0x202020, 0xffffff, 0x3c4450, 0xffffff, 0x1f4fb0,
    0x1a73e8, 0xffffff,
    0x2f80ed, 0x1b4f9c,
};
const Theme kNight = {
    0x0b0c0e, 0x1c1f26, 0x0d2438, 0x16301e, 0x1d3a26, 0x183238, 0x3b3626, 0x2a2a38, 0x0d2438,
    0x1b4466, 0x8a5aa6, 0x3a6aa8, 0x6a6a6a, 0x444458,
    {0x3a3f49, 0x4a505c, 0x596070, 0x77705a, 0x9a8440, 0xb07a30, 0xc8682a},
    {0x101216, 0x101216, 0x101216, 0x101216, 0x2a2410, 0x2e1e0a, 0x341a08},
    0xe8eaed, 0x000000, 0x30343c, 0xffffff, 0x2a5fd0,
    0x4da3ff, 0xffffff,
    0x3d8bff, 0x0c2a5c,
};

inline uint16_t blend(uint16_t d, uint16_t s, uint32_t a) {   // a: 0..32
    uint32_t D = (d | uint32_t(d) << 16) & 0x07E0F81F, S = (s | uint32_t(s) << 16) & 0x07E0F81F;
    D = (D + (((S - D) * a) >> 5)) & 0x07E0F81F;
    return uint16_t(D | D >> 16);
}

int utf8_len(const char* s) {
    int n = 0;
    for (; *s; s++) n += (uint8_t(*s) & 0xC0) != 0x80;
    return n;
}

// The part of a segment inside a box (Liang-Barsky). False if none.
bool clip_segment(float& x0, float& y0, float& x1, float& y1, float bx0, float by0, float bx1, float by1) {
    float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
    auto edge = [&](float p, float q) {
        if (p == 0) return q >= 0;
        float r = q / p;
        if (p < 0) {
            if (r > t1) return false;
            t0 = std::max(t0, r);
        } else {
            if (r < t0) return false;
            t1 = std::min(t1, r);
        }
        return true;
    };
    if (!edge(-dx, x0 - bx0) || !edge(dx, bx1 - x0) || !edge(-dy, y0 - by0) || !edge(dy, by1 - y0)) return false;
    float ax = x0 + t0 * dx, ay = y0 + t0 * dy;
    x1 = x0 + t1 * dx, y1 = y0 + t1 * dy;
    x0 = ax, y0 = ay;
    return true;
}

}  // namespace

const Theme& day_theme() { return kDay; }
const Theme& night_theme() { return kNight; }

double metres_between(double x0, double y0, double x1, double y1) {
    double upm = units_per_metre(y_to_lat((y0 + y1) / 2));
    return std::hypot(x1 - x0, y1 - y0) / upm;
}

void Renderer::to_screen(const View& v, double wx, double wy, double& sx, double& sy) {
    double upp = v.mpp * units_per_metre(y_to_lat(v.cy));
    double c = std::cos(v.up * kPi / 180), s = std::sin(v.up * kPi / 180);
    double dx = (wx - v.cx) / upp, dy = (wy - v.cy) / upp;
    sx = dx * c + dy * s + v.px;
    sy = -dx * s + dy * c + v.py;
}

void Renderer::to_world(const View& v, double sx, double sy, double& wx, double& wy) {
    double upp = v.mpp * units_per_metre(y_to_lat(v.cy));
    double c = std::cos(v.up * kPi / 180), s = std::sin(v.up * kPi / 180);
    double dx = sx - v.px, dy = sy - v.py;
    wx = v.cx + (dx * c - dy * s) * upp;
    wy = v.cy + (dx * s + dy * c) * upp;
}

// Levels go from the coarsest to the finest. A level at zoom z is made to
// be drawn at 5 * 2^(11 - z) metres a pixel and up (tools/zmap_build.cpp).
int Renderer::level_for(const Map& map, double mpp) {
    for (int i = 0; i < map.levels() - 1; i++)
        if (mpp >= 5.0 * std::pow(2.0, 11.0 - double(map.level(i).zoom))) return i;
    return map.levels() - 1;
}

double Renderer::road_width(uint8_t kind, double mpp) {
    // Rows: service .. motorway. Columns: 1.5, 3, 6, 12, 25, 50, 100, 200,
    // 400, 800, 1600 m a pixel. 0: not drawn, so a whole state isn't a blot.
    static const float kW[7][11] = {
        {3.0f, 2.0f, 1.2f, 0, 0, 0, 0, 0, 0, 0, 0},
        {5.0f, 3.5f, 2.2f, 1.3f, 0.9f, 0, 0, 0, 0, 0, 0},
        {6.0f, 4.5f, 3.0f, 2.2f, 1.5f, 1.1f, 0.8f, 0, 0, 0, 0},
        {7.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.1f, 0, 0, 0, 0},
        {8.0f, 6.0f, 4.5f, 3.5f, 2.5f, 2.0f, 1.5f, 1.0f, 0.8f, 0, 0},
        {8.0f, 6.5f, 5.0f, 4.0f, 3.0f, 2.5f, 2.0f, 1.6f, 1.3f, 1.0f, 0.8f},
        {9.0f, 7.0f, 5.5f, 4.5f, 3.5f, 3.0f, 2.5f, 2.2f, 2.0f, 1.7f, 1.4f},
    };
    if (!is_road(kind)) return 0;
    const float* row = kW[kind - kService];
    double f = std::clamp(std::log2(std::max(mpp, 0.01) / 1.5), 0.0, 10.0);
    int i = std::min(int(f), 9);
    double a = row[i], b = row[i + 1];
    if (a <= 0) return 0;
    if (b <= 0) return f - i < 0.5 ? a : 0;
    return a + (b - a) * (f - i);
}

// Even-odd fill of rings given in screen pixels.
void Renderer::poly(const float* xy, const uint32_t* counts, int nparts, uint16_t colour) {
    if (int(rows_.size()) < h_) rows_.resize(size_t(h_));
    int lo = h_, hi = -1;
    const float* p = xy;
    for (int part = 0; part < nparts; part++) {
        uint32_t n = counts[part];
        for (uint32_t i = 0; i < n; i++) {
            float ax = p[2 * i], ay = p[2 * i + 1];
            uint32_t j = i + 1 == n ? 0 : i + 1;
            float bx = p[2 * j], by = p[2 * j + 1];
            if (ay == by) continue;
            float y0 = std::min(ay, by), y1 = std::max(ay, by);
            if (y1 <= 0.5f || y0 > float(h_) - 0.5f) continue;
            int r0 = std::max(0, int(std::ceil(y0 - 0.5f))), r1 = std::min(h_ - 1, int(std::ceil(y1 - 0.5f)) - 1);
            float slope = (bx - ax) / (by - ay);
            for (int r = r0; r <= r1; r++) rows_[size_t(r)].push_back(ax + (float(r) + 0.5f - ay) * slope);
            if (r0 <= r1) lo = std::min(lo, r0), hi = std::max(hi, r1);
        }
        p += 2 * n;
    }
    for (int r = lo; r <= hi; r++) {
        std::vector<float>& xs = rows_[size_t(r)];
        std::sort(xs.begin(), xs.end());
        uint16_t* line = px_ + size_t(r) * size_t(w_);
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            if (xs[k + 1] <= 0.5f || xs[k] >= float(w_)) continue;
            int i0 = std::max(0, int(std::ceil(xs[k] - 0.5f))), i1 = std::min(w_, int(std::ceil(xs[k + 1] - 0.5f)));
            for (int x = i0; x < i1; x++) line[x] = colour;
        }
        xs.clear();
    }
}

void Renderer::fill(const Item& it, uint16_t colour, bool with_tile_rect) {
    const Feature& f = it.f;
    pts_.resize(size_t(f.npoints) * 2 + 8);
    counts_.resize(size_t(f.head.nparts) + 1);
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    auto put = [&](size_t i, float lx, float ly) {
        float sx = lx * it.m00 + ly * it.m01 + it.bx, sy = lx * it.m10 + ly * it.m11 + it.by;
        pts_[2 * i] = sx, pts_[2 * i + 1] = sy;
        minx = std::min(minx, sx), maxx = std::max(maxx, sx), miny = std::min(miny, sy), maxy = std::max(maxy, sy);
    };
    for (uint32_t i = 0; i < f.npoints; i++) {
        int x, y;
        f.point(i, x, y);
        put(i, float(x), float(y));
    }
    int nparts = f.head.nparts;
    for (int i = 0; i < nparts; i++) counts_[size_t(i)] = f.count(i);
    if (with_tile_rect) {
        const float e = float(kTileExtent);
        put(f.npoints, 0, 0), put(f.npoints + 1, e, 0), put(f.npoints + 2, e, e), put(f.npoints + 3, 0, e);
        counts_[size_t(nparts++)] = 4;
    }
    if (maxx < 0 || maxy < 0 || minx > float(w_) || miny > float(h_)) return;
    poly(pts_.data(), counts_.data(), nparts, colour);
    drawn_++;
}

// One stroke: every pixel within r of the segment, with a soft edge.
void Renderer::segment(float x0, float y0, float x1, float y1, float r, uint16_t colour) {
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, reach = r + 1;
    int ya = std::max(0, int(std::floor(std::min(y0, y1) - reach))), yb = std::min(h_ - 1, int(std::ceil(std::max(y0, y1) + reach)));
    for (int y = ya; y <= yb; y++) {
        float yc = float(y) + 0.5f, xa, xb;
        if (std::fabs(dy) < 1e-4f) {
            xa = std::min(x0, x1), xb = std::max(x0, x1);
        } else {
            float t0 = std::clamp((yc - reach - y0) / dy, 0.0f, 1.0f), t1 = std::clamp((yc + reach - y0) / dy, 0.0f, 1.0f);
            xa = x0 + t0 * dx, xb = x0 + t1 * dx;
            if (xa > xb) std::swap(xa, xb);
        }
        int i0 = std::max(0, int(std::floor(xa - reach))), i1 = std::min(w_ - 1, int(std::ceil(xb + reach)));
        uint16_t* line = px_ + size_t(y) * size_t(w_);
        for (int x = i0; x <= i1; x++) {
            float qx = float(x) + 0.5f - x0, qy = yc - y0;
            float t = len2 > 0 ? std::clamp((qx * dx + qy * dy) / len2, 0.0f, 1.0f) : 0;
            float ex = qx - t * dx, ey = qy - t * dy;
            float a = r + 0.5f - std::sqrt(ex * ex + ey * ey);
            if (a <= 0) continue;
            line[x] = a >= 1 ? colour : blend(line[x], colour, uint32_t(a * 32));
        }
    }
}

void Renderer::lines(const Item& it, uint16_t colour, float width, float dash_on, float dash_off) {
    const Feature& f = it.f;
    float r = width / 2, m = r + 2;
    uint32_t at = 0;
    bool any = false;
    for (int part = 0; part < f.head.nparts; part++) {
        uint32_t n = f.count(part);
        float px = 0, py = 0, phase = 0;
        for (uint32_t i = 0; i < n; i++) {
            int lx, ly;
            f.point(at + i, lx, ly);
            float sx = float(lx) * it.m00 + float(ly) * it.m01 + it.bx, sy = float(lx) * it.m10 + float(ly) * it.m11 + it.by;
            if (i > 0) {
                float len = dash_on > 0 ? std::hypot(sx - px, sy - py) : 0;
                bool off = (px < -m && sx < -m) || (py < -m && sy < -m) || (px > float(w_) + m && sx > float(w_) + m) ||
                           (py > float(h_) + m && sy > float(h_) + m);
                if (!off) {
                    any = true;
                    if (dash_on <= 0) {
                        segment(px, py, sx, sy, r, colour);
                    } else if (len > 0) {
                        float period = dash_on + dash_off;
                        // Start at the first dash that reaches into this segment.
                        for (float d = -std::fmod(phase, period); d < len; d += period) {
                            float a = std::max(d, 0.0f), b = std::min(d + dash_on, len);
                            if (b <= a) continue;
                            segment(px + (sx - px) * a / len, py + (sy - py) * a / len, px + (sx - px) * b / len,
                                    py + (sy - py) * b / len, r, colour);
                        }
                    }
                }
                phase += len;
            }
            px = sx, py = sy;
        }
        at += n;
    }
    drawn_ += any;
}

void Renderer::draw(const Map& map, const View& v, const Theme& t, uint16_t* pixels, std::vector<Label>& labels,
                    const std::vector<Rect>& keep_out) {
    px_ = pixels, w_ = v.w, h_ = v.h, drawn_ = 0;
    labels.clear();
    std::fill(pixels, pixels + size_t(w_) * size_t(h_), rgb565(t.outside));
    if (!map.ok()) return;
    int li = level_for(map, v.mpp);
    const Level& L = map.level(li);
    int shift = kWorldBits - int(L.zoom);
    double upp = v.mpp * units_per_metre(y_to_lat(v.cy));
    double c = std::cos(v.up * kPi / 180), s = std::sin(v.up * kPi / 180);

    // The tiles under the screen's four corners.
    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    for (int k = 0; k < 4; k++) {
        double wx, wy;
        to_world(v, k & 1 ? w_ : 0, k & 2 ? h_ : 0, wx, wy);
        minx = std::min(minx, wx), maxx = std::max(maxx, wx), miny = std::min(miny, wy), maxy = std::max(maxy, wy);
    }
    double tile = double(int64_t(1) << shift);
    int64_t tx0 = std::max<int64_t>(int64_t(std::floor(minx / tile)), L.tx0), tx1 = std::min<int64_t>(int64_t(std::floor(maxx / tile)), int64_t(L.tx0) + L.ntx - 1);
    int64_t ty0 = std::max<int64_t>(int64_t(std::floor(miny / tile)), L.ty0), ty1 = std::min<int64_t>(int64_t(std::floor(maxy / tile)), int64_t(L.ty0) + L.nty - 1);
    // Never more than a screenful: a damaged file can claim any grid it likes.
    tx1 = std::min(tx1, tx0 + 7), ty1 = std::min(ty1, ty0 + 7);
    items_.clear();
    std::vector<Item> bare;   // tiles with nothing of the region in them
    for (int64_t ty = ty0; ty <= ty1; ty++) {
        for (int64_t tx = tx0; tx <= tx1; tx++) {
            Item it{};
            double k = tile / kTileExtent / upp;
            double dx0 = (double(tx) * tile - v.cx) / upp, dy0 = (double(ty) * tile - v.cy) / upp;
            it.m00 = float(k * c), it.m01 = float(k * s), it.m10 = float(-k * s), it.m11 = float(k * c);
            it.bx = float(dx0 * c + dy0 * s + v.px), it.by = float(-dx0 * s + dy0 * c + v.py);
            const uint8_t* data;
            uint32_t size, count;
            bool covered = false;
            if (map.tile(li, tx, ty, data, size, count)) {
                TileWalk walk(data, size, count);
                while (walk.next(it.f)) {
                    covered |= it.f.head.kind == kCoverage;
                    items_.push_back(it);
                }
            }
            if (!covered) bare.push_back(it);
        }
    }

    // Land, then what lies on it.
    for (const Item& it : items_)
        if (it.f.head.kind == kCoverage) fill(it, rgb565(t.land), false);
    const uint32_t area_colour[] = {0, 0, t.sea, t.forest, t.park, t.wetland, t.sand, t.airport, t.water};
    for (uint8_t kind = kSea; kind <= kLastArea; kind++)
        for (const Item& it : items_)
            if (it.f.head.kind == kind) fill(it, rgb565(area_colour[kind]), false);
    // Grey out what is outside the region: areas run over its edge.
    for (const Item& it : items_) {
        if (it.f.head.kind != kCoverage) continue;
        bool whole = it.f.head.nparts == 1 && it.f.npoints == 4;
        for (uint32_t i = 0; whole && i < 4; i++) {
            int x, y;
            it.f.point(i, x, y);
            whole = (x == 0 || x == kTileExtent) && (y == 0 || y == kTileExtent);
        }
        if (!whole) fill(it, rgb565(t.outside), true);
    }
    for (Item& it : bare) {
        it.f = Feature{};
        fill(it, rgb565(t.outside), true);
    }

    // Lines, thin to thick. Roads get their edges first, so junctions join up.
    float wide = v.mpp < 6 ? 1.6f : 1.2f;
    for (const Item& it : items_) {
        switch (it.f.head.kind) {
            case kStream: lines(it, rgb565(t.stream), 1.0f, 0, 0); break;
            case kRiver: lines(it, rgb565(t.water), v.mpp < 12 ? 2.4f : 1.4f, 0, 0); break;
            case kBoundary: lines(it, rgb565(t.boundary), wide, 6, 3); break;
            case kFerry: lines(it, rgb565(t.ferry), 1.2f, 4, 4); break;
            case kRail: lines(it, rgb565(t.rail), wide, 7, 3); break;
            case kRunway: lines(it, rgb565(t.runway), float(std::clamp(40.0 / v.mpp, 1.5, 10.0)), 0, 0); break;
            default: break;
        }
    }
    float width[7];
    for (int k = 0; k < 7; k++) width[k] = float(road_width(uint8_t(kService + k), v.mpp));
    bool night = &t == &kNight;
    for (int pass = 0; pass < 2; pass++)
        for (uint8_t kind = kService; kind <= kMotorway; kind++) {
            float wd = width[kind - kService];
            if (wd <= 0) continue;
            bool cased = wd >= 2.2f;
            if (pass == 0 && !cased) continue;
            // A road too thin for an edge is one line: dark on the day map, light at night.
            uint32_t colour = pass == 0 ? t.casing[kind - kService] : cased || night ? t.road[kind - kService] : t.casing[kind - kService];
            for (const Item& it : items_)
                if (it.f.head.kind == kind) lines(it, rgb565(colour), pass == 0 ? wd + 1.6f : wd, 0, 0);
        }

    // Names: places first, then roads, each once, where they don't overlap.
    struct Cand {
        Label l;
        float score;
    };
    std::vector<Cand> cands;
    auto add = [&](const Label& l, float score) {
        for (Cand& o : cands)
            if (o.l.text == l.text && o.l.shield == l.shield) {
                if (score > o.score) o = {l, score};
                return;
            }
        cands.push_back({l, score});
    };
    for (const Item& it : items_) {
        const Feature& f = it.f;
        uint8_t kind = f.head.kind;
        if (is_place(kind)) {
            if (f.npoints == 0) continue;
            int lx, ly;
            f.point(0, lx, ly);
            float sx = float(lx) * it.m00 + float(ly) * it.m01 + it.bx, sy = float(lx) * it.m10 + float(ly) * it.m11 + it.by;
            const char* name = map.str(f.head.name);
            if (*name && sx >= 0 && sy >= 0 && sx < float(w_) && sy < float(h_))
                add({int(sx), int(sy), name, kind, false}, 1000.0f * float(kind));
            continue;
        }
        if (!is_road(kind) || width[kind - kService] < 2.2f) continue;
        const char *name = map.str(f.head.name), *ref = map.str(f.head.ref);
        bool shield = *ref && (kind >= kPrimary || !*name);
        const char* text = shield ? ref : name;
        if (!*text) continue;
        // The longest run on screen, and the point half way along it.
        uint32_t at = 0;
        float best = 0, bx = 0, by = 0;
        for (int part = 0; part < f.head.nparts; part++) {
            uint32_t n = f.count(part);
            pts_.clear();
            float len = 0, px = 0, py = 0;
            for (uint32_t i = 0; i < n; i++) {
                int lx, ly;
                f.point(at + i, lx, ly);
                float sx = float(lx) * it.m00 + float(ly) * it.m01 + it.bx, sy = float(lx) * it.m10 + float(ly) * it.m11 + it.by;
                if (i > 0) {
                    float ax = px, ay = py, cx2 = sx, cy2 = sy;
                    if (clip_segment(ax, ay, cx2, cy2, 0, 0, float(w_), float(h_))) {
                        float d = std::hypot(cx2 - ax, cy2 - ay);
                        pts_.insert(pts_.end(), {ax, ay, cx2, cy2, d});
                        len += d;
                    }
                }
                px = sx, py = sy;
            }
            at += n;
            if (len <= best) continue;
            best = len;
            float half = len / 2;
            for (size_t k = 0; k + 4 < pts_.size(); k += 5) {
                if (half <= pts_[k + 4] || k + 5 >= pts_.size()) {
                    float u = pts_[k + 4] > 0 ? std::min(1.0f, half / pts_[k + 4]) : 0;
                    bx = pts_[k] + (pts_[k + 2] - pts_[k]) * u, by = pts_[k + 1] + (pts_[k + 3] - pts_[k + 1]) * u;
                    break;
                }
                half -= pts_[k + 4];
            }
        }
        float need = shield ? 30.0f : std::max(30.0f, 3.5f * float(utf8_len(text)));
        if (best < need) continue;
        add({int(bx), int(by), text, kind, shield}, 100.0f * float(kind) + std::min(best, 200.0f) / 2 + (shield ? 30 : 0));
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    std::vector<Rect> taken = keep_out;
    for (const Cand& cd : cands) {
        if (labels.size() >= 16) break;
        const Label& l = cd.l;
        int n = utf8_len(l.text);
        int tw = l.kind == kCity ? 9 * n : l.shield ? 6 * n + 8 : is_place(l.kind) ? 7 * n : 6 * n + 2;
        int th = l.kind == kCity ? 18 : 14;
        Rect r{l.x - tw / 2 - 2, l.y - th / 2 - 1, l.x + tw / 2 + 2, l.y + th / 2 + 1};
        if (r.x0 < 0 || r.y0 < 0 || r.x1 > w_ || r.y1 > h_) continue;
        bool hit = false;
        for (const Rect& o : taken)
            if (r.x0 < o.x1 && o.x0 < r.x1 && r.y0 < o.y1 && o.y0 < r.y1) {
                hit = true;
                break;
            }
        if (hit) continue;
        taken.push_back(r);
        labels.push_back(l);
    }
}

void Renderer::marker(const View& v, const Theme& t, uint16_t* pixels, double x, double y, double screen_deg, bool moving) {
    px_ = pixels, w_ = v.w, h_ = v.h;
    float fx = float(x), fy = float(y);
    if (!moving) {
        segment(fx, fy, fx, fy, 6.0f, rgb565(t.marker_edge));
        segment(fx, fy, fx, fy, 4.4f, rgb565(t.marker));
        return;
    }
    static const float kShape[4][2] = {{0, -10}, {7, 8}, {0, 4}, {-7, 8}};
    float c = float(std::cos(screen_deg * kPi / 180)), s = float(std::sin(screen_deg * kPi / 180));
    float xy[8];
    for (int i = 0; i < 4; i++) {
        xy[2 * i] = fx + kShape[i][0] * c - kShape[i][1] * s;
        xy[2 * i + 1] = fy + kShape[i][0] * s + kShape[i][1] * c;
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        segment(xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1], 1.8f, rgb565(t.marker_edge));
    }
    uint32_t four = 4;
    poly(xy, &four, 1, rgb565(t.marker));
}

void Renderer::compass(const View& v, const Theme& t, uint16_t* pixels, int x, int y) {
    px_ = pixels, w_ = v.w, h_ = v.h;
    float fx = float(x), fy = float(y);
    segment(fx, fy, fx, fy, 9.5f, rgb565(t.text));
    segment(fx, fy, fx, fy, 8.5f, rgb565(t.halo));
    float nx = float(-std::sin(v.up * kPi / 180)), ny = float(-std::cos(v.up * kPi / 180));   // north, on screen
    segment(fx, fy, fx - nx * 6, fy - ny * 6, 1.2f, rgb565(t.text));
    segment(fx, fy, fx + nx * 6, fy + ny * 6, 1.4f, rgb565(0xe03020));
}

void Renderer::route(const View& v, const Theme& t, uint16_t* pixels, const std::vector<double>& xy, size_t from) {
    px_ = pixels, w_ = v.w, h_ = v.h;
    size_t n = xy.size() / 2;
    if (n < 2 || from + 1 >= n) return;
    float width = float(std::clamp(9.0 / std::sqrt(std::max(v.mpp, 1.0)) + 2.5, 3.0, 7.0));
    std::vector<float> sp;
    sp.reserve((n - from) * 2);
    for (size_t i = from; i < n; i++) {
        double sx, sy;
        to_screen(v, xy[2 * i], xy[2 * i + 1], sx, sy);
        sp.push_back(float(sx));
        sp.push_back(float(sy));
    }
    float m = width + 2;
    for (int pass = 0; pass < 2; pass++) {
        float r = pass == 0 ? width / 2 + 1.2f : width / 2;
        uint16_t colour = rgb565(pass == 0 ? t.route_edge : t.route);
        for (size_t i = 0; i + 3 < sp.size(); i += 2) {
            float ax = sp[i], ay = sp[i + 1], bx = sp[i + 2], by = sp[i + 3];
            if ((ax < -m && bx < -m) || (ay < -m && by < -m) || (ax > float(w_) + m && bx > float(w_) + m) ||
                (ay > float(h_) + m && by > float(h_) + m))
                continue;
            segment(ax, ay, bx, by, r, colour);
        }
    }
}

void Renderer::box(uint16_t* pixels, int w, int h, int x0, int y0, int x1, int y1, uint16_t colour) {
    x0 = std::max(x0, 0), y0 = std::max(y0, 0), x1 = std::min(x1, w), y1 = std::min(y1, h);
    for (int y = y0; y < y1; y++) std::fill(pixels + size_t(y) * size_t(w) + size_t(x0), pixels + size_t(y) * size_t(w) + size_t(x1), colour);
}

void Renderer::turn_icon(uint16_t* pixels, int w, int h, int kind, float cx, float cy, float size, uint16_t colour) {
    px_ = pixels, w_ = w, h_ = h;
    float s = size / 2, r = std::max(1.5f, size * 0.09f);
    auto P = [&](float x, float y) { return std::pair<float, float>(cx + x * s, cy + y * s); };
    auto stroke = [&](std::initializer_list<std::pair<float, float>> pts) {
        std::vector<std::pair<float, float>> v;
        for (auto& p : pts) v.push_back(P(p.first, p.second));
        for (size_t i = 0; i + 1 < v.size(); i++) segment(v[i].first, v[i].second, v[i + 1].first, v[i + 1].second, r, colour);
        // An arrow head on the last piece.
        auto [ax, ay] = v[v.size() - 2];
        auto [bx, by] = v.back();
        float dx = bx - ax, dy = by - ay, len = std::hypot(dx, dy);
        if (len <= 0) return;
        dx /= len, dy /= len;
        float hl = s * 0.55f, hw = s * 0.45f;
        float tip[6] = {bx + dx * hl * 0.6f, by + dy * hl * 0.6f, bx - dx * hl * 0.5f - dy * hw, by - dy * hl * 0.5f + dx * hw,
                        bx - dx * hl * 0.5f + dy * hw, by - dy * hl * 0.5f - dx * hw};
        uint32_t three = 3;
        poly(tip, &three, 1, colour);
    };
    switch (kind) {
        case 1: stroke({{0.3f, 1}, {0.3f, 0}, {-0.45f, 0}}); break;                    // left
        case 2: stroke({{-0.3f, 1}, {-0.3f, 0}, {0.45f, 0}}); break;                   // right
        case 3: case 9: stroke({{0.2f, 1}, {0.2f, 0.2f}, {-0.35f, -0.4f}}); break;     // slight left, exit left
        case 4: case 10: stroke({{-0.2f, 1}, {-0.2f, 0.2f}, {0.35f, -0.4f}}); break;   // slight right, exit right
        case 5: stroke({{0.35f, 1}, {0.35f, -0.5f}, {-0.35f, 0.3f}}); break;           // sharp left
        case 6: stroke({{-0.35f, 1}, {-0.35f, -0.5f}, {0.35f, 0.3f}}); break;          // sharp right
        case 7: stroke({{0.4f, 1}, {0.4f, -0.4f}, {0.2f, -0.7f}, {-0.2f, -0.7f}, {-0.4f, -0.4f}, {-0.4f, 0.3f}}); break;   // U-turn
        case 8: {   // roundabout: a ring, in from below, out to the upper right
            for (int i = 0; i < 24; i++) {
                float a0 = float(i) / 24 * 6.2832f, a1 = float(i + 1) / 24 * 6.2832f;
                auto p0 = P(0.38f * std::cos(a0), -0.15f + 0.38f * std::sin(a0));
                auto p1 = P(0.38f * std::cos(a1), -0.15f + 0.38f * std::sin(a1));
                segment(p0.first, p0.second, p1.first, p1.second, r * 0.8f, colour);
            }
            auto a = P(0, 1), b = P(0, 0.23f);
            segment(a.first, a.second, b.first, b.second, r, colour);
            stroke({{0.27f, -0.42f}, {0.6f, -0.75f}});
            break;
        }
        case 11: {   // arrive: a pin
            auto c0 = P(0, -0.25f);
            segment(c0.first, c0.second, c0.first, c0.second, s * 0.5f, colour);
            float tri[6] = {cx - s * 0.42f, cy - s * 0.0f, cx + s * 0.42f, cy - s * 0.0f, cx, cy + s * 0.95f};
            uint32_t three = 3;
            poly(tri, &three, 1, colour);
            break;
        }
        default: stroke({{0, 1}, {0, -0.55f}}); break;                                // straight on
    }
}

RoadHit nearest_road(const Map& map, double x, double y, double heading, double max_m) {
    RoadHit hit;
    if (!map.ok()) return hit;
    int li = map.levels() - 1;
    const Level& L = map.level(li);
    int shift = kWorldBits - int(L.zoom);
    double tile = double(int64_t(1) << shift), unit = tile / kTileExtent;
    double upm = units_per_metre(y_to_lat(y)), reach = max_m * upm;
    double best = 1e300;
    for (int64_t ty = int64_t(std::floor((y - reach) / tile)); ty <= int64_t(std::floor((y + reach) / tile)); ty++)
        for (int64_t tx = int64_t(std::floor((x - reach) / tile)); tx <= int64_t(std::floor((x + reach) / tile)); tx++) {
            const uint8_t* data;
            uint32_t size, count;
            if (!map.tile(li, tx, ty, data, size, count)) continue;
            double lx0 = (x - double(tx) * tile) / unit, ly0 = (y - double(ty) * tile) / unit;
            TileWalk walk(data, size, count);
            Feature f;
            while (walk.next(f)) {
                if (!is_road(f.head.kind)) continue;
                uint32_t at = 0;
                for (int part = 0; part < f.head.nparts; part++) {
                    uint32_t n = f.count(part);
                    int ax = 0, ay = 0;
                    for (uint32_t i = 0; i < n; i++) {
                        int bx, by;
                        f.point(at + i, bx, by);
                        if (i > 0) {
                            double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
                            double u = len2 > 0 ? std::clamp(((lx0 - ax) * dx + (ly0 - ay) * dy) / len2, 0.0, 1.0) : 0;
                            double m = std::hypot(lx0 - (ax + u * dx), ly0 - (ay + u * dy)) * unit / upm;
                            if (m <= max_m) {
                                double score = m + (f.head.kind == kService ? 5 : 0);
                                if (heading >= 0 && len2 > 0) {
                                    double bearing = std::atan2(dx, -dy) * 180 / kPi;
                                    double diff = std::fabs(std::fmod(bearing - heading + 540.0, 180.0) - 90.0);
                                    diff = 90.0 - diff;   // 0: along the road, 90: across it
                                    score += diff > 40 ? 25 : diff > 20 ? 8 : 0;
                                }
                                if (score < best) {
                                    best = score;
                                    hit.found = true;
                                    hit.name = map.str(f.head.name), hit.ref = map.str(f.head.ref);
                                    hit.speed = f.head.speed, hit.kind = f.head.kind, hit.metres = m;
                                }
                            }
                        }
                        ax = bx, ay = by;
                    }
                    at += n;
                }
            }
        }
    return hit;
}

}  // namespace zmap
