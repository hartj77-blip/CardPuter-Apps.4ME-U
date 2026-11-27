// Browser: a web browser for the small screen. Markdown is shown as
// formatted text, HTML pages as their text, links, pictures and forms,
// anything else as plain text. No CSS or JavaScript.
//
// A page is a Control screen (docs/UX.md in the OS): its letters work as
// they are, and every one is in its action list, so the soft-key bar, the
// Tab menu and the key sheet (0) show them:
//   Up / Down, J / K  scroll a line      Fn Page Up / Down   a screen
//   Left / Right   previous / next link or form control
//   Enter          open the link, type in the box, tick the box, press the button
//   Esc, Bksp      back                  G  type an address (or words to search)
//   S  search      C  contents (a List of the headings)   F  find on the page
//   B  bookmark    L  history            I  pictures on / off
//   R  reload      H  home (bookmarks)
//
// Pictures (PNG, JPEG, GIF, BMP) are fetched after the page shows, on a
// thread of their own, decoded by picture.cpp and laid in as they arrive.
// Cookies are kept in ~/.config/zeroos/browser/cookies.txt, so sites that
// need a sign-in remember it.
//
// GitHub links open the file behind them (github.com/o/r is its README.md),
// so docs come as Markdown, not GitHub's heavy HTML. Pages are fetched with
// curl on a worker thread and kept in ~/.cache/zeroos/browser, so a page
// read once can be read again offline. Other apps can open a page with
// shell_open_app("browser", "<url or file path>").
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "apps/browser/page.h"
#include "apps/browser/picture.h"
#include "apps/common.h"
#include "platform/keys.h"
#include "ui/list_view.h"
#include "ui/fileio.h"
#include "apps/kit/look.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern char** environ;

namespace zs {
namespace {
using namespace theme;
using namespace web;

constexpr const char* kHomeUrl = "about:home";
constexpr size_t kMaxBytes = 4 * 1024 * 1024;
constexpr int kPad = 6;      // left and right margin
constexpr int kTitleH = 16;  // the title row
constexpr size_t kMaxPictures = 40;      // per page
constexpr size_t kMaxHistory = 300;      // remembered visits

const char* const kOs = "https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/HEAD/";

using Bookmarks = std::vector<std::pair<std::string, std::string>>;   // (url, title)

std::string home_page(const Bookmarks& bookmarks) {
    std::string os = kOs;
    std::string md;
    if (!bookmarks.empty()) {
        md += "## Bookmarks\n\n";
        for (auto& b : bookmarks) {
            std::string t;
            for (char ch : b.second.empty() ? short_url(b.first) : b.second) {
                if (std::strchr("\\[]*_`<>&", ch)) t += '\\';
                t += ch;
            }
            md += "- [" + t + "](<" + b.first + ">)\n";
        }
        md += "\n";
    }
    md += "Press **S** to search the web, **G** to type an address, **B** to bookmark a page.\n\n"
        "## ZeroOS\n\n"
        "- [ZeroOS README](https://github.com/hartj77-blip/CardPuter-OS.4ME-U)\n"
        "- [Apps README](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U)\n\n"
        "## Using it\n\n";
    struct Doc {
        const char* name;
        const char* file;
    };
    static const Doc using_it[] = {{"Keys", "KEYS"}, {"Apps", "APPS"}, {"Updates", "UPDATE"},
                                   {"Battery and performance", "PERFORMANCE"}};
    static const Doc apps[] = {{"AI", "AI"},         {"Camera", "CAMERA"},     {"Car", "CAR"},
                               {"Dev Tools", "DEV"}, {"Dock", "DOCK"},
                               {"GPS and LoRa", "EXPANSION"}, {"IR Remote", "IR"}, {"Libretro", "GAMES"},
                               {"Media and Jellyfin", "JELLYFIN"},
                               {"Remote", "REMOTE"}, {"Sync", "SYNC"},         {"Tether", "TETHER"}};
    static const Doc dev[] = {{"Architecture", "ARCHITECTURE"}, {"Interface spec", "UX"},
                              {"Hardware notes", "HARDWARE"}, {"Hardware test checklist", "HARDWARE-TEST"},
                              {"Roadmap", "ROADMAP"}};
    auto list = [&](const Doc* d, size_t n) {
        for (size_t i = 0; i < n; ++i) md += std::string("- [") + d[i].name + "](" + os + "docs/" + d[i].file + ".md)\n";
        md += "\n";
    };
    list(using_it, std::size(using_it));
    md += "## App guides\n\n";
    list(apps, std::size(apps));
    md += "## Development\n\n";
    list(dev, std::size(dev));
    md += "## On this device\n\n- [Home folder](file://" + home_dir() + "/)\n";
    return md;
}

// ---- fetching -------------------------------------------------------------------

struct Fetched {
    bool ok = false;
    std::string url;            // after redirects
    std::string content_type;
    std::string body;
    std::string error;
    bool from_cache = false;
};

std::string cache_dir() { return home_dir() + "/.cache/zeroos/browser"; }

// Bookmarks, history, cookies and settings: ~/.config/zeroos/browser.
std::string data_dir() {
    std::string d = home_dir() + "/.config";
    mkdir(d.c_str(), 0755);
    mkdir((d + "/zeroos").c_str(), 0755);
    d += "/zeroos/browser";
    mkdir(d.c_str(), 0700);
    return d;
}

std::string cookie_jar() { return data_dir() + "/cookies.txt"; }

// "url<TAB>title" lines.
Bookmarks read_list(const std::string& path) {
    Bookmarks out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t t = line.find('\t');
        std::string url = line.substr(0, t), title = t == std::string::npos ? "" : line.substr(t + 1);
        if (!url.empty()) out.push_back({url, title});
    }
    return out;
}

void write_list(const std::string& path, const Bookmarks& list) {
    std::string s;
    for (auto& e : list) {
        std::string t = e.second;
        std::replace(t.begin(), t.end(), '\t', ' ');
        std::replace(t.begin(), t.end(), '\n', ' ');
        s += e.first + "\t" + t + "\n";
    }
    write_file_atomic(path, s, 0600);
}

std::string cache_path(const std::string& url) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a
    for (unsigned char ch : url) h = (h ^ ch) * 1099511628211ULL;
    return cache_dir() + "/" + fmt("%016llx", (unsigned long long)h);
}

void cache_put(const std::string& url, const Fetched& f) {
    std::string d = home_dir() + "/.cache";
    mkdir(d.c_str(), 0755);
    mkdir((d + "/zeroos").c_str(), 0755);
    mkdir(cache_dir().c_str(), 0755);
    write_file_atomic(cache_path(url), f.url + "\n" + f.content_type + "\n" + f.body, 0600);
}

bool cache_get(const std::string& url, Fetched& f) {
    std::ifstream in(cache_path(url), std::ios::binary);
    if (!in) return false;
    std::string u, ct;
    if (!std::getline(in, u) || !std::getline(in, ct)) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    f.ok = true;
    f.url = u;
    f.content_type = ct;
    f.body = ss.str();
    f.from_cache = true;
    return true;
}

std::string url_path_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// A folder as a page of links.
std::string folder_page(const std::string& path) {
    struct E {
        std::string name;
        bool dir;
    };
    std::vector<E> es;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.') continue;
            struct stat st{};
            if (stat((path + "/" + n).c_str(), &st) != 0) continue;
            es.push_back({n, S_ISDIR(st.st_mode)});
        }
        closedir(d);
    }
    std::sort(es.begin(), es.end(), [](const E& a, const E& b) { return a.dir != b.dir ? a.dir : a.name < b.name; });
    std::string md = "# " + path + "\n\n";
    if (path != "/") md += "- [.. (up)](../)\n";
    for (auto& e : es) {
        std::string shown;
        for (char ch : e.name) {
            if (std::strchr("\\[]*_`<>&", ch)) shown += '\\';
            shown += ch;
        }
        md += "- [" + shown + (e.dir ? "/" : "") + "](<" + e.name + (e.dir ? "/" : "") + ">)\n";
    }
    if (es.empty()) md += "\n(empty folder)\n";
    return md;
}

Fetched fetch_file(const std::string& url) {
    Fetched f;
    f.url = url;
    std::string path = url_path_decode(url.substr(7));   // after file://
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) {
        f.error = "No such file";
        return f;
    }
    if (S_ISDIR(st.st_mode)) {
        if (f.url.back() != '/') f.url += '/';
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        f.body = folder_page(path);
        f.content_type = "text/markdown";
        f.ok = true;
        return f;
    }
    if (!S_ISREG(st.st_mode)) {
        f.error = "Not a file";
        return f;
    }
    if (size_t(st.st_size) > kMaxBytes) {
        f.error = "Too large to show";
        return f;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    f.body = ss.str();
    f.ok = bool(in) || !f.body.empty() || st.st_size == 0;
    if (!f.ok) f.error = "Couldn't read the file";
    return f;
}

// One request with curl. Blocks: run it off the UI thread.
// post: a form's body (POST), or empty (GET). picture: fetching a picture
// (asks for the formats picture.cpp reads, and leaves the cookies as they are).
Fetched fetch_http(const std::string& url_in, std::atomic<int>* pid_out, const std::string& post = "",
                   bool picture = false) {
    Fetched f;
#ifdef __EMSCRIPTEN__
    (void)url_in;
    (void)pid_out;
    (void)post;
    (void)picture;
    f.error = "The browser demo can't load pages";
    return f;
#else
    std::string url;
    for (char ch : url_in) {   // curl won't take a raw space
        if (ch == ' ') url += "%20";
        else url += ch;
    }
    std::string jar = cookie_jar();
    std::vector<const char*> argv = {"curl", "-sS", "-L", "--compressed", "--max-time", "30", "--connect-timeout", "10",
                                     "--max-filesize", "4194304", "-A", "ZeroOS-Browser/1.0", "-b", jar.c_str()};
    if (picture) {
        argv.insert(argv.end(), {"-H", "Accept: image/png,image/jpeg,image/gif;q=0.9,*/*;q=0.1"});
    } else {
        argv.insert(argv.end(), {"-c", jar.c_str(), "-H",
                                 "Accept: text/html,text/markdown;q=0.9,text/plain;q=0.8,*/*;q=0.5"});
    }
    if (!post.empty()) argv.insert(argv.end(), {"--data-binary", post.c_str()});   // form_encode()d: never "@file"
    argv.insert(argv.end(), {"-w", "\n\x1f%{http_code}\x1f%{content_type}\x1f%{url_effective}", url.c_str(), nullptr});
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) {
        f.error = "Couldn't start curl";
        return f;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "curl", &fa, nullptr, const_cast<char* const*>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out[1]);
    std::string body;
    int status = -1;
    if (rc == 0) {
        if (pid_out) pid_out->store(pid);
        char buf[8192];
        ssize_t n;
        while ((n = read(out[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
            if (n > 0) body.append(buf, size_t(n));
            if (body.size() > kMaxBytes + 4096) {
                kill(pid, SIGTERM);
                break;
            }
        }
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (pid_out) pid_out->store(0);
        if (!picture) chmod(jar.c_str(), 0600);   // cookies are sign-ins: only for you
    }
    close(out[0]);
    if (rc != 0) {
        f.error = "curl is missing";
        return f;
    }
    size_t m = body.rfind("\n\x1f");
    if (m == std::string::npos) {
        f.error = WIFSIGNALED(status) ? "Stopped" : "Couldn't connect (offline?)";
        return f;
    }
    std::string tail = body.substr(m + 2);
    body.erase(m);
    size_t a = tail.find('\x1f'), b = a == std::string::npos ? a : tail.find('\x1f', a + 1);
    int code = std::atoi(tail.c_str());
    if (a != std::string::npos) f.content_type = tail.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    f.url = b != std::string::npos ? tail.substr(b + 1) : url_in;
    if (code == 0) {
        f.error = "Couldn't connect (offline?)";
        return f;
    }
    if (code < 200 || code >= 300) {
        f.error = code == 404 ? "Not found (404)" : fmt("The server said %d", code);
        f.body.clear();
        return f;
    }
    f.body = std::move(body);
    f.ok = true;
    return f;
#endif
}

// What a page load does, start to finish, on the worker thread.
Fetched load(const std::string& url, bool prefer_cache, std::atomic<int>* pid, const std::string& post = "",
             bool picture = false) {
    if (url.compare(0, 7, "file://") == 0) return fetch_file(url);
    Fetched cached;
    bool have = post.empty() && cache_get(url, cached);
    if (prefer_cache && have) return cached;
    Fetched f = fetch_http(url, pid, post, picture);
    // A folder on GitHub ("apps/battery" from a README): try its README.
    if (!f.ok && f.error == "Not found (404)" && url.find("raw.githubusercontent.com/") != std::string::npos) {
        std::string last = url.substr(url.rfind('/') + 1);
        if (last.find('.') == std::string::npos) {
            std::string alt = url + (url.back() == '/' ? "" : "/") + "README.md";
            Fetched g = fetch_http(alt, pid, "", picture);
            if (g.ok) {
                cache_put(url, g);
                return g;
            }
        }
    }
    if (f.ok) {
        cache_put(url, f);
        return f;
    }
    if (have && f.error != "Stopped") {
        cached.error = f.error;   // shown as "offline copy"
        return cached;
    }
    return f;
}


#ifdef __EMSCRIPTEN__
// ---- fetching in the browser simulator ------------------------------------------
//
// No curl and no threads in a web page: the page's own fetch(), started here
// and polled from tick(). A web page may only read a site that allows it
// (CORS): GitHub's raw files, Wikipedia's API and many picture hosts do, most
// sites don't. The Cardputer itself has no such limit.

EM_JS(int, zs_web_fetch_start, (const char* url, const char* post), {
    const jobs = Module.zsFetches || (Module.zsFetches = {});
    const id = Module.zsFetchNext = (Module.zsFetchNext || 0) + 1;
    const job = jobs[id] = {state: 0};
    const opts = {credentials: 'omit', redirect: 'follow'};
    const body = UTF8ToString(post);
    if (body) {
        opts.method = 'POST';
        opts.body = body;
        opts.headers = {'Content-Type': 'application/x-www-form-urlencoded'};
    }
    fetch(UTF8ToString(url), opts).then(async (r) => {
        job.status = r.status;
        job.parts = [null, new TextEncoder().encode(r.headers.get('content-type') || ''),
                     new TextEncoder().encode(r.url || '')];
        job.parts[0] = new Uint8Array(await r.arrayBuffer());
        job.state = 1;
    }).catch(() => { job.state = -1; });
    return id;
});
// 0: still loading, 1: done, -1: couldn't fetch (offline, or the site doesn't allow it).
EM_JS(int, zs_web_fetch_state, (int id), {
    const j = (Module.zsFetches || {})[id];
    return j ? j.state : -1;
});
EM_JS(int, zs_web_fetch_status, (int id), { return Module.zsFetches[id].status; });
// part 0: the body, 1: the content type, 2: the address after redirects
EM_JS(int, zs_web_fetch_size, (int id, int part), { return Module.zsFetches[id].parts[part].length; });
EM_JS(void, zs_web_fetch_copy, (int id, int part, char* out), {
    HEAPU8.set(Module.zsFetches[id].parts[part], out);
});
EM_JS(void, zs_web_fetch_free, (int id), {
    if (Module.zsFetches) delete Module.zsFetches[id];
});

std::string web_part(int id, int part) {
    std::string s(size_t(zs_web_fetch_size(id, part)), '\0');
    if (!s.empty()) zs_web_fetch_copy(id, part, &s[0]);
    return s;
}

// Pages here are served over https, so http:// can't be fetched: try https.
std::string web_url(const std::string& url) {
    std::string u = url.compare(0, 7, "http://") == 0 ? "https://" + url.substr(7) : url;
    std::string out;
    for (char ch : u) out += ch == ' ' ? std::string("%20") : std::string(1, ch);
    return out;
}

// A finished fetch as Fetched (false while it is still loading). Frees it.
bool web_poll(int id, const std::string& url, Fetched& f) {
    int st = zs_web_fetch_state(id);
    if (st == 0) return false;
    f = Fetched{};
    f.url = url;
    if (st < 0) {
        f.error = "The simulator can't open this site: it doesn't let other web pages read it (CORS), "
                  "or you're offline. The Cardputer opens any site.";
    } else {
        int code = zs_web_fetch_status(id);
        f.content_type = web_part(id, 1);
        std::string u = web_part(id, 2);
        if (!u.empty()) f.url = u;
        if (code < 200 || code >= 300) {
            f.error = code == 404 ? "Not found (404)" : fmt("The server said %d", code);
        } else {
            f.body = web_part(id, 0);
            if (f.body.size() > kMaxBytes) f.error = "Too large to show";
            else f.ok = true;
        }
    }
    zs_web_fetch_free(id);
    return true;
}
#endif

struct Job {
    std::mutex m;
    bool done = false;
    Fetched result;
    std::atomic<int> pid{0};
    int web_id = 0;          // the simulator: the fetch() in flight
    std::string url, post;   // the simulator: what it is for
    bool readme_tried = false;   // the simulator: a GitHub folder's README.md was tried
};

// The pictures of one page, fetched and decoded one after another.
struct PictureJob {
    struct Want {
        int image;
        std::string url;   // resolved, or a data: URI
        int w, h;          // the size the page asks for (0: not given)
    };
    std::vector<Want> wants;
    int max_w = 0, max_h = 0;

    std::mutex m;
    std::vector<std::pair<int, std::shared_ptr<Picture>>> ready;   // taken by tick()
    bool done = false;
    std::atomic<bool> cancel{false};
    std::atomic<int> pid{0};

    void add(const Want& w, const std::string& bytes) {
        auto p = std::make_shared<Picture>();
        if (!decode_picture(bytes, w.w, w.h, max_w, max_h, kBg, *p)) return;
        std::lock_guard<std::mutex> l(m);
        ready.push_back({w.image, std::move(p)});
    }

    // On its own thread (the Cardputer).
    void run() {
        for (auto& w : wants) {
            if (cancel) break;
            std::string bytes;
            if (!data_uri(w.url, bytes)) {
                Fetched f = load(w.url, true, &pid, "", true);
                if (!f.ok || cancel) continue;
                bytes = std::move(f.body);
            }
            add(w, bytes);
        }
        std::lock_guard<std::mutex> l(m);
        done = true;
    }

#ifdef __EMSCRIPTEN__
    // The simulator: one step per tick, a fetch() at a time.
    size_t next = 0;
    int web_id = 0;

    void step() {
        for (int decoded = 0; decoded < 2 && !done;) {
            if (web_id) {
                Fetched f;
                if (!web_poll(web_id, wants[next].url, f)) return;
                web_id = 0;
                if (f.ok) {
                    cache_put(wants[next].url, f);
                    add(wants[next], f.body);
                }
                ++next;
                ++decoded;
                continue;
            }
            if (next >= wants.size()) {
                done = true;
                return;
            }
            std::string bytes;
            Fetched cached;
            if (data_uri(wants[next].url, bytes) || (cache_get(wants[next].url, cached) && !cached.body.empty())) {
                add(wants[next], bytes.empty() ? cached.body : bytes);
                ++next;
                ++decoded;
                continue;
            }
            web_id = zs_web_fetch_start(web_url(wants[next].url).c_str(), "");
            return;
        }
    }

    ~PictureJob() {
        if (web_id) zs_web_fetch_free(web_id);
    }
#endif
};

// A decoded picture as LVGL draws it (the descriptor points into pic).
struct Shown {
    std::shared_ptr<Picture> pic;
    lv_image_dsc_t dsc{};
};

// ---- the screen -------------------------------------------------------------------

const lv_font_t* font_for(Style s) {
    switch (s) {
        case Style::H1: return &zs_font_20;
        case Style::H2: return &zs_font_16;
        case Style::H3: return &zs_font_14;
        default: return &zs_font_12;
    }
}

uint32_t color_for(Style s) {
    switch (s) {
        case Style::Strong: return kText;
        case Style::Code: return kInfo;
        case Style::Muted:
        case Style::Quote: return kMuted;
        case Style::H1:
        case Style::H2:
        case Style::H3: return kText;
        default: return kGrey7;
    }
}

class BrowserApp : public App {
public:
    ~BrowserApp() override {
        stop();
        stop_pictures();
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        title_ = label(root, "", &zs_font_12, kAccent);
        lv_obj_set_pos(title_, kPad, 1);
        lv_obj_set_width(title_, kScreenW - 2 * kPad - 40);
        lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
        pos_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(pos_, LV_ALIGN_TOP_RIGHT, -kPad, 1);

        view_ = lv_obj_create(root);
        style_plain(view_);
        lv_obj_set_pos(view_, 0, kTitleH);
        lv_obj_set_size(view_, kScreenW, view_h());
        lv_obj_remove_flag(view_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(view_, LV_OBJ_FLAG_CLICKABLE);

        metrics_.width = [](const std::string& s, Style st) {
            return int(lv_text_get_width(s.c_str(), uint32_t(s.size()), font_for(st), 0));
        };
        metrics_.height = [](Style st) { return int(lv_font_get_line_height(font_for(st))); };
        metrics_.image_size = [this](int i, int& w, int& h) {
            auto it = shown_.find(i);
            if (it == shown_.end()) return false;
            w = it->second->pic->w;
            h = it->second->pic->h;
            return true;
        };

        bookmarks_ = read_list(data_dir() + "/bookmarks");
        visited_ = read_list(data_dir() + "/history");
        std::ifstream set(data_dir() + "/settings");
        std::string line;
        while (std::getline(set, line))
            if (line == "pictures=off") pictures_on_ = false;

        set_actions();
        std::string arg = shell_app_arg();
        if (!arg.empty() && arg[0] == '/') arg = "file://" + arg;
        go(arg.empty() ? kHomeUrl : arg);
    }

    void tick() override {
        take_pictures();
        if (!job_) return;
#ifdef __EMSCRIPTEN__
        if (job_->web_id) {
            Fetched f;
            if (web_poll(job_->web_id, job_->url, f)) {
                job_->web_id = 0;
                // A folder on GitHub ("apps/battery" from a README): try its README, as load() does.
                std::string last = job_->url.substr(job_->url.rfind('/') + 1);
                if (!f.ok && f.error == "Not found (404)" && !job_->readme_tried &&
                    job_->url.find("raw.githubusercontent.com/") != std::string::npos &&
                    last.find('.') == std::string::npos) {
                    job_->readme_tried = true;
                    std::string alt = job_->url + (job_->url.back() == '/' ? "" : "/") + "README.md";
                    job_->web_id = zs_web_fetch_start(alt.c_str(), "");
                } else {
                    Fetched cached;
                    if (f.ok) cache_put(job_->url, f);
                    else if (job_->post.empty() && cache_get(job_->url, cached)) {
                        cached.error = f.error;   // shown as "offline copy"
                        f = std::move(cached);
                    }
                    job_->result = std::move(f);
                    job_->done = true;
                }
            }
        }
#endif
        {
            std::lock_guard<std::mutex> l(job_->m);
            if (!job_->done) {
                static const char* const d[] = {"", ".", "..", "..."};
                lv_label_set_text(pos_, d[(++dots_ / 3) % 4]);
                return;
            }
        }
        Fetched f = std::move(job_->result);
        if (worker_.joinable()) worker_.join();
        job_.reset();
        loaded(f);
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // typing
        if (picker_) return picker_list_.key(key);
        switch (key) {
            case LV_KEY_DOWN: case 'j': scroll(1); return true;
            case LV_KEY_UP: case 'k': scroll(-1); return true;
            case kKeyPageDown: page(1); return true;
            case kKeyPageUp: page(-1); return true;
            case LV_KEY_HOME: top(); return true;
            case LV_KEY_END: bottom(); return true;
            case LV_KEY_RIGHT: next_link(1); return true;
            case LV_KEY_LEFT: next_link(-1); return true;
            case LV_KEY_ENTER: case '\r': follow(); return true;
            case LV_KEY_BACKSPACE: return on_back();
            case 'g': case 'G': ask_address(); return true;
            case 's': case 'S': ask_search(); return true;
            case 'c': case 'C': open_contents(); return true;
            case 'f': case 'F': ask_find(); return true;
            case 'b': case 'B': toggle_bookmark(); return true;
            case 'l': case 'L': open_history(); return true;
            case 'i': case 'I': toggle_pictures(); return true;
            case 'r': case 'R': reload(); return true;
            case 'h': case 'H': go(kHomeUrl); return true;
            default: return false;
        }
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (picker_) {
            close_picker();
            return true;
        }
        if (job_) {   // stop loading
            stop();
            show_message(LV_SYMBOL_CLOSE " Stopped");
            return true;
        }
        if (history_.size() > 1) {
            history_.pop_back();
            open(history_.back(), true);
            return true;
        }
        return false;
    }

private:
    struct Visit {
        std::string url;    // with #fragment
        size_t top = 0;
        std::string post;   // a form sent with POST: its body
    };

    int view_h() const { return kBodyH - kTitleH; }

    void set_actions() {
        std::vector<Action> a;
        a.push_back({"Open", 0, "Enter", nullptr});
        a.push_back({"Address", 'g', "G", [this] { ask_address(); }});
        a.push_back({"Search", 's', "S", [this] { ask_search(); }});
        a.push_back({"Links", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
        a.push_back({"Back", 0, "Esc", nullptr});
        a.push_back({"Bookmark", 'b', "B", [this] { toggle_bookmark(); }});
        a.push_back({"History", 'l', "L", [this] { open_history(); }});
        a.push_back({"Contents", 'c', "C", [this] { open_contents(); }});
        a.push_back({"Find on page", 'f', "F", [this] { ask_find(); }});
        a.push_back({pictures_on_ ? "Pictures off" : "Pictures on", 'i', "I", [this] { toggle_pictures(); }});
        a.push_back({"Reload", 'r', "R", [this] { reload(); }});
        a.push_back({"Home", 'h', "H", [this] { go(kHomeUrl); }});
        a.push_back({"Page " LV_SYMBOL_DOWN, 0, "PgDn", nullptr});
        a.push_back({"Page " LV_SYMBOL_UP, 0, "PgUp", nullptr});
        a.push_back({"Scroll", 0, "J K", nullptr});
        shell_set_actions(std::move(a));
    }

    // ---- navigation

    // Open a link or typed address as a new step in the history. post: a
    // form's body, to send with POST.
    void go(const std::string& url_in, const std::string& post = "") {
        std::string url = url_in == kHomeUrl ? url_in : rewrite_url(url_in);
        if (unsupported_by_name(url)) {
            shell_toast("Can't show this kind of file yet");
            return;
        }
        if (url.compare(0, 7, "mailto:") == 0 || (url != kHomeUrl && url.find("://") == std::string::npos)) {
            shell_toast("Can't open " + url.substr(0, url.find(':') + 1) + " links");
            return;
        }
        if (!history_.empty()) history_.back().top = top_;
        // Same page, other heading: no reload.
        std::string frag_url = url, frag = split_fragment(frag_url);
        if (!job_ && post.empty() && frag_url == page_url_ && !frag.empty()) {
            history_.push_back({url, top_, ""});
            jump_to(frag);
            return;
        }
        history_.push_back({url, 0, post});
        open(history_.back(), false);
    }

    // Show a page: from the history (back: the saved copy, where it was) or fresh.
    void open(const Visit& v, bool from_history) {
        stop();
        stop_pictures();
        std::string url = v.url;
        pending_frag_ = split_fragment(url);
        restore_top_ = from_history ? v.top : 0;
        if (url == kHomeUrl) {
            Fetched f;
            f.ok = true;
            f.url = kHomeUrl;
            f.content_type = "text/markdown";
            f.body = home_page(bookmarks_);
            loaded(f);
            return;
        }
        if (url.compare(0, 7, "file://") == 0) {   // local: quick, no thread
            loaded(load(url, false, nullptr));
            return;
        }
        loading_url_ = url;
        lv_label_set_text(title_, short_url(url).c_str());
        show_message("Loading " + short_url(url));
        job_ = std::make_shared<Job>();
        auto job = job_;
        bool cache = from_history;
        std::string post = v.post;
#ifdef __EMSCRIPTEN__
        Fetched cached;
        if (cache && post.empty() && cache_get(url, cached)) {
            job->result = std::move(cached);
            job->done = true;
            return;
        }
        job->url = url;
        job->post = post;
        job->web_id = zs_web_fetch_start(web_url(url).c_str(), post.c_str());
#else
        worker_ = std::thread([job, url, cache, post] {
            Fetched f = load(url, cache, &job->pid, post);
            std::lock_guard<std::mutex> l(job->m);
            job->result = std::move(f);
            job->done = true;
        });
#endif
    }

    void reload() {
        if (history_.empty()) return;
        history_.back().top = top_;
        Visit v = history_.back();
        open(v, false);
        restore_top_ = v.top;
    }

    void stop() {
#ifdef __EMSCRIPTEN__
        if (job_ && job_->web_id) zs_web_fetch_free(job_->web_id);   // its answer is ignored
#endif
        if (job_)
            if (int pid = job_->pid.load()) kill(pid, SIGTERM);
        if (worker_.joinable()) worker_.join();
        job_.reset();
    }

    void loaded(const Fetched& f) {
        if (!f.ok) {
            show_message(LV_SYMBOL_WARNING " " + f.error, f.url.empty() ? loading_url_ : f.url);
            lv_label_set_text(pos_, "");
            return;
        }
        Kind k = content_kind(f.url, f.content_type, f.body);
        if (f.url.compare(0, 7, "file://") == 0 && f.content_type.empty() && k == Kind::Html &&
            f.url.find(".htm") == std::string::npos)
            k = Kind::Text;
        std::string page_url = f.url;
        split_fragment(page_url);
        std::string name = page_url.substr(page_url.find_last_of('/') + 1);
        bool picture = k == Kind::Unsupported &&
                       (f.content_type.compare(0, 6, "image/") == 0 || image_by_name(f.url));
        if (k == Kind::Unsupported && !picture) {
            show_message(LV_SYMBOL_WARNING " Can't show this kind of file yet", f.url);
            return;
        }
        clear_pictures();
        if (picture) {   // a picture on its own: a page with just it
            doc_ = Doc{};
            doc_.title = name;
            doc_.images.push_back({page_url, name, 0, 0, -1});
            Block b;
            b.kind = Block::Image;
            b.image = 0;
            b.runs.push_back({name, Style::Muted, -1, -1});
            doc_.blocks.push_back(b);
        } else {
            std::string body = to_utf8(f.body, f.content_type);
            doc_ = k == Kind::Markdown ? parse_markdown(body) : k == Kind::Html ? parse_html(body) : parse_text(body);
        }
        page_url_ = page_url;
        // Relative links resolve against the address after redirects.
        if (!history_.empty() && f.url != kHomeUrl && !f.from_cache) {
            std::string frag = split_fragment(history_.back().url);
            history_.back().url = page_url_ + (frag.empty() ? "" : "#" + frag);
        }
        title_text_ = f.url == kHomeUrl ? std::string(LV_SYMBOL_HOME " Home")
                      : !doc_.title.empty() ? doc_.title : !name.empty() ? name : short_url(page_url_);
        focus_ = -1;
        lines_ = layout(doc_, kScreenW - 2 * kPad, metrics_);
        index_links();
        top_ = std::min(restore_top_, lines_.empty() ? 0 : lines_.size() - 1);
        if (!pending_frag_.empty()) jump_to(pending_frag_, false);
        render();
        if (f.from_cache && !f.error.empty()) shell_toast("Offline: showing a saved copy");
        if (f.url != kHomeUrl) remember_visit();
        start_pictures();
    }

    void index_links() {
        link_line_.assign(doc_.links.size(), -1);
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].link >= 0 && link_line_[size_t(lines_[i].link)] < 0) link_line_[size_t(lines_[i].link)] = int(i);
            for (auto& p : lines_[i].pieces)
                if (p.link >= 0 && link_line_[size_t(p.link)] < 0) link_line_[size_t(p.link)] = int(i);
        }
    }

    // Lay the page out again (a picture arrived, a form control changed),
    // keeping the same text at the top of the screen.
    void relayout() {
        size_t block = 0, into = 0;
        if (top_ < lines_.size()) {
            block = lines_[top_].block;
            size_t first = top_;
            while (first > 0 && lines_[first - 1].block == block) --first;
            into = top_ - first;
        }
        bool at_top = top_ == 0;
        lines_ = layout(doc_, kScreenW - 2 * kPad, metrics_);
        index_links();
        if (at_top) {
            top_ = 0;
        } else {
            size_t i = 0;
            while (i < lines_.size() && lines_[i].block < block) ++i;
            size_t n = 0;
            while (n < into && i + n + 1 < lines_.size() && lines_[i + n + 1].block == block) ++n;
            top_ = std::min(i + n, lines_.empty() ? 0 : lines_.size() - 1);
        }
        settle_focus();
        render();
    }

    void jump_to(const std::string& frag, bool redraw = true) {
        auto it = doc_.anchors.find(frag);
        if (it == doc_.anchors.end()) it = doc_.anchors.find(url_path_decode(frag));
        if (it != doc_.anchors.end()) {
            for (size_t i = 0; i < lines_.size(); ++i)
                if (lines_[i].block >= it->second) {
                    top_ = i;
                    break;
                }
        } else if (redraw) {
            shell_toast("No \"" + frag + "\" on this page");
        }
        focus_ = -1;
        if (redraw) render();
    }

    void follow() {
        if (focus_ < 0 || size_t(focus_) >= doc_.links.size()) {
            shell_toast("No link chosen");
            return;
        }
        std::string href = doc_.links[size_t(focus_)];
        int field = href_field(href);
        if (field >= 0 && size_t(field) < doc_.fields.size()) {
            use_field(field);
            return;
        }
        go(resolve_url(page_url_ == kHomeUrl ? std::string("https://") : page_url_, href));
    }

    // ---- forms

    void use_field(int fi) {
        Field& f = doc_.fields[size_t(fi)];
        switch (f.type) {
            case Field::Text:
            case Field::TextArea:
            case Field::Password: {
                std::string title = !f.label.empty() ? f.label : !f.name.empty() ? f.name : "Text";
                if (title.size() > 40) title = title.substr(0, 40);
                ask(title.c_str(), f.value, [this, fi](const std::string& v) {
                    Field& f = doc_.fields[size_t(fi)];
                    f.value = v;
                    update_field(doc_, fi);
                    // A search box (the form's only one): Enter sends it. Otherwise on to the next box.
                    if (f.form >= 0 && text_fields(doc_, f.form) == 1) {
                        submit(f.form, -1);
                        return;
                    }
                    relayout();
                    next_link(1);
                }, "", f.type == Field::Password);
                break;
            }
            case Field::Checkbox:
            case Field::Radio:
                toggle_field(doc_, fi);
                relayout();
                break;
            case Field::Select: {
                std::vector<std::string> rows;
                for (auto& o : f.options) rows.push_back(o.first);
                open_picker(rows, f.selected, "No choices", [this, fi](int i) {
                    doc_.fields[size_t(fi)].selected = i;
                    update_field(doc_, fi);
                    relayout();
                });
                break;
            }
            case Field::Submit:
                if (f.form < 0) shell_toast("This button needs JavaScript");
                else submit(f.form, fi);
                break;
            case Field::Hidden: break;
        }
    }

    void submit(int form, int button) {
        Submission s = submit_form(doc_, form, button, page_url_);
        if (s.url.empty()) return;
        go(s.url, s.is_post ? (s.post.empty() ? std::string("=") : s.post) : "");
    }

    // ---- pictures

    void clear_pictures() {
        stop_pictures();
        if (view_) lv_obj_clean(view_);   // no image widget may point at a descriptor about to go
        for (auto& s : shown_) lv_image_cache_drop(&s.second->dsc);
        shown_.clear();
    }

    void start_pictures() {
        if (!pictures_on_ || doc_.images.empty()) return;
        auto job = std::make_shared<PictureJob>();
        job->max_w = kScreenW - 2 * kPad;
        job->max_h = view_h() - 8;
        for (size_t i = 0; i < doc_.images.size() && job->wants.size() < kMaxPictures; ++i) {
            if (shown_.count(int(i))) continue;
            const Image& im = doc_.images[i];
            std::string url = im.src.compare(0, 5, "data:") == 0 ? im.src : resolve_url(page_url_, im.src);
            if (url.compare(0, 5, "data:") != 0 && url.find("://") == std::string::npos) continue;
            job->wants.push_back({int(i), url, im.w, im.h});
        }
        if (job->wants.empty()) return;
        pictures_ = job;
#ifndef __EMSCRIPTEN__
        picture_worker_ = std::thread([job] { job->run(); });   // the simulator steps it in take_pictures()
#endif
    }

    void stop_pictures() {
        if (!pictures_) return;
        pictures_->cancel = true;
        if (int pid = pictures_->pid.load()) kill(pid, SIGTERM);
        if (picture_worker_.joinable()) picture_worker_.join();
        pictures_.reset();
    }

    // Pictures that arrived since the last tick go into the page.
    void take_pictures() {
        if (!pictures_) return;
#ifdef __EMSCRIPTEN__
        pictures_->step();
#endif
        std::vector<std::pair<int, std::shared_ptr<Picture>>> got;
        bool done;
        {
            std::lock_guard<std::mutex> l(pictures_->m);
            got.swap(pictures_->ready);
            done = pictures_->done;
        }
        for (auto& g : got) {
            auto s = std::make_unique<Shown>();
            s->pic = g.second;
            s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            s->dsc.header.w = uint32_t(s->pic->w);
            s->dsc.header.h = uint32_t(s->pic->h);
            s->dsc.header.stride = uint32_t(s->pic->w * 2);
            s->dsc.data_size = uint32_t(s->pic->pixels.size() * 2);
            s->dsc.data = reinterpret_cast<const uint8_t*>(s->pic->pixels.data());
            shown_[g.first] = std::move(s);
        }
        if (done) {
            if (picture_worker_.joinable()) picture_worker_.join();
            pictures_.reset();
        }
        if (!got.empty() && !lines_.empty()) relayout();
    }

    void toggle_pictures() {
        pictures_on_ = !pictures_on_;
        write_file_atomic(data_dir() + "/settings", pictures_on_ ? "pictures=on\n" : "pictures=off\n", 0600);
        shell_toast(pictures_on_ ? "Pictures on" : "Pictures off");
        set_actions();
        if (pictures_on_) {
            start_pictures();
        } else {
            clear_pictures();
            if (!lines_.empty()) relayout();
        }
    }

    // ---- bookmarks and history

    void toggle_bookmark() {
        if (history_.empty() || history_.back().url == kHomeUrl || lines_.empty()) {
            shell_toast("Open a page to bookmark it");
            return;
        }
        std::string url = history_.back().url;
        auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(), [&](auto& b) { return b.first == url; });
        if (it != bookmarks_.end()) {
            bookmarks_.erase(it);
            shell_toast("Bookmark removed");
        } else {
            bookmarks_.push_back({url, title_text_});
            shell_toast("Bookmarked: on the home page (H)");
        }
        write_list(data_dir() + "/bookmarks", bookmarks_);
    }

    void remember_visit() {
        if (history_.empty() || !history_.back().post.empty()) return;
        std::string url = history_.back().url;
        split_fragment(url);
        visited_.erase(std::remove_if(visited_.begin(), visited_.end(), [&](auto& v) { return v.first == url; }),
                       visited_.end());
        visited_.push_back({url, title_text_});
        if (visited_.size() > kMaxHistory) visited_.erase(visited_.begin(), visited_.end() - kMaxHistory);
        write_list(data_dir() + "/history", visited_);
    }

    void open_history() {
        if (visited_.empty()) {
            shell_toast("No pages visited yet");
            return;
        }
        std::vector<std::string> rows;
        Bookmarks list(visited_.rbegin(), visited_.rend());   // newest first
        for (auto& v : list) rows.push_back(v.second.empty() ? short_url(v.first) : v.second);
        open_picker(rows, 0, "No pages", [this, list](int i) { go(list[size_t(i)].first); });
    }

    // ---- moving around

    size_t last_visible() const {   // index past the last line that fits
        int y = 0;
        size_t i = top_;
        for (; i < lines_.size(); ++i) {
            if (y + lines_[i].th > view_h()) break;
            y += lines_[i].h;
        }
        return i;
    }

    size_t max_top() const {   // the top line that shows the end of the page
        int y = 0;
        size_t i = lines_.size();
        while (i > 0) {
            int add = lines_[i - 1].h;
            if (y + (i == lines_.size() ? lines_[i - 1].th : add) > view_h()) break;
            y += add;
            --i;
        }
        return i;
    }

    void scroll(int dir) {
        if (lines_.empty()) return;
        size_t t = top_;
        if (dir > 0 && top_ < max_top()) ++top_;
        if (dir < 0 && top_ > 0) --top_;
        if (t != top_) settle_focus();
        render();
    }

    void page(int dir) {
        if (lines_.empty()) return;
        if (dir > 0) {
            size_t end = last_visible();
            top_ = std::min(end > top_ + 1 ? end - 1 : top_ + 1, max_top());   // keep one line for context
        } else {
            int y = 0;
            size_t i = top_;
            while (i > 0 && y + lines_[i - 1].h <= view_h() - lines_[top_].h) y += lines_[--i].h;
            top_ = i;
        }
        settle_focus();
        render();
    }

    void top() {
        top_ = 0;
        focus_ = -1;
        render();
    }

    void bottom() {
        top_ = max_top();
        focus_ = -1;
        render();
    }

    bool link_visible(int link) const {
        if (link < 0 || size_t(link) >= link_line_.size() || link_line_[size_t(link)] < 0) return false;
        size_t l = size_t(link_line_[size_t(link)]);
        return l >= top_ && l < last_visible();
    }

    // A focused link that scrolled away is no longer focused: Enter must
    // never open something you can't see.
    void settle_focus() {
        if (focus_ >= 0 && !link_visible(focus_)) focus_ = -1;
    }

    void next_link(int dir) {
        int n = int(doc_.links.size());
        for (int pass = 0; pass < 2; ++pass) {
            int start = focus_ >= 0 ? focus_ + dir : dir > 0 ? 0 : n - 1;
            for (int i = start; i >= 0 && i < n; i += dir)
                if (link_visible(i)) {
                    focus_ = i;
                    render();
                    return;
                }
            // none left on this screen: turn the page and take its first (or last) link
            if (pass == 1) break;
            size_t before = top_;
            if (dir > 0) page(1);
            else page(-1);
            if (top_ == before) break;
            focus_ = -1;
        }
        render();
    }

    // ---- drawing

    void show_message(const std::string& msg, const std::string& url = "") {
        clear_pictures();
        lines_.clear();
        doc_ = Doc{};
        link_line_.clear();
        focus_ = -1;
        top_ = 0;
        lv_obj_clean(view_);
        lv_obj_t* l = label(view_, msg.c_str(), &zs_font_12, kText);
        lv_obj_set_pos(l, kPad, 8);
        lv_obj_set_width(l, kScreenW - 2 * kPad);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        if (!url.empty()) {
            lv_obj_t* u = label(view_, short_url(url).c_str(), &zs_font_12, kMuted);
            lv_obj_update_layout(l);   // under the message, however many lines it took
            lv_obj_set_pos(u, kPad, 8 + lv_obj_get_height(l) + 8);
            lv_obj_set_width(u, kScreenW - 2 * kPad);
            lv_label_set_long_mode(u, LV_LABEL_LONG_WRAP);
        }
    }

    lv_obj_t* box(int x, int y, int w, int h, uint32_t color) {
        lv_obj_t* o = lv_obj_create(view_);
        style_plain(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(o, x, y);
        lv_obj_set_size(o, w, h);
        lv_obj_set_style_bg_color(o, c(color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(o, 0, 0);
        return o;
    }

    void render() {
        lv_obj_clean(view_);
        lv_label_set_text(title_, title_text_.c_str());
        int y = 0;
        size_t end = last_visible();
        for (size_t i = top_; i < end; ++i) {
            const Line& ln = lines_[i];
            if (ln.image >= 0) {
                // the whole picture, once, from its first strip on screen (the view clips it)
                bool first = i == top_ || lines_[i - 1].image != ln.image;
                auto it = shown_.find(ln.image);
                if (first && it != shown_.end()) {
                    lv_obj_t* im = lv_image_create(view_);
                    lv_image_set_src(im, &it->second->dsc);
                    lv_obj_set_pos(im, kPad + ln.image_x, y - ln.image_y);
                    if (ln.link >= 0 && ln.link == focus_) {
                        lv_obj_set_style_outline_width(im, 2, 0);
                        lv_obj_set_style_outline_color(im, c(kAccent), 0);
                        lv_obj_set_style_outline_pad(im, 1, 0);
                    }
                }
                y += ln.h;
                continue;
            }
            if (ln.shade) {
                bool more = i + 1 < lines_.size() && lines_[i + 1].shade && lines_[i + 1].block == ln.block;
                box(kPad - 2, y, kScreenW - 2 * kPad + 4, more ? ln.h : ln.th + 1, kSurface);
            }
            if (ln.bar) box(kPad + 2, y, 2, ln.h, kSurface2);
            if (ln.rule) box(kPad, ln.pieces.empty() ? y + 4 : y + ln.th + 1, kScreenW - 2 * kPad, 1, kSurface2);
            for (auto& p : ln.pieces) {
                bool is_link = p.link >= 0;
                bool focused = is_link && p.link == focus_;
                bool field = is_link && p.style == Style::Code && href_field(doc_.links[size_t(p.link)]) >= 0;
                int lh = int(lv_font_get_line_height(font_for(p.style)));
                if (field && !focused) {   // a text box: a lighter box behind it
                    int tw = metrics_.width(p.text, p.style);
                    lv_obj_t* b = box(kPad + p.x, y + ln.th - lh, tw, lh, kSurface);
                    lv_obj_set_style_border_width(b, 1, 0);
                    lv_obj_set_style_border_color(b, c(kMuted), 0);
                    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
                }
                lv_obj_t* l = lv_label_create(view_);
                lv_label_set_text(l, p.text.c_str());
                lv_obj_set_style_text_font(l, font_for(p.style), 0);
                lv_obj_set_style_text_color(
                    l, c(focused ? kOnAccent : field ? kText : is_link ? kAccent : color_for(p.style)), 0);
                if (is_link && !focused && !field) lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_UNDERLINE, 0);
                if (focused) {
                    look::focus_fill(l, 0);
                } else if (p.style == Style::Code && !field) {
                    lv_obj_set_style_bg_color(l, c(kSurface), 0);
                    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
                }
                // headings sit on the baseline of their (taller) line
                lv_obj_set_pos(l, kPad + p.x, y + ln.th - lh);
            }
            y += ln.h;
        }
        if (lines_.empty()) {
            lv_label_set_text(pos_, "");
        } else {
            size_t mt = max_top();
            lv_label_set_text(pos_, mt == 0 ? "" : fmt("%zu%%", std::min<size_t>(100, top_ * 100 / mt)).c_str());
        }
    }

    // ---- address, find, contents

    using Done = std::function<void(const std::string&)>;
    // One-line text box over the page (a Text screen while it is up).
    void ask(const char* title, const std::string& initial, Done done, const std::string& hint = "",
             bool password = false) {
        done_ = std::move(done);
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = lv_obj_create(root_);
        look::raised(input_);
        lv_obj_set_size(input_, 300, 64);
        lv_obj_align(input_, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t* t = label(input_, title, &zs_font_12, kMuted);
        lv_obj_set_width(t, 280);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_t* ta = lv_textarea_create(input_);
        look::field(ta);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_text(ta, initial.c_str());
        if (!hint.empty()) lv_textarea_set_placeholder_text(ta, hint.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_set_style_text_font(ta, &zs_font_12, 0);
        lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<BrowserApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        set_actions();
    }

    void ask_address() {
        // Empty, so typing starts a new address; the current one is the hint.
        std::string cur = history_.empty() || history_.back().url == kHomeUrl ? "" : short_url(history_.back().url);
        ask("Address, or words to search for", "", [this](const std::string& v) {
            std::string u = typed_url(v, home_dir());
            if (!u.empty()) go(u);
        }, cur);
    }

    void ask_search() {
        ask("Search the web (DuckDuckGo)", "", [this](const std::string& v) {
            std::string t = v;
            t.erase(0, t.find_first_not_of(' '));
            if (!t.empty()) go(search_url(t));
        });
    }

    void ask_find() {
        if (lines_.empty()) return;
        ask("Find on this page", find_, [this](const std::string& v) {
            bool again = v == find_;
            find_ = v;
            find(again);
        });
    }

    static std::string lower(std::string s) {
        for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
        return s;
    }

    void find(bool again) {
        if (find_.empty()) return;
        std::string q = lower(find_);
        size_t n = lines_.size();
        size_t from = again ? top_ + 1 : top_;
        for (size_t k = 0; k < n; ++k) {
            size_t i = (from + k) % n;
            std::string t;
            for (auto& p : lines_[i].pieces) t += p.text;
            if (lower(t).find(q) != std::string::npos) {
                if (from + k >= n) shell_toast("From the top");
                top_ = std::min(i, max_top());
                focus_ = -1;
                render();
                return;
            }
        }
        shell_toast("\"" + find_ + "\" isn't on this page");
    }

    void open_contents() {
        std::vector<size_t> heads;
        for (size_t i = 0; i < doc_.blocks.size(); ++i)
            if (doc_.blocks[i].kind == Block::Heading && doc_.blocks[i].level <= 3) heads.push_back(i);
        if (heads.empty()) {
            shell_toast("No headings on this page");
            return;
        }
        std::vector<std::string> rows;
        int focus = 0;
        size_t here = lines_.empty() ? 0 : lines_[top_].block;
        for (size_t b : heads) {
            const Block& blk = doc_.blocks[b];
            std::string t(size_t(blk.level - 1) * 3, ' ');
            for (auto& r : blk.runs) t += r.text;
            if (b <= here) focus = int(rows.size());
            rows.push_back(t);
        }
        open_picker(rows, focus, "No headings", [this, heads](int i) { contents_pick(heads[size_t(i)]); });
    }

    // A List screen over the page (contents, history, a form's choices):
    // typing narrows it, Enter picks a row.
    void open_picker(const std::vector<std::string>& rows, int focus, const char* empty,
                     std::function<void(int)> pick) {
        shell_set_actions({{"Choose", 0, "Enter", nullptr}, {"Close", 0, "Esc", nullptr}});
        picker_ = lv_obj_create(root_);
        style_panel(picker_);
        lv_obj_set_size(picker_, kScreenW - 8, kBodyH - 4);
        lv_obj_set_pos(picker_, 4, 2);
        lv_obj_set_style_pad_all(picker_, 2, 0);
        lv_obj_remove_flag(picker_, LV_OBJ_FLAG_SCROLLABLE);
        picker_list_.build(picker_, 0, 0, kScreenW - 12, kBodyH - 8);
        picker_list_.set_empty_text(empty);
        for (size_t i = 0; i < rows.size(); ++i)
            picker_list_.add(nullptr, rows[i], "", [this, pick, i] {
                close_picker();   // ListView runs this after the key, so the list may go
                pick(int(i));
            });
        picker_list_.finish();
        picker_list_.focus(focus);
    }

    void close_picker() {
        if (!picker_) return;
        lv_obj_t* p = picker_;
        picker_ = nullptr;
        lv_group_remove_all_objs(group);
        picker_list_.reset();
        lv_obj_delete_async(p);
        set_actions();
    }

    void contents_pick(size_t block) {
        for (size_t i = 0; i < lines_.size(); ++i)
            if (lines_[i].block >= block) {
                top_ = std::min(i, max_top());
                break;
            }
        if (!history_.empty()) history_.back().top = top_;
        focus_ = -1;
        render();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* pos_ = nullptr;
    lv_obj_t* view_ = nullptr;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* picker_ = nullptr;
    ListView picker_list_;
    Done done_;

    Metrics metrics_;
    Doc doc_;
    std::vector<Line> lines_;
    std::vector<int> link_line_;   // first line of each link
    std::string page_url_, title_text_, loading_url_, pending_frag_, find_;
    std::vector<Visit> history_;   // this session's Back steps
    Bookmarks bookmarks_, visited_;
    size_t top_ = 0, restore_top_ = 0;
    int focus_ = -1;
    int dots_ = 0;

    std::shared_ptr<Job> job_;
    std::thread worker_;

    bool pictures_on_ = true;
    std::map<int, std::unique_ptr<Shown>> shown_;   // Doc::images index -> picture
    std::shared_ptr<PictureJob> pictures_;
    std::thread picture_worker_;
};
}  // namespace

std::unique_ptr<App> make_browser_app() { return std::make_unique<BrowserApp>(); }

}  // namespace zs
