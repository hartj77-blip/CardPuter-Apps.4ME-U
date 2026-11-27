// A small reader for OpenStreetMap's .osm.pbf files: enough for
// zmap_build.cpp (nodes, ways, relations and their tags; no metadata).
// The format: https://wiki.openstreetmap.org/wiki/PBF_Format
// Needs zlib. Only zlib-compressed and raw blobs are read, which is what
// Geofabrik, planet.openstreetmap.org and osmium write by default.
#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pbf {

// A cursor over one protobuf message.
struct Msg {
    const uint8_t *p = nullptr, *end = nullptr;
    Msg() = default;
    Msg(const uint8_t* b, size_t n) : p(b), end(b + n) {}
    explicit Msg(std::string_view s) : p(reinterpret_cast<const uint8_t*>(s.data())), end(p + s.size()) {}

    bool more() const { return p < end; }
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p >= end) throw std::runtime_error("pbf: cut short");
            uint8_t b = *p++;
            v |= uint64_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        throw std::runtime_error("pbf: bad varint");
    }
    int64_t svarint() {
        uint64_t v = varint();
        return int64_t(v >> 1) ^ -int64_t(v & 1);
    }
    // The next field's number; its wire type goes to `wire`.
    uint32_t field(int& wire) {
        uint64_t k = varint();
        wire = int(k & 7);
        return uint32_t(k >> 3);
    }
    std::string_view bytes() {
        uint64_t n = varint();
        if (n > uint64_t(end - p)) throw std::runtime_error("pbf: cut short");
        std::string_view s(reinterpret_cast<const char*>(p), size_t(n));
        p += n;
        return s;
    }
    void skip(int wire) {
        switch (wire) {
            case 0: varint(); break;
            case 1: need(8); p += 8; break;
            case 2: bytes(); break;
            case 5: need(4); p += 4; break;
            default: throw std::runtime_error("pbf: unknown wire type");
        }
    }
    void need(size_t n) const {
        if (size_t(end - p) < n) throw std::runtime_error("pbf: cut short");
    }
};

using Tags = std::vector<std::pair<std::string_view, std::string_view>>;

inline std::string_view tag(const Tags& t, std::string_view key) {
    for (auto& kv : t)
        if (kv.first == key) return kv.second;
    return {};
}

struct Member {
    int64_t id;
    int type;                 // 0 node, 1 way, 2 relation
    std::string_view role;
};

struct Handler {
    // lat and lon in degrees. Tags are only valid during the call.
    std::function<void(int64_t id, double lat, double lon, const Tags&)> node;
    std::function<void(int64_t id, const std::vector<int64_t>& refs, const Tags&)> way;
    std::function<void(int64_t id, const std::vector<Member>& members, const Tags&)> relation;
    // The header's replication timestamp (seconds since 1970), if it has one.
    std::function<void(int64_t when)> timestamp;
};

namespace detail {

inline void packed_u(std::string_view s, std::vector<uint32_t>& out) {
    out.clear();
    for (Msg m(s); m.more();) out.push_back(uint32_t(m.varint()));
}

inline void block(std::string_view data, const Handler& h) {
    std::vector<std::string_view> strings;
    std::vector<std::string_view> groups;
    int64_t granularity = 100, lat_off = 0, lon_off = 0;
    for (Msg m(data); m.more();) {
        int w;
        switch (m.field(w)) {
            case 1:
                for (Msg st(m.bytes()); st.more();) {
                    int w2;
                    if (st.field(w2) == 1) strings.push_back(st.bytes());
                    else st.skip(w2);
                }
                break;
            case 2: groups.push_back(m.bytes()); break;
            case 17: granularity = int64_t(m.varint()); break;
            case 19: lat_off = int64_t(m.varint()); break;
            case 20: lon_off = int64_t(m.varint()); break;
            default: m.skip(w);
        }
    }
    auto str = [&](uint64_t i) { return i < strings.size() ? strings[size_t(i)] : std::string_view(); };
    auto deg = [&](int64_t v, int64_t off) { return 1e-9 * double(off + granularity * v); };
    Tags tags;
    std::vector<uint32_t> keys, vals;
    auto read_tags = [&] {
        tags.clear();
        for (size_t i = 0; i < keys.size() && i < vals.size(); i++) tags.emplace_back(str(keys[i]), str(vals[i]));
    };
    std::vector<int64_t> refs;
    std::vector<Member> members;
    for (std::string_view g : groups) {
        for (Msg m(g); m.more();) {
            int w;
            uint32_t f = m.field(w);
            if (f == 1 && h.node) {   // a plain Node
                int64_t id = 0, lat = 0, lon = 0;
                keys.clear();
                vals.clear();
                for (Msg n(m.bytes()); n.more();) {
                    int w2;
                    switch (n.field(w2)) {
                        case 1: id = n.svarint(); break;
                        case 2: packed_u(n.bytes(), keys); break;
                        case 3: packed_u(n.bytes(), vals); break;
                        case 8: lat = n.svarint(); break;
                        case 9: lon = n.svarint(); break;
                        default: n.skip(w2);
                    }
                }
                read_tags();
                h.node(id, deg(lat, lat_off), deg(lon, lon_off), tags);
            } else if (f == 2 && h.node) {   // DenseNodes
                std::string_view ids, lats, lons, kv;
                for (Msg d(m.bytes()); d.more();) {
                    int w2;
                    switch (d.field(w2)) {
                        case 1: ids = d.bytes(); break;
                        case 8: lats = d.bytes(); break;
                        case 9: lons = d.bytes(); break;
                        case 10: kv = d.bytes(); break;
                        default: d.skip(w2);
                    }
                }
                Msg mi(ids), mla(lats), mlo(lons), mkv(kv);
                int64_t id = 0, lat = 0, lon = 0;
                while (mi.more()) {
                    id += mi.svarint();
                    lat += mla.svarint();
                    lon += mlo.svarint();
                    tags.clear();
                    while (mkv.more()) {
                        uint64_t k = mkv.varint();
                        if (k == 0) break;
                        uint64_t v = mkv.varint();
                        tags.emplace_back(str(k), str(v));
                    }
                    h.node(id, deg(lat, lat_off), deg(lon, lon_off), tags);
                }
            } else if (f == 3 && h.way) {
                int64_t id = 0;
                keys.clear();
                vals.clear();
                refs.clear();
                for (Msg wy(m.bytes()); wy.more();) {
                    int w2;
                    switch (wy.field(w2)) {
                        case 1: id = int64_t(wy.varint()); break;
                        case 2: packed_u(wy.bytes(), keys); break;
                        case 3: packed_u(wy.bytes(), vals); break;
                        case 8: {
                            int64_t r = 0;
                            for (Msg rm(wy.bytes()); rm.more();) refs.push_back(r += rm.svarint());
                            break;
                        }
                        default: wy.skip(w2);
                    }
                }
                read_tags();
                h.way(id, refs, tags);
            } else if (f == 4 && h.relation) {
                int64_t id = 0;
                keys.clear();
                vals.clear();
                members.clear();
                std::vector<uint32_t> roles, types;
                std::vector<int64_t> ids;
                for (Msg r(m.bytes()); r.more();) {
                    int w2;
                    switch (r.field(w2)) {
                        case 1: id = int64_t(r.varint()); break;
                        case 2: packed_u(r.bytes(), keys); break;
                        case 3: packed_u(r.bytes(), vals); break;
                        case 8: packed_u(r.bytes(), roles); break;
                        case 9: {
                            int64_t v = 0;
                            for (Msg rm(r.bytes()); rm.more();) ids.push_back(v += rm.svarint());
                            break;
                        }
                        case 10: packed_u(r.bytes(), types); break;
                        default: r.skip(w2);
                    }
                }
                for (size_t i = 0; i < ids.size() && i < types.size() && i < roles.size(); i++)
                    members.push_back({ids[i], int(types[i]), str(roles[i])});
                read_tags();
                h.relation(id, members, tags);
            } else {
                m.skip(w);
            }
        }
    }
}

}  // namespace detail

// Read a whole file, calling the handler for everything in it, in the
// file's order (nodes, then ways, then relations). Throws std::runtime_error.
inline void read(const std::string& path, const Handler& h) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("can't open " + path);
    struct Closer {
        FILE* f;
        ~Closer() { std::fclose(f); }
    } closer{f};
    std::vector<uint8_t> head, blob, raw;
    for (;;) {
        uint8_t len4[4];
        size_t got = std::fread(len4, 1, 4, f);
        if (got == 0) break;
        if (got != 4) throw std::runtime_error("pbf: cut short");
        uint32_t hlen = uint32_t(len4[0]) << 24 | uint32_t(len4[1]) << 16 | uint32_t(len4[2]) << 8 | len4[3];
        if (hlen > 64 * 1024) throw std::runtime_error("pbf: not an .osm.pbf file");
        head.resize(hlen);
        if (std::fread(head.data(), 1, hlen, f) != hlen) throw std::runtime_error("pbf: cut short");
        std::string_view type;
        uint64_t datasize = 0;
        for (Msg m(head.data(), head.size()); m.more();) {
            int w;
            switch (m.field(w)) {
                case 1: type = m.bytes(); break;
                case 3: datasize = m.varint(); break;
                default: m.skip(w);
            }
        }
        if (datasize > 64u * 1024 * 1024) throw std::runtime_error("pbf: blob too large");
        blob.resize(size_t(datasize));
        if (std::fread(blob.data(), 1, blob.size(), f) != blob.size()) throw std::runtime_error("pbf: cut short");
        std::string_view plain, zdata;
        uint64_t raw_size = 0;
        for (Msg m(blob.data(), blob.size()); m.more();) {
            int w;
            switch (m.field(w)) {
                case 1: plain = m.bytes(); break;
                case 2: raw_size = m.varint(); break;
                case 3: zdata = m.bytes(); break;
                default:
                    if (w != 2) m.skip(w);
                    else throw std::runtime_error("pbf: a compression other than zlib; convert the file with osmium");
            }
        }
        std::string_view data = plain;
        if (!zdata.empty()) {
            if (raw_size > 64u * 1024 * 1024) throw std::runtime_error("pbf: blob too large");
            raw.resize(size_t(raw_size));
            uLongf n = uLongf(raw.size());
            if (uncompress(raw.data(), &n, reinterpret_cast<const Bytef*>(zdata.data()), uLong(zdata.size())) != Z_OK ||
                n != raw.size())
                throw std::runtime_error("pbf: a blob doesn't decompress");
            data = std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
        if (type == "OSMData") {
            detail::block(data, h);
        } else if (type == "OSMHeader" && h.timestamp) {
            for (Msg m(data); m.more();) {
                int w;
                if (m.field(w) == 32 && w == 0) h.timestamp(int64_t(m.varint()));
                else m.skip(w);
            }
        }
    }
}

}  // namespace pbf
