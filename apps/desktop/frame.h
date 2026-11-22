// The remote screen, shared between the Desktop app and its viewer process
// (view/). The viewer makes a file in /dev/shm for each desktop size, tells
// the app its path on stdout ("frame PATH W H"), and keeps drawing into it.
// The app maps it read-only and removes the name. A resize is always a new
// file, never a truncated one, so the app can't fault on a mapping that
// shrank under it.
//
// Pixels are RGB565, little endian: what LVGL draws with here.
#pragma once

#include <cstdint>

namespace zs::desktop {

constexpr uint32_t kFrameMagic = 0x5a444631;   // "ZDF1"

struct FrameHeader {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;    // bytes per row
    uint32_t seq;       // bumped after every update (read with __atomic_load_n)
    uint32_t reserved[11];
};
static_assert(sizeof(FrameHeader) == 64, "frame header is 64 bytes");

inline uint16_t* frame_pixels(FrameHeader* h) {
    return reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(h) + sizeof(FrameHeader));
}
inline const uint16_t* frame_pixels(const FrameHeader* h) {
    return reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(h) + sizeof(FrameHeader));
}
inline uint64_t frame_bytes(uint32_t w, uint32_t h) { return sizeof(FrameHeader) + uint64_t(w) * h * 2; }

}  // namespace zs::desktop
