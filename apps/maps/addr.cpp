// addr.h
#include "apps/maps/addr.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "apps/maps/fold.h"

namespace zmap {

bool AddressFile::open(const std::string& path) {
    close();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < off_t(sizeof(AddrHeader))) {
        ::close(fd);
        return false;
    }
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) return false;
    data_ = static_cast<const uint8_t*>(p);
    size_ = size_t(st.st_size);
    std::memcpy(&head_, data_, sizeof head_);
    auto inside = [&](uint64_t off, uint64_t bytes) { return off <= size_ && bytes <= size_ - off; };
    bool good = std::memcmp(head_.magic, kAddrMagic, sizeof kAddrMagic) == 0 &&
                inside(head_.streets_off, uint64_t(head_.nstreets) * sizeof(AddrStreet)) &&
                inside(head_.addrs_off, uint64_t(head_.naddrs) * sizeof(Addr)) &&
                inside(head_.strings_off, head_.strings_size) && head_.strings_size > 0 &&
                data_[head_.strings_off + head_.strings_size - 1] == 0;
    if (!good) close();
    return ok();
}

void AddressFile::close() {
    if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    data_ = nullptr;
    size_ = 0;
    head_ = AddrHeader{};
}

const char* AddressFile::str(uint32_t off) const {
    return off < head_.strings_size ? reinterpret_cast<const char*>(data_ + head_.strings_off + off) : "";
}

std::vector<Address> AddressFile::find(const std::string& street_key, const std::string& number) const {
    std::vector<Address> out;
    if (!ok() || street_key.empty() || number.empty()) return out;
    auto street = [&](uint32_t i) {
        AddrStreet s;
        std::memcpy(&s, data_ + head_.streets_off + uint64_t(i) * sizeof s, sizeof s);
        return s;
    };
    // The first street whose key isn't less than the one wanted.
    uint32_t lo = 0, hi = head_.nstreets;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (std::strcmp(str(street(mid).key), street_key.c_str()) < 0) lo = mid + 1;
        else hi = mid;
    }
    std::string want = fold_key(number);
    for (int pass = 0; pass < 2 && out.empty(); pass++) {   // exact numbers, then ones that start with it
        for (uint32_t i = lo; i < head_.nstreets; i++) {
            AddrStreet s = street(i);
            if (street_key != str(s.key)) break;
            if (s.first > head_.naddrs || s.count > head_.naddrs - s.first) continue;
            for (uint32_t k = 0; k < s.count && out.size() < 50; k++) {
                Addr a;
                std::memcpy(&a, data_ + head_.addrs_off + uint64_t(s.first + k) * sizeof a, sizeof a);
                std::string n = fold_key(str(a.number));
                if (pass == 0 ? n != want : n.compare(0, want.size(), want) != 0 || n.size() == want.size()) continue;
                // "12a" from "12", not "120": the next character isn't a digit.
                if (pass == 1 && std::isdigit(uint8_t(n[want.size()]))) continue;
                Address ad;
                ad.number = str(a.number);
                ad.street = str(s.name);
                ad.city = str(a.city);
                ad.lat = a.lat / 1e6;
                ad.lon = a.lon / 1e6;
                out.push_back(std::move(ad));
            }
        }
    }
    return out;
}

}  // namespace zmap
