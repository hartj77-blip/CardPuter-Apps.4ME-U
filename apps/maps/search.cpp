// search.h
#include "apps/maps/search.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

#include "apps/maps/fold.h"

namespace zmap {
namespace {

char fold(char c) { return fold_char(c); }

int kind_rank(char k) {
    switch (k) {
        case 'c': return 0;
        case 't': return 1;
        case 'v': return 2;
        case 's': return 3;
        case 'n': return 4;
        default: return 5;
    }
}

double metres(double la1, double lo1, double la2, double lo2) {
    double k = 3.14159265358979323846 / 180, m = (la1 + la2) / 2 * k;
    double dx = (lo2 - lo1) * k * std::cos(m) * 6371008.8, dy = (la2 - la1) * k * 6371008.8;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

char search_category(const std::string& w) {
    static const char* const kGas[] = {"gas", "fuel", "petrol", "gasoline", nullptr};
    static const char* const kFood[] = {"food", "restaurant", "restaurants", "eat", "coffee", "cafe", "breakfast", "lunch", "dinner", nullptr};
    static const char* const kParking[] = {"parking", nullptr};
    for (auto [list, kind] : {std::pair{kGas, 'g'}, std::pair{kFood, 'f'}, std::pair{kParking, 'p'}})
        for (const char* const* k = list; *k; k++)
            if (w == *k) return kind;
    return 0;
}

std::vector<std::string> search_words(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        char f = fold(c);
        if (f == ' ' || f == '\t' || f == '\n') {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += f;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

bool SearchIndex::load(const std::string& path) {
    text_.clear();
    folded_.clear();
    lines_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    text_ = ss.str();
    if (text_.rfind("#zeroos-maps-search 1\n", 0) != 0 || text_.size() > 512u * 1024 * 1024) {
        text_.clear();
        return false;
    }
    if (text_.back() != '\n') text_ += '\n';
    folded_.resize(text_.size());
    std::transform(text_.begin(), text_.end(), folded_.begin(), fold);
    for (size_t at = text_.find('\n') + 1; at < text_.size();) {
        size_t end = text_.find('\n', at);
        // kind TAB name TAB town TAB lat TAB lon: four tabs, a known kind.
        if (end - at > 6 && text_[at + 1] == '\t' && std::count(text_.begin() + long(at), text_.begin() + long(end), '\t') == 4 &&
            std::strchr("ctvsnrgfp", text_[at]))
            lines_.push_back(uint32_t(at));
        at = end + 1;
    }
    if (lines_.empty()) text_.clear();
    return ok();
}

std::vector<Place> SearchIndex::find(const std::string& query, size_t max, bool have_near, double lat, double lon) const {
    std::vector<std::string> words = search_words(query);
    if (words.empty() || !ok()) return {};
    // "gas", "food", "parking": the nearest of those, then what the words find anyway.
    std::string kinds, others_text;
    std::vector<std::string> others;
    for (const std::string& w : words) {
        char k = search_category(w);
        if (k && kinds.find(k) == std::string::npos) kinds += k;
        else if (!k) others.push_back(w);
    }
    if (!kinds.empty()) {
        std::vector<Place> out = find_words(others, max, have_near, lat, lon, false, kinds);
        std::stable_sort(out.begin(), out.end(), [](const Place& a, const Place& b) { return a.metres < b.metres; });
        for (Place& p : find_words(words, max, have_near, lat, lon, false)) {
            bool seen = false;
            for (const Place& o : out) seen |= o.name == p.name && o.town == p.town && o.kind == p.kind;
            if (!seen && out.size() < max) out.push_back(std::move(p));
        }
        return out;
    }
    // "12 main st princeton": a house number first, and then a street.
    if (words.size() >= 2 && std::isdigit(uint8_t(words[0][0])) && addr_.ok()) {
        std::vector<std::string> rest(words.begin() + 1, words.end());
        std::vector<Place> out;
        for (const Place& street : find_words(rest, 20, have_near, lat, lon, true)) {
            // That number on a street of that name, nearest where the street is (within 8 km).
            const Address* best = nullptr;
            double best_m = 8000;
            std::vector<Address> found = addr_.find(street_key(street.name), words[0]);
            for (const Address& a : found) {
                double m = metres(street.lat, street.lon, a.lat, a.lon);
                if (m < best_m) best_m = m, best = &a;
            }
            if (!best) continue;
            bool seen = false;
            for (const Place& o : out) seen |= std::fabs(o.lat - best->lat) < 1e-6 && std::fabs(o.lon - best->lon) < 1e-6;
            if (seen) continue;
            Place p;
            p.kind = 'a';
            p.name = best->number + " " + best->street;
            p.town = best->city.empty() ? street.town : best->city;
            p.lat = best->lat, p.lon = best->lon;
            p.metres = have_near ? metres(lat, lon, p.lat, p.lon) : -1;
            out.push_back(std::move(p));
        }
        std::stable_sort(out.begin(), out.end(), [](const Place& a, const Place& b) { return a.metres < b.metres; });
        if (out.size() > max) out.resize(max);
        // Then what the whole query finds ("1st st hoboken" is a street, not
        // number 1st), then the streets without the number, for a number
        // that isn't on the map.
        for (const std::vector<std::string>* w : {&words, &rest})
            for (Place& p : find_words(*w, max, have_near, lat, lon, false)) {
                bool seen = false;
                for (const Place& o : out) seen |= o.name == p.name && o.town == p.town && o.kind == p.kind;
                if (!seen && out.size() < max) out.push_back(std::move(p));
            }
        return out;
    }
    return find_words(words, max, have_near, lat, lon, false);
}

std::vector<Place> SearchIndex::find_words(const std::vector<std::string>& words, size_t max, bool have_near, double lat,
                                           double lon, bool roads_only, const std::string& kinds) const {
    std::vector<Place> out;
    if (words.empty() && kinds.empty()) return out;
    struct Hit {
        uint32_t line;
        int score;
        double metres;
    };
    std::vector<Hit> hits;
    std::string whole;
    for (const std::string& w : words) whole += (whole.empty() ? "" : " ") + w;
    for (uint32_t at : lines_) {
        if (roads_only && folded_[at] != 'r') continue;
        if (!kinds.empty() && kinds.find(folded_[at]) == std::string::npos) continue;
        size_t end = folded_.find('\n', at);
        // Every word typed starts a word of the name or the town.
        // Only the name and the town: not the numbers after them.
        size_t town_end = folded_.find('\t', folded_.find('\t', at + 2) + 1);
        std::string_view line(folded_.data() + at, std::min(end, town_end) - at);
        bool all = true;
        for (const std::string& w : words) {
            bool found = false;
            for (size_t p = 2; !found && (p = line.find(w, p)) != std::string_view::npos; p++)
                found = line[p - 1] == ' ' || line[p - 1] == '\t';
            if (!found) {
                all = false;
                break;
            }
        }
        if (!all) continue;
        size_t name_end = folded_.find('\t', at + 2);
        std::string name = folded_.substr(at + 2, name_end - at - 2);
        // Lower is better: the name is what was typed, then starts with it,
        // then towns before streets.
        int score = kind_rank(folded_[at]);
        std::string compact;
        for (const std::string& w : search_words(name)) compact += (compact.empty() ? "" : " ") + w;
        if (compact == whole) score -= 20;
        else if (compact.rfind(whole, 0) == 0) score -= 10;
        double m = -1;
        if (have_near) {
            // lat comes after kind, name and town.
            const char* p = text_.c_str() + text_.find('\t', name_end + 1) + 1;
            char* e = nullptr;
            double la = std::strtod(p, &e);
            double lo = e ? std::strtod(e + 1, nullptr) : 0;
            m = metres(lat, lon, la, lo);
        }
        hits.push_back({at, score, m});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score < b.score;
        if (a.metres != b.metres) return a.metres < b.metres;
        return a.line < b.line;
    });
    for (size_t i = 0; i < hits.size() && out.size() < max; i++) {
        size_t at = hits[i].line, end = text_.find('\n', at);
        std::string line = text_.substr(at, end - at);
        Place pl;
        pl.kind = line[0];
        size_t a = 2, b = line.find('\t', a);
        pl.name = line.substr(a, b - a);
        size_t c = line.find('\t', b + 1);
        pl.town = line.substr(b + 1, c - b - 1);
        char* e = nullptr;
        pl.lat = std::strtod(line.c_str() + c + 1, &e);
        pl.lon = e ? std::strtod(e + 1, nullptr) : 0;
        pl.metres = hits[i].metres;
        out.push_back(std::move(pl));
    }
    return out;
}

}  // namespace zmap
