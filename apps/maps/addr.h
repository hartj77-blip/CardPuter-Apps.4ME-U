// House numbers for Go to: the .addr file zmap-build writes beside the map
// (tools/zmap_build.cpp, --addresses). The app maps it into memory and
// looks a number up on a street (search.cpp), so the two million addresses
// of New Jersey never have to be read in.
//
// Layout (little-endian):
//   AddrHeader
//   AddrStreet[nstreets]   sorted by their key (street_key() of the
//                          street's name, fold.h), so a street is found by bisection
//   Addr[naddrs]           each street's addresses one after another
//   strings                NUL-terminated UTF-8; offset 0 is ""
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zmap {

constexpr char kAddrMagic[8] = {'Z', 'E', 'R', 'O', 'A', 'D', 'R', '1'};

#pragma pack(push, 1)
struct AddrHeader {
    char magic[8];
    uint32_t nstreets, naddrs;
    uint64_t streets_off, addrs_off, strings_off, strings_size;
};
struct AddrStreet {
    uint32_t key;       // street_key(name) (fold.h), the sort order
    uint32_t name;      // the name as written ("Main Street")
    uint32_t first;     // its addresses: Addr[first .. first + count)
    uint32_t count;
};
struct Addr {
    int32_t lat, lon;   // millionths of a degree
    uint32_t number;    // as written: "12", "12A", "12-14"
    uint32_t city;      // its addr:city, or ""
};
#pragma pack(pop)

struct Address {
    std::string number, street, city;
    double lat = 0, lon = 0;
};

// An open .addr file. Every offset in it is checked before it is used, so
// a damaged file finds less, never crashes.
class AddressFile {
public:
    AddressFile() = default;
    ~AddressFile() { close(); }
    AddressFile(const AddressFile&) = delete;
    AddressFile& operator=(const AddressFile&) = delete;

    bool open(const std::string& path);
    void close();
    bool ok() const { return data_ != nullptr; }
    size_t size() const { return ok() ? head_.naddrs : 0; }

    // The addresses with this number on streets whose street_key() is
    // `street_key`. "12" finds "12" first; if there is none, numbers that
    // start with it ("12A", "12-14").
    std::vector<Address> find(const std::string& street_key, const std::string& number) const;

private:
    const char* str(uint32_t off) const;
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    AddrHeader head_{};
};

}  // namespace zmap
