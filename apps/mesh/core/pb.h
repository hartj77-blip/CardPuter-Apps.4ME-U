// Protocol Buffers on the wire, just enough for Meshtastic's client API:
// a writer, a reader, and "change one field, keep the rest" for settings we
// only partly understand (a newer meshtasticd may have fields we don't know;
// they must survive being read and written back). No protobuf library: the
// app is a module that links nothing, and the daemon stays small.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace zs::mesh::pb {

enum Wire { kVarint = 0, kFixed64 = 1, kBytes = 2, kFixed32 = 5 };

class Writer {
public:
    // Scalars are left out when zero, as proto3 does. The *_always forms
    // write the field whatever its value (proto3 "optional").
    void varint(uint32_t field, uint64_t v) {
        if (v) varint_always(field, v);
    }
    void varint_always(uint32_t field, uint64_t v) {
        tag(field, kVarint);
        raw_varint(v);
    }
    void int32(uint32_t field, int32_t v) { varint(field, uint64_t(int64_t(v))); }   // negative: 10 bytes
    void int32_always(uint32_t field, int32_t v) { varint_always(field, uint64_t(int64_t(v))); }
    void sint32_always(uint32_t field, int32_t v) {
        varint_always(field, (uint32_t(v) << 1) ^ uint32_t(v >> 31));
    }
    void boolean(uint32_t field, bool v) { varint(field, v ? 1 : 0); }
    void fixed32(uint32_t field, uint32_t v) {
        if (v) fixed32_always(field, v);
    }
    void fixed32_always(uint32_t field, uint32_t v) {
        tag(field, kFixed32);
        raw_fixed32(v);
    }
    void sfixed32_always(uint32_t field, int32_t v) { fixed32_always(field, uint32_t(v)); }
    void f32(uint32_t field, float v) {
        if (v != 0.0f) f32_always(field, v);
    }
    void f32_always(uint32_t field, float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        fixed32_always(field, u);
    }
    void bytes(uint32_t field, std::string_view v) {
        if (!v.empty()) bytes_always(field, v);
    }
    void bytes_always(uint32_t field, std::string_view v) {
        tag(field, kBytes);
        raw_varint(v.size());
        out_.append(v);
    }
    // A sub-message, even an empty one (its presence means something).
    void sub(uint32_t field, const Writer& w) { bytes_always(field, w.out_); }

    void raw_varint(uint64_t v) {
        while (v >= 0x80) {
            out_.push_back(char(v | 0x80));
            v >>= 7;
        }
        out_.push_back(char(v));
    }
    void raw_fixed32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out_.push_back(char(v >> (8 * i)));
    }
    void raw(std::string_view v) { out_.append(v); }

    const std::string& str() const { return out_; }
    std::string take() { return std::move(out_); }

private:
    void tag(uint32_t field, Wire w) { raw_varint((uint64_t(field) << 3) | uint64_t(w)); }
    std::string out_;
};

struct Field {
    uint32_t num = 0;
    int wire = 0;
    uint64_t v = 0;               // kVarint, kFixed32, kFixed64
    std::string_view data;        // kBytes
    std::string_view raw;         // the whole field as it was, tag included

    uint32_t u32() const { return uint32_t(v); }
    int32_t i32() const { return int32_t(uint32_t(v)); }
    int32_t s32() const { return int32_t((uint32_t(v) >> 1) ^ (~(uint32_t(v) & 1) + 1)); }
    bool b() const { return v != 0; }
    float f() const {
        uint32_t u = uint32_t(v);
        float x;
        std::memcpy(&x, &u, 4);
        return x;
    }
    std::string str() const { return std::string(data); }
};

// Walks the fields of one message. Stops (ok() false) on anything malformed;
// what was read before that still stands.
class Reader {
public:
    explicit Reader(std::string_view s) : p_(s.data()), end_(s.data() + s.size()) {}

    bool next(Field& f) {
        if (bad_ || p_ >= end_) return false;
        const char* start = p_;
        uint64_t key;
        if (!varint(key)) return fail();
        f.num = uint32_t(key >> 3);
        f.wire = int(key & 7);
        f.v = 0;
        f.data = {};
        if (f.num == 0) return fail();
        switch (f.wire) {
        case kVarint:
            if (!varint(f.v)) return fail();
            break;
        case kFixed64:
            if (end_ - p_ < 8) return fail();
            for (int i = 0; i < 8; ++i) f.v |= uint64_t(uint8_t(p_[i])) << (8 * i);
            p_ += 8;
            break;
        case kFixed32:
            if (end_ - p_ < 4) return fail();
            for (int i = 0; i < 4; ++i) f.v |= uint64_t(uint8_t(p_[i])) << (8 * i);
            p_ += 4;
            break;
        case kBytes: {
            uint64_t n;
            if (!varint(n) || n > uint64_t(end_ - p_)) return fail();
            f.data = std::string_view(p_, size_t(n));
            p_ += n;
            break;
        }
        default: return fail();   // groups: not used by Meshtastic
        }
        f.raw = std::string_view(start, size_t(p_ - start));
        return true;
    }
    bool ok() const { return !bad_; }

private:
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 70 && p_ < end_; shift += 7) {
            uint8_t b = uint8_t(*p_++);
            if (shift < 64) v |= uint64_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    bool fail() {
        bad_ = true;
        return false;
    }
    const char* p_;
    const char* end_;
    bool bad_ = false;
};

// A repeated scalar: packed (one length-delimited field) or one field each.
template <typename Fn> inline void each_fixed32(const Field& f, Fn fn) {
    if (f.wire == kFixed32) {
        fn(f.u32());
    } else if (f.wire == kBytes) {
        for (size_t i = 0; i + 4 <= f.data.size(); i += 4) {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) v |= uint32_t(uint8_t(f.data[i + k])) << (8 * k);
            fn(v);
        }
    }
}
template <typename Fn> inline void each_varint(const Field& f, Fn fn) {
    if (f.wire == kVarint) {
        fn(f.v);
    } else if (f.wire == kBytes) {
        size_t i = 0;
        while (i < f.data.size()) {
            uint64_t v = 0;
            int shift = 0;
            bool done = false;
            while (i < f.data.size() && shift < 70) {
                uint8_t b = uint8_t(f.data[i++]);
                if (shift < 64) v |= uint64_t(b & 0x7f) << shift;
                shift += 7;
                if (!(b & 0x80)) {
                    done = true;
                    break;
                }
            }
            if (!done) return;
            fn(v);
        }
    }
}

// ---- Change one field of an encoded message, keeping every other field.

// The message without `field`.
inline std::string without(std::string_view msg, uint32_t field) {
    std::string out;
    Reader r(msg);
    Field f;
    while (r.next(f))
        if (f.num != field) out.append(f.raw);
    return out;
}
inline std::string with_varint(std::string_view msg, uint32_t field, uint64_t v) {
    Writer w;
    w.raw(without(msg, field));
    w.varint(field, v);
    return w.take();
}
inline std::string with_bytes(std::string_view msg, uint32_t field, std::string_view v) {
    Writer w;
    w.raw(without(msg, field));
    w.bytes(field, v);
    return w.take();
}
inline std::string with_fixed32(std::string_view msg, uint32_t field, uint32_t v) {
    Writer w;
    w.raw(without(msg, field));
    w.fixed32(field, v);
    return w.take();
}
// The first sub-message (or bytes) in `field`, or "".
inline std::string_view find_bytes(std::string_view msg, uint32_t field) {
    Reader r(msg);
    Field f;
    while (r.next(f))
        if (f.num == field && f.wire == kBytes) return f.data;
    return {};
}
inline bool has_field(std::string_view msg, uint32_t field) {
    Reader r(msg);
    Field f;
    while (r.next(f))
        if (f.num == field) return true;
    return false;
}
inline uint64_t find_varint(std::string_view msg, uint32_t field, uint64_t fallback = 0) {
    Reader r(msg);
    Field f;
    uint64_t v = fallback;
    while (r.next(f))
        if (f.num == field && f.wire == kVarint) v = f.v;   // the last one wins
    return v;
}

}  // namespace zs::mesh::pb
