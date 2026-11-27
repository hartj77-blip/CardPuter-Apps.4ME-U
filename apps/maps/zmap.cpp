// Opening a .zmap file (zmap.h). Everything the header claims is checked
// against the file's size before it is believed.
#include "apps/maps/zmap.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace zmap {

bool Map::open(const std::string& path) {
    close();
    path_ = path;
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error_ = "can't open it";
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < off_t(sizeof(Header))) {
        ::close(fd);
        error_ = "too small to be a map";
        return false;
    }
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) {
        error_ = "can't map it into memory";
        return false;
    }
    data_ = static_cast<const uint8_t*>(p);
    size_ = size_t(st.st_size);
    std::memcpy(&head_, data_, sizeof head_);
    auto fail = [this](const char* why) {
        error_ = why;
        close();
        return false;
    };
    if (std::memcmp(head_.magic, kMagic, sizeof kMagic) != 0) return fail("not a map file");
    if (head_.version != kVersion) return fail("made for another version of Maps");
    if (head_.nlevels == 0 || head_.nlevels > 8) return fail("damaged (levels)");
    if (size_ < sizeof(Header) + head_.nlevels * sizeof(Level)) return fail("damaged (cut short)");
    if (head_.strings_off > size_ || head_.strings_size > size_ - head_.strings_off || head_.strings_size == 0 ||
        data_[head_.strings_off + head_.strings_size - 1] != 0)
        return fail("damaged (names)");
    for (uint32_t i = 0; i < head_.nlevels; i++) {
        Level& l = levels_[i];
        std::memcpy(&l, data_ + sizeof(Header) + i * sizeof(Level), sizeof(Level));
        if (l.zoom > 20 || l.ntx == 0 || l.nty == 0 || l.ntx > 4096 || l.nty > 4096) return fail("damaged (grid)");
        uint64_t bytes = uint64_t(l.ntx) * l.nty * sizeof(TileRef);
        if (l.index_off > size_ || bytes > size_ - l.index_off) return fail("damaged (index)");
    }
    error_.clear();
    return true;
}

void Map::close() {
    if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    data_ = nullptr;
    size_ = 0;
}

bool Map::tile(int level, int64_t tx, int64_t ty, const uint8_t*& data, uint32_t& size, uint32_t& count) const {
    if (!data_ || level < 0 || level >= int(head_.nlevels)) return false;
    const Level& l = levels_[level];
    if (tx < l.tx0 || ty < l.ty0 || tx >= int64_t(l.tx0) + l.ntx || ty >= int64_t(l.ty0) + l.nty) return false;
    TileRef r;
    std::memcpy(&r, data_ + l.index_off + ((ty - l.ty0) * l.ntx + (tx - l.tx0)) * sizeof(TileRef), sizeof r);
    if (r.size == 0 || r.off > size_ || r.size > size_ - r.off) return false;
    data = data_ + r.off;
    size = r.size;
    count = r.count;
    return true;
}

}  // namespace zmap
