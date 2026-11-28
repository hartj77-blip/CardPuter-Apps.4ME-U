// The Jellyfin app's own talk with the server: the lists, pictures and
// searches that ZeroOS's small client (hal/jellyfin.h) has no call for.
//
// ZeroOS still signs in, builds the addresses music and video stream from,
// and reports what is playing. Everything here is what makes the app look
// like Jellyfin's own: every item's pictures, what is played and what is a
// favourite, Next Up, Recently Added, a search that asks for each kind of
// thing on its own, and requests that run off the screen's thread, several
// at a time over one curl.
//
// No LVGL and no hardware layer in here: tests/api_test.cpp runs anywhere.
// Paths follow Jellyfin's API description (api.jellyfin.org, 12.4), with the
// per-user paths servers before 10.9 had as a second try.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace zs::jfapp {

enum class Kind { Album, Artist, Playlist, Track, Movie, Series, Season, Episode, Video, Folder };

// One picture on the server: /Items/<id>/Images/<type>.
struct Pic {
    std::string id;     // the item that holds it; empty: there is none
    std::string type;   // "Primary", "Thumb" or "Backdrop"
    std::string tag;    // changes when the picture does; empty if not known
    bool any() const { return !id.empty(); }
};

// One audio or subtitle track of a video, as the server lists it.
struct Track {
    int index = -1;          // the server's stream index (AudioStreamIndex, SubtitleStreamIndex)
    bool subtitle = false;   // else audio
    std::string lang;        // "eng", "jpn"... ("" when the file doesn't say)
    std::string title;       // the server's name for it: "English - AAC - Stereo - Default"
    bool is_default = false;
    bool forced = false;
};

// A video's subtitles: off, or a track's index.
constexpr int kSubOff = -1;

struct Item {
    Kind kind = Kind::Track;
    std::string id;
    std::string name;
    std::string detail;      // artist for albums and tracks; the show for seasons and episodes;
                             // a library's type ("music", "movies", "tvshows", "" ...) for folders
    std::string album;       // tracks
    std::string album_id;
    std::string series_id;   // seasons and episodes
    int duration_s = -1;     // tracks and videos
    int resume_s = 0;        // videos: where you stopped, 0 = from the start
    int number = -1;         // episode or track number (seasons: the season's)
    int season = -1;         // episodes: the season's number
    int year = -1;
    int children = -1;       // albums: tracks; shows and seasons: episodes, when the server says
    int unplayed = 0;        // shows and seasons: episodes not watched yet
    bool played = false;
    bool favorite = false;
    int stars10 = -1;        // community rating times ten (74 = 7.4)
    std::string rated;       // "PG-13"
    std::string overview;    // only when asked for (item_path)
    Pic poster;              // the item's own shape: a poster, a cover, a still
    Pic wide;                // a 16:9 picture for Continue Watching and episode rows
    // Videos: their audio and subtitle tracks (only when asked for one item:
    // item_path), and which to play.
    std::vector<Track> tracks;
    bool tracks_known = false;
    int audio = -1;          // a track's index; -1: the server's choice
    int sub = kSubOff;       // a track's index, or kSubOff
    bool tracks_chosen = false;   // by a person, or from what they chose before

    bool playable_video() const { return kind == Kind::Movie || kind == Kind::Episode || kind == Kind::Video; }
    bool playable() const { return playable_video() || kind == Kind::Track; }
    // "S1E3" for an episode, empty otherwise.
    std::string episode_code() const;
};

// A list as it came back.
struct Page {
    std::vector<Item> items;
    int count = 0;    // rows in the reply, with the kinds the app leaves out
    int total = -1;   // how many the whole list has, when the server says
};

// What to list. a and b depend on what.
struct Query {
    enum What {
        Views,          // the user's libraries (My Media)
        Resume,         // Continue Watching
        NextUp,
        Latest,         // a: library id (Recently Added there)
        LatestMusic,    // the newest albums of every music library
        Library,        // a: id, b: its type; a folder inside one too
        Albums, Artists, Playlists, Movies, Shows, Songs,   // across every library
        Favorites,
        ArtistAlbums,   // a: artist id
        AlbumTracks,    // a: album id
        PlaylistTracks, // a: playlist id
        Seasons,        // a: series id
        Episodes,       // a: series id, b: season id
        Suggestions,    // something to watch or hear, for the empty search page
    } what = Views;
    std::string a, b;
    // The order of the long lists. Only those take it (sortable()).
    enum Sort { ByName, ByAdded, ByYear, ByRandom } sort = ByName;
};

constexpr int kPageSize = 100;   // rows asked for at a time in the long lists
// The long lists come a page at a time (start = the row to begin at).
bool paged(Query::What what);
bool sortable(Query::What what);
const char* sort_name(Query::Sort s);

// Jellyfin ids are hex, with or without dashes. Anything else never goes
// into an address.
bool safe_id(const std::string& id);
// A token of letters, digits, '-', '_' and '.': anything else could end the
// header line it is sent on.
bool safe_token(const std::string& t);
std::string url_encode(const std::string& s);

// The path and query (after the server's address) for a list; "" if the
// query makes no sense (a bad id). user_id is the signed-in user's.
std::string list_path(const Query& q, const std::string& user_id, int start = 0);
// Where servers before 10.9 keep the same list; "" if nowhere else.
std::string legacy_path(const Query& q, const std::string& user_id);

// One item with its overview.
std::string item_path(const std::string& id, const std::string& user_id);

// Search asks once per kind of thing, as Jellyfin's own apps do, so 300
// songs with "love" in the name can't crowd the one film out.
struct SearchGroup {
    const char* title;   // "Movies", "Shows", "Songs"...
    std::string path;
};
std::vector<SearchGroup> search_paths(const std::string& text, const std::string& user_id, int limit = 12);
// What a person typed, made ready to send: spaces trimmed and runs of them
// made one. "" if nothing is left to search for.
std::string clean_search(const std::string& typed);

// What to add to a video's address for its tracks: AudioStreamIndex, and
// SubtitleStreamIndex with SubtitleMethod=Encode, so the server burns the
// subtitles into the picture (any kind of subtitle, on any screen).
std::string track_query(int audio, int sub);
// A track's name for a person: the server's title, else its language.
std::string track_label(const Track& t);
// "eng" -> "English" (the common ones); the code itself otherwise.
std::string language_name(const std::string& code);
// The track of that kind in that language: the default one first, and for
// subtitles not the "forced" ones (signs only) if there is another. -1: none.
int track_for_language(const std::vector<Track>& tracks, bool subtitle, const std::string& lang);
// The audio track the server plays when none is asked for: the default one,
// else the first. -1 when there is none.
int default_audio(const std::vector<Track>& tracks);
// The next choice after `current` (Left/Right or a key that steps through
// them): audio wraps round its tracks; subtitles go Off, each track, Off.
int next_track(const std::vector<Track>& tracks, bool subtitle, int current);

// A picture, scaled by the server to cover w x h (it keeps the shape, so
// the app crops what hangs over) and sent as JPEG.
std::string image_path(const Pic& pic, int w, int h);

// Mark as favourite / played: POST to set, DELETE to clear.
std::string favorite_path(const std::string& id, const std::string& user_id);
std::string played_path(const std::string& id, const std::string& user_id);

// Reads a list reply: {"Items":[...],"TotalRecordCount":n} or a bare array.
// Kinds the app can't show are left out. False if it isn't such a reply.
bool parse_page(const std::string& json, Page& out);
// Reads one item (the reply to item_path).
bool parse_item(const std::string& json, Item& out);

// ---- The network (curl) ---------------------------------------------------

struct Reply {
    int status = 0;     // HTTP status; 0: no answer
    std::string body;
};

// A sentence a person can act on for a reply that isn't 200.
std::string status_error(int status);

// GETs every address with one curl, up to four at a time over shared
// connections, and returns the replies in the same order. auth is the
// Authorization header's value: it travels in a file only you can read,
// never on the command line, and redirects aren't followed, so it only goes
// to the server itself. dir: a private folder for that file and the replies
// while they arrive. pid: curl's process id while it runs (to stop it).
// Blocks: call it off the screen's thread.
std::vector<Reply> http_get_all(const std::vector<std::string>& urls, const std::string& auth,
                                const std::string& dir, std::atomic<int>* pid = nullptr, int timeout_s = 12);
// One request with a method ("POST", "DELETE") and no body.
Reply http_send(const std::string& method, const std::string& url, const std::string& auth,
                const std::string& dir, std::atomic<int>* pid = nullptr);

}  // namespace zs::jfapp
