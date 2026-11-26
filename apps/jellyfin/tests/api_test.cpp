// The Jellyfin app's requests and the replies it reads (api.h): no network,
// no screen. cmake ... && ctest, or run jellyfin_api_test.
#include <cstdio>
#include <string>

#include "apps/jellyfin/api.h"

using namespace zs::jfapp;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static const char kUser[] = "aaaaaaaabbbbccccddddeeeeeeeeeeee";
static const char kId[] = "0123456789abcdef0123456789abcdef";
static const char kId2[] = "fedcba98-7654-3210-fedc-ba9876543210";

// A list as Jellyfin 12.4 sends it (shortened), one of each kind.
static const char kList[] = R"({"Items":[
 {"Name":"Sintel","Id":"11111111111111111111111111111111","Type":"Movie","ProductionYear":2010,
  "RunTimeTicks":8880000000,"CommunityRating":7.44,"OfficialRating":"PG",
  "ImageTags":{"Primary":"aaa111","Thumb":"ccc333"},"BackdropImageTags":["bbb222"],
  "UserData":{"PlaybackPositionTicks":3000000000,"Played":false,"IsFavorite":true,"PlayCount":1}},
 {"Name":"Batteries","Id":"22222222222222222222222222222222","Type":"Episode","SeriesName":"Bench Talk",
  "SeriesId":"33333333333333333333333333333333","SeasonId":"44444444444444444444444444444444",
  "IndexNumber":3,"ParentIndexNumber":1,"RunTimeTicks":15000000000,"ImageTags":{},
  "SeriesPrimaryImageTag":"ser1","ParentBackdropItemId":"33333333333333333333333333333333",
  "ParentBackdropImageTags":["back9"],"UserData":{"Played":true,"PlaybackPositionTicks":0}},
 {"Name":"Low Battery Blues","Id":"55555555555555555555555555555555","Type":"Audio","Album":"Vol. 2",
  "AlbumId":"66666666666666666666666666666666","AlbumPrimaryImageTag":"alb7","AlbumArtist":"Bench Tapes",
  "Artists":["Someone Else"],"IndexNumber":1,"RunTimeTicks":2430000000,"ImageTags":{"Primary":"own8"}},
 {"Name":"Vol. 2","Id":"66666666666666666666666666666666","Type":"MusicAlbum","Artists":["Bench Tapes"],
  "ProductionYear":2023,"ChildCount":5,"ImageTags":{"Primary":"alb7"}},
 {"Name":"Bench Talk","Id":"33333333333333333333333333333333","Type":"Series",
  "UserData":{"UnplayedItemCount":5,"Played":false},"ImageTags":{"Primary":"ser1"}},
 {"Name":"Movies","Id":"77777777777777777777777777777777","Type":"CollectionFolder","CollectionType":"movies",
  "ImageTags":{"Primary":"lib1"}},
 {"Name":"Live TV","Id":"88888888888888888888888888888888","Type":"UserView","CollectionType":"livetv"},
 {"Name":"A snap","Id":"99999999999999999999999999999999","Type":"Photo"},
 {"Name":"Evil","Id":"../../etc/passwd","Type":"Movie"},
 {"Name":"","Id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","Type":"Movie"},
 {"Name":"No picture","Id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","Type":"Movie","ImageTags":{"Primary":"x\"y z"}}
 ],"TotalRecordCount":230,"StartIndex":0})";

static void test_parse() {
    Page p;
    CHECK(parse_page(kList, p));
    CHECK(p.count == 11);       // rows in the reply
    CHECK(p.total == 230);
    // Live TV, the photo, the bad id and the nameless one are left out.
    CHECK(p.items.size() == 7);
    if (p.items.size() != 7) return;

    const Item& film = p.items[0];
    CHECK(film.kind == Kind::Movie && film.name == "Sintel" && film.year == 2010);
    CHECK(film.duration_s == 888 && film.resume_s == 300);
    CHECK(film.favorite && !film.played);
    CHECK(film.stars10 == 74 && film.rated == "PG");
    CHECK(film.poster.id == film.id && film.poster.type == "Primary" && film.poster.tag == "aaa111");
    // A film's 16:9 picture: its thumb before its backdrop.
    CHECK(film.wide.type == "Thumb" && film.wide.tag == "ccc333");
    CHECK(film.playable_video() && film.playable());

    const Item& ep = p.items[1];
    CHECK(ep.kind == Kind::Episode && ep.detail == "Bench Talk" && ep.episode_code() == "S1E3");
    CHECK(ep.series_id == "33333333333333333333333333333333");
    CHECK(ep.played && ep.resume_s == 0);
    // No picture of its own: the show's poster, and the show's backdrop for 16:9.
    CHECK(ep.poster.id == ep.series_id && ep.poster.tag == "ser1");
    CHECK(ep.wide.id == ep.series_id && ep.wide.type == "Backdrop" && ep.wide.tag == "back9");

    const Item& track = p.items[2];
    CHECK(track.kind == Kind::Track && track.detail == "Bench Tapes" && track.album == "Vol. 2");
    CHECK(track.duration_s == 243 && track.number == 1);
    // A track shows its album's cover, even with a picture of its own.
    CHECK(track.poster.id == "66666666666666666666666666666666" && track.poster.tag == "alb7");
    CHECK(!track.playable_video() && track.playable());

    CHECK(p.items[3].kind == Kind::Album && p.items[3].detail == "Bench Tapes" && p.items[3].children == 5);
    CHECK(p.items[4].kind == Kind::Series && p.items[4].unplayed == 5);
    CHECK(p.items[5].kind == Kind::Folder && p.items[5].detail == "movies");
    CHECK(p.items[5].wide.id == p.items[5].id);   // a library's picture is 16:9
    // A tag that isn't plain characters is never sent back.
    CHECK(p.items[6].poster.any() && p.items[6].poster.tag.empty());

    // /Items/Latest answers with a bare array.
    Page bare;
    CHECK(parse_page(R"([{"Name":"A","Id":"11111111111111111111111111111111","Type":"MusicAlbum"}])", bare));
    CHECK(bare.items.size() == 1 && bare.total == -1 && bare.count == 1);
    // With the count turned off some servers say 0: that isn't "none".
    Page uncounted;
    CHECK(parse_page(R"({"Items":[{"Name":"A","Id":"11111111111111111111111111111111","Type":"Movie"}],
                         "TotalRecordCount":0})", uncounted));
    CHECK(uncounted.items.size() == 1 && uncounted.total == -1);
    Page empty;
    CHECK(parse_page(R"({"Items":[],"TotalRecordCount":0})", empty) && empty.items.empty());

    // Not a list: an error page, half a reply, nothing.
    Page bad;
    CHECK(!parse_page("<html>502 Bad Gateway</html>", bad));
    CHECK(!parse_page(R"({"Items":[{"Name":"A")", bad));
    CHECK(!parse_page("", bad));
    CHECK(!parse_page(R"({"Message":"no"})", bad));
    // Nested deeper than any reply is: refused, not followed down.
    CHECK(!parse_page(std::string(5000, '[') + std::string(5000, ']'), bad));

    Item one;
    CHECK(parse_item(R"({"Name":"Sintel","Id":"11111111111111111111111111111111","Type":"Movie",
                         "Overview":"A girl and a \"dragon\".\nTwo lines, café, 😀."})", one));
    CHECK(one.overview == "A girl and a \"dragon\".\nTwo lines, caf\xC3\xA9, \xF0\x9F\x98\x80.");
    CHECK(!parse_item(R"({"Name":"X","Id":"11111111111111111111111111111111","Type":"Photo"})", one));
    CHECK(!parse_item("[]", one));
}

static void test_paths() {
    Query q;
    q.what = Query::Views;
    CHECK(list_path(q, kUser) == std::string("/UserViews?userId=") + kUser);
    CHECK(legacy_path(q, kUser) == std::string("/Users/") + kUser + "/Views");

    q.what = Query::Resume;
    CHECK(has(list_path(q, kUser), "/UserItems/Resume?userId=") && has(list_path(q, kUser), "mediaTypes=Video"));
    CHECK(has(legacy_path(q, kUser), "/Items/Resume?"));
    q.what = Query::NextUp;
    CHECK(has(list_path(q, kUser), "/Shows/NextUp?userId="));
    CHECK(legacy_path(q, kUser).empty());

    q.what = Query::Latest;
    q.a = kId;
    CHECK(has(list_path(q, kUser), "/Items/Latest?") && has(list_path(q, kUser), std::string("parentId=") + kId));
    q.a = "not an id";
    CHECK(list_path(q, kUser).empty() && legacy_path(q, kUser).empty());

    // A library lists what its type holds; anything else, what is in the folder.
    q = Query{};
    q.what = Query::Library;
    q.a = kId2;
    q.b = "music";
    CHECK(has(list_path(q, kUser), "includeItemTypes=MusicAlbum&recursive=true"));
    CHECK(has(list_path(q, kUser), std::string("parentId=") + kId2));
    q.b = "movies";
    CHECK(has(list_path(q, kUser), "includeItemTypes=Movie&"));
    q.b = "tvshows";
    CHECK(has(list_path(q, kUser), "includeItemTypes=Series&"));
    q.b = "homevideos";
    CHECK(!has(list_path(q, kUser), "includeItemTypes"));
    CHECK(has(list_path(q, kUser), "sortBy=IsFolder,SortName&sortOrder=Descending,Ascending"));
    q.sort = Query::ByAdded;
    CHECK(has(list_path(q, kUser), "sortBy=IsFolder,DateCreated,SortName&sortOrder=Descending,Descending,Ascending"));
    q.sort = Query::ByRandom;
    CHECK(has(list_path(q, kUser), "sortBy=IsFolder,Random&sortOrder=Descending,Ascending"));
    q.sort = Query::ByAdded;
    q.b = "movies";
    CHECK(has(list_path(q, kUser), "sortBy=DateCreated,SortName&sortOrder=Descending,Ascending"));
    // The long lists come a page at a time.
    CHECK(paged(Query::Library) && has(list_path(q, kUser), "&startIndex=0&limit=100"));
    CHECK(has(list_path(q, kUser, 200), "&startIndex=200&limit=100"));
    CHECK(!paged(Query::AlbumTracks) && !paged(Query::Seasons));
    CHECK(sortable(Query::Movies) && !sortable(Query::Artists));

    q = Query{};
    q.what = Query::Artists;
    CHECK(has(list_path(q, kUser), "/Artists/AlbumArtists?userId="));
    q.what = Query::Episodes;
    q.a = kId;
    q.b = kId2;
    CHECK(has(list_path(q, kUser), std::string("/Shows/") + kId + "/Episodes?") &&
          has(list_path(q, kUser), std::string("seasonId=") + kId2));
    q.b = "x";
    CHECK(list_path(q, kUser).empty());
    q.what = Query::PlaylistTracks;
    CHECK(has(list_path(q, kUser), std::string("/Playlists/") + kId + "/Items?"));
    q.what = Query::Favorites;
    CHECK(has(list_path(q, kUser), "isFavorite=true"));

    // Whoever the user is, the address stays an address.
    q.what = Query::Views;
    CHECK(list_path(q, "a b&c=d/../e") == "/UserViews?userId=a%20b%26c%3Dd%2F..%2Fe");
    CHECK(legacy_path(q, "a b&c").empty());

    CHECK(item_path(kId, kUser) == std::string("/Items/") + kId + "?userId=" + kUser);
    CHECK(item_path("../x", kUser).empty());
    CHECK(favorite_path(kId, kUser) == std::string("/UserFavoriteItems/") + kId + "?userId=" + kUser);
    CHECK(played_path(kId, kUser) == std::string("/UserPlayedItems/") + kId + "?userId=" + kUser);
    CHECK(favorite_path("", kUser).empty() && played_path("x y", kUser).empty());
}

static void test_search() {
    CHECK(clean_search("  bench \t tapes  ") == "bench tapes");
    CHECK(clean_search(" \n ").empty());
    CHECK(search_paths("   ", kUser).empty());

    auto groups = search_paths(" big & buck ", kUser);
    CHECK(groups.size() == 10);
    bool artists = false, songs = false;
    for (const SearchGroup& g : groups) {
        // What was typed is one value, whatever is in it.
        CHECK(has(g.path, "searchTerm=big%20%26%20buck&"));
        CHECK(has(g.path, "limit=12"));
        std::string title = g.title;
        if (title == "Artists") {
            artists = true;
            CHECK(g.path.compare(0, 9, "/Artists?") == 0);
        } else {
            CHECK(g.path.compare(0, 7, "/Items?") == 0 && has(g.path, "recursive=true"));
        }
        if (title == "Songs") {
            songs = true;
            CHECK(has(g.path, "includeItemTypes=Audio&"));
        }
    }
    CHECK(artists && songs);
    CHECK(std::string(groups[0].title) == "Movies");   // as Jellyfin's own search page starts
    CHECK(has(search_paths("x", kUser, 5)[0].path, "limit=5"));
}

static void test_pictures() {
    Pic pic{kId, "Primary", "abc123"};
    CHECK(image_path(pic, 46, 69) == std::string("/Items/") + kId +
                                         "/Images/Primary?fillWidth=46&fillHeight=69&quality=90&format=Jpg&tag=abc123");
    pic.tag.clear();
    CHECK(image_path(pic, 100, 56) == std::string("/Items/") + kId +
                                          "/Images/Primary?fillWidth=100&fillHeight=56&quality=90&format=Jpg");
    pic.tag = "a&b=c";   // never sent
    CHECK(!has(image_path(pic, 10, 10), "tag="));
    pic.type = "Backdrop";
    CHECK(has(image_path(pic, 10, 10), "/Images/Backdrop?"));
    pic.type = "Logo/../..";
    CHECK(image_path(pic, 10, 10).empty());
    pic.type = "Primary";
    CHECK(image_path(pic, 0, 10).empty() && image_path(pic, 10, 99999).empty());
    pic.id = "x/y";
    CHECK(image_path(pic, 10, 10).empty());
    CHECK(image_path(Pic{}, 10, 10).empty());
}

static void test_safety() {
    CHECK(safe_id(kId) && safe_id(kId2));
    CHECK(!safe_id("") && !safe_id("abc/def") && !safe_id("abc def") && !safe_id(std::string(65, 'a')));
    CHECK(safe_token("0123abcdEF-_.") && !safe_token("") && !safe_token("a\"b") && !safe_token("a\nb") &&
          !safe_token(std::string(257, 'a')));
    CHECK(url_encode("a b/c?d=e&f~g-h_i.j") == "a%20b%2Fc%3Fd%3De%26f~g-h_i.j");
    CHECK(url_encode("caf\xC3\xA9") == "caf%C3%A9");
    CHECK(status_error(0) == "Can't reach the server");
    CHECK(has(status_error(401), "sign in again"));
    CHECK(status_error(500) == "Server error 500");
}

int main() {
    test_parse();
    test_paths();
    test_search();
    test_pictures();
    test_safety();
    if (failures) return 1;
    std::printf("ok   jellyfin_api_test\n");
    return 0;
}
