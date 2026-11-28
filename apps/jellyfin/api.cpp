// See api.h.
#include "apps/jellyfin/api.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace zs::jfapp {
namespace {

// ---- a small JSON reader: just enough to walk objects and arrays ----------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;                             // Array
    std::vector<std::pair<std::string, Json>> fields;    // Object

    const Json& operator[](const char* key) const {
        static const Json none;
        for (auto& f : fields)
            if (f.first == key) return f.second;
        return none;
    }
    const std::string& text() const { return str; }
    // The first element of an array of strings, or "".
    const std::string& first() const {
        static const std::string none;
        return type == Array && !items.empty() ? items[0].str : none;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Json& out) {
        bool ok = value(out, 0);
        ws();
        return ok && i_ == s_.size();
    }

private:
    void ws() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    }
    bool lit(const char* word) {
        size_t n = std::strlen(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) {
            o += char(cp);
        } else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char ch = s_[i_++];
            if (!std::isxdigit((unsigned char)ch)) return false;
            v = v * 16 + unsigned(std::isdigit((unsigned char)ch) ? ch - '0' : std::tolower(ch) - 'a' + 10);
        }
        return true;
    }
    bool string(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char ch = s_[i_++];
            if (ch == '"') return true;
            if (ch != '\\') {
                o += ch;
                continue;
            }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        size_t back = i_;
                        unsigned lo;
                        i_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i_ = back;   // not a pair: the next escape stands alone
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;     // a lone surrogate is not a character
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;   // \" \\ \/
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 32) return false;
        ws();
        if (i_ >= s_.size()) return false;
        char ch = s_[i_];
        if (ch == '{') {
            out.type = Json::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (s_[i_] == '}') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (ch == '[') {
            out.type = Json::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (s_[i_] == ']') {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (ch == '"') {
            out.type = Json::String;
            return string(out.str);
        }
        if (lit("true")) {
            out.type = Json::Bool;
            out.b = true;
            return true;
        }
        if (lit("false")) {
            out.type = Json::Bool;
            return true;
        }
        if (lit("null")) return true;
        char* end = nullptr;
        out.num = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        out.type = Json::Number;
        i_ = size_t(end - s_.c_str());
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

int ticks_to_s(const Json& v) {
    // RunTimeTicks and positions: 10 000 000 to the second.
    return v.type == Json::Number ? int(v.num / 1e7 + 0.5) : -1;
}

int int_of(const Json& v) { return v.type == Json::Number ? int(v.num) : -1; }

// An id from a reply, or "" if it isn't one.
std::string id_of(const Json& v) { return safe_id(v.text()) ? v.text() : std::string(); }
// An image tag: hex in practice. Anything else isn't sent back.
std::string tag_of(const std::string& t) { return safe_token(t) ? t : std::string(); }
std::string tag_of(const Json& v) { return tag_of(v.text()); }

std::string artist_of(const Json& it) {
    if (!it["AlbumArtist"].text().empty()) return it["AlbumArtist"].text();
    return it["Artists"].first();
}

bool read_item(const Json& it, Item& x) {
    const std::string& type = it["Type"].text();
    if (type == "MusicAlbum") x.kind = Kind::Album;
    else if (type == "MusicArtist") x.kind = Kind::Artist;
    else if (type == "Playlist") x.kind = Kind::Playlist;
    else if (type == "Audio" || type == "AudioBook") x.kind = Kind::Track;
    else if (type == "Movie") x.kind = Kind::Movie;
    else if (type == "Series") x.kind = Kind::Series;
    else if (type == "Season") x.kind = Kind::Season;
    else if (type == "Episode") x.kind = Kind::Episode;
    else if (type == "Video" || type == "MusicVideo") x.kind = Kind::Video;
    else if (type == "CollectionFolder" || type == "UserView" || type == "Folder" || type == "BoxSet" ||
             type == "PhotoAlbum" || type == "PlaylistsFolder" || type == "ManualPlaylistsFolder")
        x.kind = Kind::Folder;
    else return false;
    x.id = it["Id"].text();
    x.name = it["Name"].text();
    if (!safe_id(x.id) || x.name.empty()) return false;

    if (x.kind == Kind::Album || x.kind == Kind::Track) x.detail = artist_of(it);
    if (x.kind == Kind::Track) {
        x.album = it["Album"].text();
        x.album_id = id_of(it["AlbumId"]);
    }
    if (x.kind == Kind::Folder) {
        // The library type: "music", "movies", "tvshows", "homevideos"...
        // Live TV can't be played here, so it isn't listed.
        for (char ch : it["CollectionType"].text()) x.detail += char(std::tolower((unsigned char)ch));
        if (x.detail == "livetv") return false;
        if (type == "BoxSet") x.detail = "boxsets";
        if (type == "PlaylistsFolder" || type == "ManualPlaylistsFolder") x.detail = "playlists";
        if (x.detail.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos) x.detail.clear();
    }
    if (x.kind == Kind::Season || x.kind == Kind::Episode) {
        x.detail = it["SeriesName"].text();
        x.series_id = id_of(it["SeriesId"]);
    }
    x.duration_s = ticks_to_s(it["RunTimeTicks"]);
    x.number = int_of(it["IndexNumber"]);
    x.season = int_of(it["ParentIndexNumber"]);
    x.year = int_of(it["ProductionYear"]);
    x.children = int_of(it["ChildCount"]);
    if (it["CommunityRating"].type == Json::Number) x.stars10 = int(it["CommunityRating"].num * 10 + 0.5);
    x.rated = it["OfficialRating"].text();
    if (x.rated.size() > 12) x.rated.clear();
    x.overview = it["Overview"].text();

    const Json& ud = it["UserData"];
    int resume = ticks_to_s(ud["PlaybackPositionTicks"]);
    if (resume > 0) x.resume_s = resume;
    x.played = ud["Played"].b;
    x.favorite = ud["IsFavorite"].b;
    x.unplayed = std::max(0, int_of(ud["UnplayedItemCount"]));

    // A video's tracks: those of its first version (MediaSources), which the
    // indexes sent back refer to; else the item's own list.
    if (x.playable_video()) {
        const Json& sources = it["MediaSources"];
        const Json* streams = &it["MediaStreams"];
        int default_audio_index = -1;
        if (sources.type == Json::Array && !sources.items.empty()) {
            if (sources.items[0]["MediaStreams"].type == Json::Array) streams = &sources.items[0]["MediaStreams"];
            default_audio_index = int_of(sources.items[0]["DefaultAudioStreamIndex"]);
        }
        if (streams->type == Json::Array) {
            x.tracks_known = true;
            for (const Json& st : streams->items) {
                const std::string& type = st["Type"].text();
                if (type != "Audio" && type != "Subtitle") continue;
                Track t;
                t.index = int_of(st["Index"]);
                if (t.index < 0 || t.index > 999) continue;
                t.subtitle = type == "Subtitle";
                for (char ch : st["Language"].text())
                    if (std::isalpha((unsigned char)ch) && t.lang.size() < 8) t.lang += char(std::tolower((unsigned char)ch));
                t.title = st["DisplayTitle"].text();
                if (t.title.empty()) t.title = st["Title"].text();
                if (t.title.size() > 80) t.title.resize(80);
                t.is_default = st["IsDefault"].b || (!t.subtitle && t.index == default_audio_index);
                t.forced = st["IsForced"].b;
                x.tracks.push_back(std::move(t));
            }
        }
    }

    // The pictures. Jellyfin names each by the item that holds it, which
    // for a track is its album and for an episode may be its show.
    const Json& tags = it["ImageTags"];
    const Pic own{x.id, "Primary", tag_of(tags["Primary"])};
    const bool has_own = !tags["Primary"].text().empty();
    const Pic album{x.album_id, "Primary", tag_of(it["AlbumPrimaryImageTag"])};
    const bool has_album = !album.id.empty() && !it["AlbumPrimaryImageTag"].text().empty();
    const Pic series{x.series_id, "Primary", tag_of(it["SeriesPrimaryImageTag"])};
    const bool has_series = !series.id.empty() && !it["SeriesPrimaryImageTag"].text().empty();
    // A track shows its album's cover: one picture for the whole album.
    if (x.kind == Kind::Track && has_album) x.poster = album;
    else if (has_own) x.poster = own;
    else if (has_album) x.poster = album;
    else if (has_series) x.poster = series;

    const Pic thumb{x.id, "Thumb", tag_of(tags["Thumb"])};
    const Pic backdrop{x.id, "Backdrop", tag_of(it["BackdropImageTags"].first())};
    const Pic parent_thumb{id_of(it["ParentThumbItemId"]), "Thumb", tag_of(it["ParentThumbImageTag"])};
    const Pic parent_backdrop{id_of(it["ParentBackdropItemId"]), "Backdrop",
                              tag_of(it["ParentBackdropImageTags"].first())};
    const bool has_thumb = !tags["Thumb"].text().empty();
    const bool has_backdrop = !it["BackdropImageTags"].first().empty();
    // An episode's or a home video's own picture is a 16:9 still already.
    if ((x.kind == Kind::Episode || x.kind == Kind::Video || x.kind == Kind::Folder) && has_own) x.wide = own;
    else if (has_thumb) x.wide = thumb;
    else if (has_backdrop) x.wide = backdrop;
    else if (parent_thumb.any()) x.wide = parent_thumb;
    else if (parent_backdrop.any()) x.wide = parent_backdrop;
    else x.wide = x.poster;
    return true;
}

const char kPics[] = "&enableImageTypes=Primary,Backdrop,Thumb&imageTypeLimit=1";

std::string sort_query(Query::Sort s) {
    switch (s) {
        case Query::ByAdded: return "&sortBy=DateCreated,SortName&sortOrder=Descending,Ascending";
        case Query::ByYear: return "&sortBy=ProductionYear,SortName&sortOrder=Descending,Ascending";
        case Query::ByRandom: return "&sortBy=Random";
        case Query::ByName: break;
    }
    return "&sortBy=SortName&sortOrder=Ascending";
}

std::string page_query(int start) {
    return "&startIndex=" + std::to_string(start < 0 ? 0 : start) + "&limit=" + std::to_string(kPageSize);
}

}  // namespace

std::string Item::episode_code() const {
    if (kind != Kind::Episode || number < 0) return "";
    return (season >= 0 ? "S" + std::to_string(season) : std::string()) + "E" + std::to_string(number);
}

bool paged(Query::What what) {
    switch (what) {
        case Query::Library: case Query::Albums: case Query::Artists: case Query::Playlists:
        case Query::Movies: case Query::Shows: case Query::Songs: case Query::Favorites:
            return true;
        default: return false;
    }
}

bool sortable(Query::What what) {
    return what == Query::Library || what == Query::Albums || what == Query::Movies || what == Query::Shows ||
           what == Query::Songs;
}

const char* sort_name(Query::Sort s) {
    switch (s) {
        case Query::ByAdded: return "Newest first";
        case Query::ByYear: return "By year";
        case Query::ByRandom: return "Shuffled";
        case Query::ByName: break;
    }
    return "By name";
}

bool safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char ch : id)
        if (!std::isxdigit(ch) && ch != '-') return false;
    return true;
}

bool safe_token(const std::string& t) {
    if (t.empty() || t.size() > 256) return false;
    for (unsigned char ch : t)
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') return false;
    return true;
}

std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            o += char(ch);
        } else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", ch);
            o += b;
        }
    }
    return o;
}

std::string list_path(const Query& q, const std::string& user_id, int start) {
    const std::string user = "userId=" + url_encode(user_id);
    const std::string items = "/Items?" + user + "&enableUserData=true" + kPics;
    const std::string sort = sort_query(q.sort);
    const std::string page = page_query(start);
    switch (q.what) {
        case Query::Views: return "/UserViews?" + user;
        case Query::Resume:
            return "/UserItems/Resume?" + user + "&mediaTypes=Video&limit=12&enableUserData=true" + kPics +
                   "&enableTotalRecordCount=false";
        case Query::NextUp:
            return "/Shows/NextUp?" + user + "&limit=12&enableUserData=true&enableResumable=false" + kPics +
                   "&enableTotalRecordCount=false";
        case Query::Latest:
            if (!safe_id(q.a)) return "";
            return "/Items/Latest?" + user + "&parentId=" + q.a + "&limit=12&enableUserData=true" + kPics;
        case Query::LatestMusic:
            return items + "&includeItemTypes=MusicAlbum&recursive=true&sortBy=DateCreated&sortOrder=Descending"
                           "&limit=12&enableTotalRecordCount=false";
        case Query::Library: {
            if (!safe_id(q.a)) return "";
            const std::string in = items + "&parentId=" + q.a;
            if (q.b == "music") return in + "&includeItemTypes=MusicAlbum&recursive=true" + sort + page;
            if (q.b == "movies") return in + "&includeItemTypes=Movie&recursive=true" + sort + page;
            if (q.b == "tvshows") return in + "&includeItemTypes=Series&recursive=true" + sort + page;
            // Home videos, music videos, books, mixed, collections, a plain
            // folder: what is in it, folders first.
            std::string by = sort.substr(sort.find('=') + 1);   // "SortName&sortOrder=Ascending"
            std::string order = "Ascending";
            auto o = by.find("&sortOrder=");
            if (o != std::string::npos) {
                order = by.substr(o + 11);
                by.erase(o);
            }
            return in + "&sortBy=IsFolder," + by + "&sortOrder=Descending," + order + page;
        }
        case Query::Albums: return items + "&includeItemTypes=MusicAlbum&recursive=true" + sort + page;
        case Query::Artists:
            return "/Artists/AlbumArtists?" + user + "&enableUserData=true" + kPics +
                   "&sortBy=SortName&sortOrder=Ascending" + page;
        case Query::Playlists:
            return items + "&includeItemTypes=Playlist&recursive=true&sortBy=SortName&sortOrder=Ascending" + page;
        case Query::Movies: return items + "&includeItemTypes=Movie&recursive=true" + sort + page;
        case Query::Shows: return items + "&includeItemTypes=Series&recursive=true" + sort + page;
        case Query::Songs: return items + "&includeItemTypes=Audio&recursive=true" + sort + page;
        case Query::Favorites:
            return items + "&isFavorite=true&recursive=true&includeItemTypes=Movie,Series,Episode,Video,MusicVideo,"
                           "MusicAlbum,Audio,Playlist,BoxSet&sortBy=SortName&sortOrder=Ascending" + page;
        case Query::ArtistAlbums:
            if (!safe_id(q.a)) return "";
            return items + "&includeItemTypes=MusicAlbum&recursive=true&albumArtistIds=" + q.a +
                   "&sortBy=ProductionYear,SortName&sortOrder=Descending,Ascending&limit=300";
        case Query::AlbumTracks:
            if (!safe_id(q.a)) return "";
            return items + "&parentId=" + q.a +
                   "&includeItemTypes=Audio&recursive=true&sortBy=ParentIndexNumber,IndexNumber,SortName&limit=500";
        case Query::PlaylistTracks:
            if (!safe_id(q.a)) return "";
            return "/Playlists/" + q.a + "/Items?" + user + "&limit=500&enableUserData=true" + kPics;
        case Query::Seasons:
            if (!safe_id(q.a)) return "";
            return "/Shows/" + q.a + "/Seasons?" + user + "&enableUserData=true" + kPics;
        case Query::Episodes:
            if (!safe_id(q.a) || !safe_id(q.b)) return "";
            return "/Shows/" + q.a + "/Episodes?" + user + "&seasonId=" + q.b + "&limit=500&enableUserData=true" + kPics;
        case Query::Suggestions:
            return items + "&recursive=true&includeItemTypes=Movie,Series,MusicAlbum&sortBy=IsFavoriteOrLiked,Random"
                           "&limit=12&enableTotalRecordCount=false";
    }
    return "";
}

std::string legacy_path(const Query& q, const std::string& user_id) {
    if (!safe_id(user_id)) return "";
    const std::string base = "/Users/" + user_id;
    switch (q.what) {
        case Query::Views: return base + "/Views";
        case Query::Resume: return base + "/Items/Resume?mediaTypes=Video&limit=12" + kPics;
        case Query::Latest:
            if (!safe_id(q.a)) return "";
            return base + "/Items/Latest?parentId=" + q.a + "&limit=12" + kPics;
        default: return "";
    }
}

std::string item_path(const std::string& id, const std::string& user_id) {
    if (!safe_id(id)) return "";
    return "/Items/" + id + "?userId=" + url_encode(user_id);
}

std::string clean_search(const std::string& typed) {
    std::string o;
    for (unsigned char ch : typed) {
        if (ch < 0x20 || ch == 0x7f) ch = ' ';
        if (ch == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += char(ch);
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

std::vector<SearchGroup> search_paths(const std::string& text, const std::string& user_id, int limit) {
    const std::string term = clean_search(text);
    if (term.empty()) return {};
    const std::string user = "userId=" + url_encode(user_id);
    const std::string tail = "&searchTerm=" + url_encode(term) + "&limit=" + std::to_string(limit) +
                             "&enableUserData=true&enableTotalRecordCount=false" + kPics;
    auto items = [&](const char* types) {
        return "/Items?" + user + "&recursive=true&includeItemTypes=" + types + tail;
    };
    // The order Jellyfin's own search page lists them in.
    return {
        {"Movies", items("Movie")},
        {"Shows", items("Series")},
        {"Episodes", items("Episode")},
        {"Videos", items("Video,MusicVideo")},
        {"Collections", items("BoxSet")},
        {"Playlists", items("Playlist")},
        // Artists have a list of their own; /Items leaves them out on some servers.
        {"Artists", "/Artists?" + user + tail},
        {"Albums", items("MusicAlbum")},
        {"Songs", items("Audio")},
        {"Audiobooks", items("AudioBook")},
    };
}

std::string track_query(int audio, int sub) {
    std::string q;
    if (audio >= 0) q += "&AudioStreamIndex=" + std::to_string(audio);
    if (sub >= 0) q += "&SubtitleStreamIndex=" + std::to_string(sub) + "&SubtitleMethod=Encode";
    return q;
}

std::string language_name(const std::string& code) {
    static const char* const kNames[][2] = {
        {"eng", "English"},   {"en", "English"},     {"jpn", "Japanese"},   {"ja", "Japanese"},
        {"spa", "Spanish"},   {"es", "Spanish"},     {"fre", "French"},     {"fra", "French"},
        {"fr", "French"},     {"ger", "German"},     {"deu", "German"},     {"de", "German"},
        {"ita", "Italian"},   {"it", "Italian"},     {"por", "Portuguese"}, {"pt", "Portuguese"},
        {"rus", "Russian"},   {"ru", "Russian"},     {"chi", "Chinese"},    {"zho", "Chinese"},
        {"zh", "Chinese"},    {"kor", "Korean"},     {"ko", "Korean"},      {"dut", "Dutch"},
        {"nld", "Dutch"},     {"nl", "Dutch"},       {"swe", "Swedish"},    {"sv", "Swedish"},
        {"nor", "Norwegian"}, {"nob", "Norwegian"},  {"dan", "Danish"},     {"fin", "Finnish"},
        {"pol", "Polish"},    {"pl", "Polish"},      {"tur", "Turkish"},    {"ara", "Arabic"},
        {"hin", "Hindi"},     {"heb", "Hebrew"},     {"gre", "Greek"},      {"ell", "Greek"},
        {"cze", "Czech"},     {"ces", "Czech"},      {"hun", "Hungarian"},  {"tha", "Thai"},
        {"vie", "Vietnamese"}, {"ind", "Indonesian"}, {"ukr", "Ukrainian"}, {"und", "Unknown language"},
    };
    for (const auto& n : kNames)
        if (code == n[0]) return n[1];
    return code;
}

std::string track_label(const Track& t) {
    std::string s = t.title;
    if (s.empty()) s = t.lang.empty() ? "Track " + std::to_string(t.index) : language_name(t.lang);
    if (t.forced && s.find("orced") == std::string::npos) s += " (forced)";
    return s;
}

int track_for_language(const std::vector<Track>& tracks, bool subtitle, const std::string& lang) {
    if (lang.empty()) return -1;
    int best = -1, best_score = -1;
    for (const Track& t : tracks) {
        if (t.subtitle != subtitle || t.lang != lang) continue;
        int score = (t.is_default ? 2 : 0) + (subtitle && !t.forced ? 4 : 0);
        if (score > best_score) {
            best = t.index;
            best_score = score;
        }
    }
    return best;
}

int default_audio(const std::vector<Track>& tracks) {
    int first = -1;
    for (const Track& t : tracks) {
        if (t.subtitle) continue;
        if (t.is_default) return t.index;
        if (first < 0) first = t.index;
    }
    return first;
}

int next_track(const std::vector<Track>& tracks, bool subtitle, int current) {
    std::vector<int> choices;
    if (subtitle) choices.push_back(kSubOff);
    for (const Track& t : tracks)
        if (t.subtitle == subtitle) choices.push_back(t.index);
    if (choices.empty()) return current;
    if (!subtitle && current < 0) current = default_audio(tracks);
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == current) return choices[(i + 1) % choices.size()];
    return choices[subtitle && choices.size() > 1 ? 1 : 0];
}

std::string image_path(const Pic& pic, int w, int h) {
    if (!safe_id(pic.id) || w <= 0 || h <= 0 || w > 2000 || h > 2000) return "";
    if (pic.type != "Primary" && pic.type != "Thumb" && pic.type != "Backdrop") return "";
    std::string p = "/Items/" + pic.id + "/Images/" + pic.type + "?fillWidth=" + std::to_string(w) +
                    "&fillHeight=" + std::to_string(h) + "&quality=90&format=Jpg";
    if (safe_token(pic.tag)) p += "&tag=" + pic.tag;
    return p;
}

std::string favorite_path(const std::string& id, const std::string& user_id) {
    if (!safe_id(id)) return "";
    return "/UserFavoriteItems/" + id + "?userId=" + url_encode(user_id);
}

std::string played_path(const std::string& id, const std::string& user_id) {
    if (!safe_id(id)) return "";
    return "/UserPlayedItems/" + id + "?userId=" + url_encode(user_id);
}

bool parse_page(const std::string& json, Page& out) {
    Json j;
    if (!Parser(json).parse(j)) return false;
    const Json& arr = j.type == Json::Array ? j : j["Items"];
    if (arr.type != Json::Array) return false;
    out.items.clear();
    out.count = int(arr.items.size());
    for (const Json& it : arr.items) {
        Item x;
        if (read_item(it, x)) out.items.push_back(std::move(x));
    }
    out.total = j.type == Json::Object ? int_of(j["TotalRecordCount"]) : -1;
    // With the count turned off, some servers say 0.
    if (out.total < out.count) out.total = -1;
    return true;
}

bool parse_item(const std::string& json, Item& out) {
    Json j;
    if (!Parser(json).parse(j) || j.type != Json::Object) return false;
    Item x;
    if (!read_item(j, x)) return false;
    out = std::move(x);
    return true;
}

std::string status_error(int status) {
    if (status == 0) return "Can't reach the server";
    if (status == 401) return "Signed out by the server; sign in again";
    if (status == 403) return "This user may not do that";
    if (status == 404) return "The server doesn't have that";
    if (status == 503) return "The server is starting up; try again";
    return "Server error " + std::to_string(status);
}

// ---- The network ------------------------------------------------------------

#ifdef __EMSCRIPTEN__

// The browser simulator has no processes and no server.
std::vector<Reply> http_get_all(const std::vector<std::string>& urls, const std::string&, const std::string&,
                                std::atomic<int>*, int) {
    return std::vector<Reply>(urls.size());
}
Reply http_send(const std::string&, const std::string&, const std::string&, const std::string&, std::atomic<int>*) {
    return Reply{};
}

#else

namespace {

constexpr off_t kMaxBody = 8 * 1024 * 1024;

// A name nothing else in this process is using.
std::string scratch_name(const std::string& dir, const char* what) {
    static std::atomic<unsigned> n{0};
    return dir + "/" + what + "-" + std::to_string(getpid()) + "-" + std::to_string(n.fetch_add(1));
}

// The request's headers in a file only you can read: the token never goes on
// a command line, where any program could see it. "" if it can't be made.
std::string header_file(const std::string& dir, const std::string& auth) {
    if (auth.find_first_of("\r\n") != std::string::npos) return "";
    std::string path = scratch_name(dir, "h");
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return "";
    std::string text = "Accept: application/json, image/*\n";
    if (!auth.empty()) text += "Authorization: " + auth + "\n";
    bool ok = write(fd, text.data(), text.size()) == ssize_t(text.size());
    close(fd);
    if (!ok) {
        unlink(path.c_str());
        return "";
    }
    return path;
}

bool read_small_file(const std::string& path, std::string& out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size > kMaxBody) {
        close(fd);
        return false;
    }
    out.clear();
    out.reserve(size_t(st.st_size));
    char buf[16384];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
        if (n > 0) out.append(buf, size_t(n));
    close(fd);
    return true;
}

// Runs curl with argv and returns what it printed. pid: its process id
// while it runs.
std::string run_curl(std::vector<std::string>& args, std::atomic<int>* pid_out) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) return "";
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "curl", &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out[1]);
    std::string printed;
    if (rc == 0) {
        if (pid_out) pid_out->store(pid);
        char buf[1024];
        ssize_t n;
        while ((n = read(out[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
            if (n > 0 && printed.size() < 65536) printed.append(buf, size_t(n));
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (pid_out) pid_out->store(0);
    }
    close(out[0]);
    return printed;
}

// What every request shares. Redirects are not followed: curl would send
// the token (a header of our own) wherever the server points.
std::vector<std::string> curl_base(const std::string& headers, int timeout_s) {
    return {"curl", "-sS", "-g", "--proto", "=http,https", "--compressed", "--connect-timeout", "5",
            "--max-time", std::to_string(timeout_s), "--max-filesize", std::to_string(kMaxBody),
            "-H", "@" + headers};
}

}  // namespace

std::vector<Reply> http_get_all(const std::vector<std::string>& urls, const std::string& auth,
                                const std::string& dir, std::atomic<int>* pid, int timeout_s) {
    std::vector<Reply> replies(urls.size());
    if (urls.empty()) return replies;
    std::string headers = header_file(dir, auth);
    if (headers.empty()) return replies;
    std::vector<std::string> args = curl_base(headers, timeout_s);
    // Each reply's number and status on a line of its own: with several
    // going at once they finish in any order.
    args.insert(args.end(), {"--parallel", "--parallel-max", "4", "-w", "%{urlnum} %{http_code}\\n"});
    std::vector<std::string> files;
    for (const std::string& url : urls) {
        files.push_back(scratch_name(dir, "r"));
        // Only ever an http(s) address: nothing curl could take for an option or a file.
        if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
            for (auto& f : files) unlink(f.c_str());
            unlink(headers.c_str());
            return replies;
        }
        args.insert(args.end(), {"-o", files.back(), "--url", url});
    }
    std::string printed = run_curl(args, pid);
    unlink(headers.c_str());
    size_t at = 0;
    while (at < printed.size()) {
        size_t nl = printed.find('\n', at);
        std::string line = printed.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? printed.size() : nl + 1;
        char* end = nullptr;
        long n = std::strtol(line.c_str(), &end, 10);
        if (end == line.c_str() || *end != ' ') continue;
        long code = std::strtol(end + 1, nullptr, 10);
        if (n >= 0 && size_t(n) < replies.size()) replies[size_t(n)].status = int(code);
    }
    for (size_t i = 0; i < files.size(); ++i) {
        if (replies[i].status >= 200 && replies[i].status < 300 && !read_small_file(files[i], replies[i].body))
            replies[i].status = 0;
        unlink(files[i].c_str());
    }
    return replies;
}

Reply http_send(const std::string& method, const std::string& url, const std::string& auth,
                const std::string& dir, std::atomic<int>* pid) {
    Reply r;
    if (method != "POST" && method != "DELETE") return r;
    std::string headers = header_file(dir, auth);
    if (headers.empty()) return r;
    std::vector<std::string> args = curl_base(headers, 12);
    args.insert(args.end(), {"-X", method, "-o", "/dev/null", "-w", "%{http_code}"});
    if (method == "POST") args.insert(args.end(), {"--data-binary", ""});
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
        unlink(headers.c_str());
        return r;
    }
    args.insert(args.end(), {"--url", url});
    std::string printed = run_curl(args, pid);
    unlink(headers.c_str());
    r.status = std::atoi(printed.c_str());
    return r;
}

#endif

}  // namespace zs::jfapp
