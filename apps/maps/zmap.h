// The .zmap file: an offline vector map for the Maps app (README.md).
// Written by tools/zmap_build.cpp from OpenStreetMap data, read here by
// mapping the file into memory, so a 60 MB map costs only the pages the
// screen is looking at.
//
// Coordinates are Web Mercator, with the whole world 2^30 units across
// (about 3 cm a unit at New Jersey's latitude); y grows southwards.
//
// The map is stored at several levels of detail. A level is a grid of
// tiles at one zoom (a tile at zoom z is 2^(30-z) units wide). Features are
// cut at tile edges, and their points are stored relative to the tile's
// corner in 1/8192 of a tile (int16). The renderer picks the level that
// suits the scale on screen and draws the tiles in view.
//
// Layout (little-endian):
//   Header
//   Level[nlevels]
//   per level: TileRef[ntx * nty], row by row
//   tiles: features one after another, sorted by kind
//   strings: NUL-terminated UTF-8; offset 0 is the empty string
//
// A feature: FeatureHead, then nparts uint32 point counts, then the points
// (int16 x, int16 y), part after part. A line has one or more runs; an
// area has one or more rings, filled even-odd; a place has one point.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace zmap {

constexpr char kMagic[8] = {'Z', 'E', 'R', 'O', 'M', 'A', 'P', '1'};
constexpr uint32_t kVersion = 1;
constexpr int kWorldBits = 30;
constexpr int kTileBits = 13;                 // a tile is 8192 local units
constexpr int kTileExtent = 1 << kTileBits;
constexpr uint32_t kFlagImperial = 1;         // speeds and distances in miles by default

// What a feature is. The order is the drawing order: areas, lines, roads
// from the smallest to the largest, then places.
enum Kind : uint8_t {
    kCoverage = 1,   // the region the map covers; everything else is "no data"
    kSea,
    kForest,
    kPark,
    kWetland,
    kSand,
    kAirport,
    kWater,          // lakes, wide rivers
    kBuilding,       // only in the finest level
    kLastArea = kBuilding,

    kStream = 20,
    kRiver,
    kBoundary,       // state line
    kFerry,
    kRail,
    kRunway,
    kService = 30,
    kResidential,    // also unclassified and living streets
    kTertiary,
    kSecondary,
    kPrimary,
    kTrunk,
    kMotorway,
    kLastLine = kMotorway,

    kNeighbourhood = 50,
    kSuburb,         // also hamlets
    kVillage,
    kTown,
    kCity,
};

inline bool is_area(uint8_t k) { return k >= kCoverage && k <= kLastArea; }
inline bool is_line(uint8_t k) { return k >= kStream && k <= kLastLine; }
inline bool is_road(uint8_t k) { return k >= kService && k <= kMotorway; }
inline bool is_place(uint8_t k) { return k >= kNeighbourhood && k <= kCity; }

#pragma pack(push, 1)
struct Header {
    char magic[8];
    uint32_t version;
    uint32_t nlevels;
    int32_t min_x, min_y, max_x, max_y;   // what the map covers, world units
    uint32_t flags;
    uint32_t reserved;
    uint64_t strings_off, strings_size;
    char name[40];                        // "New Jersey"
    char date[24];                        // of the OpenStreetMap data, "2026-11-26"
};

struct Level {
    uint32_t zoom;
    uint32_t tx0, ty0, ntx, nty;          // the grid's first tile and size
    uint32_t reserved;
    uint64_t index_off;                   // TileRef[ntx * nty]
};

struct TileRef {
    uint64_t off;
    uint32_t size;                        // bytes; 0 = empty tile
    uint32_t count;                       // features
};

struct FeatureHead {
    uint8_t kind;
    uint8_t speed;                        // roads: the limit; bit 7 set = mph, else km/h; 0 = unknown
    uint16_t nparts;
    uint32_t name;                        // string offsets
    uint32_t ref;                         // roads: "I 95", "NJ 18"
};
#pragma pack(pop)

// ---- Mercator ----------------------------------------------------------
constexpr double kWorld = double(1 << kWorldBits);
constexpr double kPi = 3.14159265358979323846;

inline double lon_to_x(double lon) { return (lon + 180.0) / 360.0 * kWorld; }
inline double lat_to_y(double lat) {
    double s = std::sin(lat * kPi / 180.0);
    return (0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi)) * kWorld;
}
inline double x_to_lon(double x) { return x / kWorld * 360.0 - 180.0; }
inline double y_to_lat(double y) {
    double n = kPi - 2 * kPi * y / kWorld;
    return 180.0 / kPi * std::atan(std::sinh(n));
}
// World units in a metre on the ground at this latitude.
inline double units_per_metre(double lat) { return kWorld / (40075016.686 * std::cos(lat * kPi / 180.0)); }

// ---- Reading -----------------------------------------------------------
// One feature in a tile, as the reader hands it out. Points stay in the
// file: pts[2*i], pts[2*i+1] are local x and y of point i.
struct Feature {
    FeatureHead head;
    const uint8_t* counts = nullptr;   // nparts * uint32 (unaligned)
    const uint8_t* pts = nullptr;      // int16 pairs (unaligned)
    uint32_t npoints = 0;

    uint32_t count(int part) const {
        uint32_t n;
        std::memcpy(&n, counts + 4 * part, 4);
        return n;
    }
    void point(uint32_t i, int& x, int& y) const {
        int16_t v[2];
        std::memcpy(v, pts + 4 * i, 4);
        x = v[0];
        y = v[1];
    }
};

// A tile's features, walked front to back. Every read is checked against
// the end of the tile, so a damaged file ends the walk and nothing more.
class TileWalk {
public:
    TileWalk(const uint8_t* p, size_t size, uint32_t count) : p_(p), end_(p + size), left_(count) {}
    bool next(Feature& f) {
        if (left_ == 0 || size_t(end_ - p_) < sizeof(FeatureHead)) return false;
        std::memcpy(&f.head, p_, sizeof(FeatureHead));
        const uint8_t* q = p_ + sizeof(FeatureHead);
        if (size_t(end_ - q) < size_t(f.head.nparts) * 4) return false;
        f.counts = q;
        uint64_t n = 0;
        for (int i = 0; i < f.head.nparts; i++) n += f.count(i);
        q += size_t(f.head.nparts) * 4;
        if (n > uint64_t(end_ - q) / 4) return false;
        f.pts = q;
        f.npoints = uint32_t(n);
        p_ = q + n * 4;
        left_--;
        return true;
    }

private:
    const uint8_t *p_, *end_;
    uint32_t left_;
};

// An open map file. Not copyable; the file stays mapped until it is closed.
class Map {
public:
    Map() = default;
    ~Map() { close(); }
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    // False (and why in error()) if the file is missing or isn't a map.
    bool open(const std::string& path);
    void close();
    bool ok() const { return data_ != nullptr; }
    const std::string& error() const { return error_; }
    const std::string& path() const { return path_; }

    const Header& header() const { return head_; }
    std::string name() const { return std::string(head_.name, strnlen(head_.name, sizeof head_.name)); }
    std::string date() const { return std::string(head_.date, strnlen(head_.date, sizeof head_.date)); }
    bool imperial() const { return head_.flags & kFlagImperial; }
    int levels() const { return int(head_.nlevels); }
    const Level& level(int i) const { return levels_[i]; }
    bool contains(double x, double y) const {
        return x >= head_.min_x && x <= head_.max_x && y >= head_.min_y && y <= head_.max_y;
    }

    // The tile at (tx, ty) of a level, by world tile numbers. False if it
    // is outside the grid or empty.
    bool tile(int level, int64_t tx, int64_t ty, const uint8_t*& data, uint32_t& size, uint32_t& count) const;
    // "" for 0 or anything out of range.
    const char* str(uint32_t off) const {
        return off && off < head_.strings_size ? reinterpret_cast<const char*>(data_ + head_.strings_off + off) : "";
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    Header head_{};
    Level levels_[8]{};
    std::string error_, path_;
};

}  // namespace zmap
