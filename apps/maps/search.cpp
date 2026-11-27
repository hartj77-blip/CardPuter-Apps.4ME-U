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

namespace zmap {
namespace {

char fold(char c) {
    if (c >= 'A' && c <= 'Z') return char(c - 'A' + 'a');
    if (c == '\t' || c == '\n') return c;
    if (uint8_t(c) < 0x80 && !std::isalnum(uint8_t(c))) return ' ';   // "St." matches "st"
    return c;
}

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
            std::strchr("ctvsnr", text_[at]))
            lines_.push_back(uint32_t(at));
        at = end + 1;
    }
    if (lines_.empty()) text_.clear();
    return ok();
}

std::vector<Place> SearchIndex::find(const std::string& query, size_t max, bool have_near, double lat, double lon) const {
    std::vector<std::string> words = search_words(query);
    std::vector<Place> out;
    if (words.empty() || !ok()) return out;
    struct Hit {
        uint32_t line;
        int score;
        double metres;
    };
    std::vector<Hit> hits;
    std::string whole;
    for (const std::string& w : words) whole += (whole.empty() ? "" : " ") + w;
    for (uint32_t at : lines_) {
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
