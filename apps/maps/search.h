// "Go to": finding a place or a street in the map's search index, the
// .search file zmap-build writes beside the map (tools/zmap_build.cpp):
//   #zeroos-maps-search 1
//   kind<TAB>name<TAB>town<TAB>lat<TAB>lon      (sorted by name)
// The whole file is read once (about 7 MB for New Jersey) and searched in
// memory: every word typed must start a word of the name or the town.
//
// House numbers: when the query starts with one ("12 main st princeton"),
// the streets that match the rest are found as usual, and the number is
// looked up on each in the .addr file (addr.h), near where that street is.
//
// "gas", "food" or "parking" (and the like: search_category()) lists the
// nearest of those, narrowed by any other words ("gas princeton").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "apps/maps/addr.h"

namespace zmap {

struct Place {
    char kind = 'r';     // c city, t town, v village, s suburb, n neighbourhood, r road, a an address,
                         // g gas station, f food, p parking
    std::string name, town;
    double lat = 0, lon = 0;
    double metres = -1;  // from where the search was made, if known
};

class SearchIndex {
public:
    // False if the file is missing or isn't an index.
    bool load(const std::string& path);
    bool ok() const { return !lines_.empty(); }
    // The house numbers, if the map has them. False if not (Go to still
    // finds towns and streets).
    bool load_addresses(const std::string& path) { return addr_.open(path); }
    bool has_addresses() const { return addr_.ok(); }
    size_t size() const { return lines_.size(); }

    // Up to `max` places matching `query`, best first: whole-name matches,
    // then towns before streets, then the nearest to (lat, lon) if have_near.
    std::vector<Place> find(const std::string& query, size_t max, bool have_near, double lat, double lon) const;

private:
    std::vector<Place> find_words(const std::vector<std::string>& words, size_t max, bool have_near, double lat, double lon,
                                  bool roads_only, const std::string& kinds = "") const;
    AddressFile addr_;
    std::string text_, folded_;   // the file, and the same lower-cased with accents kept
    std::vector<uint32_t> lines_; // where each entry starts
};

// The kind a word asks for: 'g' for "gas", "fuel"...; 'f' for "food",
// "coffee"...; 'p' for "parking"; 0 for any other word.
char search_category(const std::string& folded_word);

// "washington st" -> {"washington", "st"}: lower case, split on spaces and
// punctuation.
std::vector<std::string> search_words(const std::string& s);

}  // namespace zmap
