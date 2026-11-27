// "Go to": finding a place or a street in the map's search index, the
// .search file zmap-build writes beside the map (tools/zmap_build.cpp):
//   #zeroos-maps-search 1
//   kind<TAB>name<TAB>town<TAB>lat<TAB>lon      (sorted by name)
// The whole file is read once (about 7 MB for New Jersey) and searched in
// memory: every word typed must start a word of the name or the town.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zmap {

struct Place {
    char kind = 'r';     // c city, t town, v village, s suburb, n neighbourhood, r road
    std::string name, town;
    double lat = 0, lon = 0;
    double metres = -1;  // from where the search was made, if known
};

class SearchIndex {
public:
    // False if the file is missing or isn't an index.
    bool load(const std::string& path);
    bool ok() const { return !lines_.empty(); }
    size_t size() const { return lines_.size(); }

    // Up to `max` places matching `query`, best first: whole-name matches,
    // then towns before streets, then the nearest to (lat, lon) if have_near.
    std::vector<Place> find(const std::string& query, size_t max, bool have_near, double lat, double lon) const;

private:
    std::string text_, folded_;   // the file, and the same lower-cased with accents kept
    std::vector<uint32_t> lines_; // where each entry starts
};

// "washington st" -> {"washington", "st"}: lower case, split on spaces and
// punctuation.
std::vector<std::string> search_words(const std::string& s);

}  // namespace zmap
