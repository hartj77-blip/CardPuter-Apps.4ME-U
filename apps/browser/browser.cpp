// Browser: a text web browser, made first for reading this project's docs
// and READMEs. Markdown is shown as formatted text, HTML pages as their
// text and links, anything else as plain text. Images aren't shown yet.
//
//   Up / Down      scroll a line        Space / Backspace   page down / up
//   Left / Right   previous / next link Enter               open the link
//   Esc            back                 G                   type an address
//   C  contents (jump to a heading)     F  find on the page
//   R  reload      H  home (bookmarks)
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
#include <mutex>
#include <sstream>
#include <thread>

#include "apps/browser/page.h"
#include "apps/common.h"
#include "platform/keys.h"
#include "ui/fileio.h"

extern char** environ;

namespace zs {
namespace {
using namespace theme;
using namespace web;

constexpr const char* kHomeUrl = "about:home";
constexpr size_t kMaxBytes = 4 * 1024 * 1024;
constexpr int kPad = 6;      // left and right margin
constexpr int kTitleH = 16;  // the title row

const char* const kOs = "https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/HEAD/";

std::string home_page() {
    std::string os = kOs;
    std::string md =
        "**Left/Right** pick a link, **Enter** opens it, **Esc** goes back. **G** types an address, "
        "**C** lists a page's headings, **F** finds text. **Tab** shows everything.\n\n"
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
                               {"Dev Tools", "DEV"}, {"Dock", "DOCK"},         {"Games", "GAMES"},
                               {"GPS and LoRa", "EXPANSION"}, {"IR Remote", "IR"}, {"Media and Jellyfin", "JELLYFIN"},
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
Fetched fetch_http(const std::string& url_in, std::atomic<int>* pid_out) {
    Fetched f;
#ifdef __EMSCRIPTEN__
    (void)url_in;
    (void)pid_out;
    f.error = "The browser demo can't load pages";
    return f;
#else
    std::string url;
    for (char ch : url_in) {   // curl won't take a raw space
        if (ch == ' ') url += "%20";
        else url += ch;
    }
    const char* argv[] = {"curl", "-sS", "-L", "--compressed", "--max-time", "30", "--connect-timeout", "10",
                          "--max-filesize", "4194304", "-A", "ZeroOS-Browser/1.0",
                          "-w", "\n\x1f%{http_code}\x1f%{content_type}\x1f%{url_effective}", url.c_str(), nullptr};
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
    int rc = posix_spawnp(&pid, "curl", &fa, nullptr, const_cast<char* const*>(argv), environ);
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
Fetched load(const std::string& url, bool prefer_cache, std::atomic<int>* pid) {
    if (url.compare(0, 7, "file://") == 0) return fetch_file(url);
    Fetched cached;
    bool have = cache_get(url, cached);
    if (prefer_cache && have) return cached;
    Fetched f = fetch_http(url, pid);
    // A folder on GitHub ("apps/battery" from a README): try its README.
    if (!f.ok && f.error == "Not found (404)" && url.find("raw.githubusercontent.com/") != std::string::npos) {
        std::string last = url.substr(url.rfind('/') + 1);
        if (last.find('.') == std::string::npos) {
            std::string alt = url + (url.back() == '/' ? "" : "/") + "README.md";
            Fetched g = fetch_http(alt, pid);
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

struct Job {
    std::mutex m;
    bool done = false;
    Fetched result;
    std::atomic<int> pid{0};
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
        case Style::Strong: return 0xffffff;
        case Style::Code: return kInfo;
        case Style::Muted:
        case Style::Quote: return kMuted;
        case Style::H3: return kAccent;
        case Style::H1:
        case Style::H2: return 0xffffff;
        default: return 0xdedede;
    }
}

class BrowserApp : public App {
public:
    ~BrowserApp() override {
        lv_async_call_cancel(contents_pick_cb, this);
        stop();
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

        set_actions();
        std::string arg = shell_app_arg();
        if (!arg.empty() && arg[0] == '/') arg = "file://" + arg;
        go(arg.empty() ? kHomeUrl : arg);
    }

    void tick() override {
        if (!job_) return;
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
        if (input_ || contents_) return false;   // typing, or the contents list
        switch (key) {
            case LV_KEY_DOWN: case 'j': scroll(1); return true;
            case LV_KEY_UP: case 'k': scroll(-1); return true;
            case ' ': case kKeyPageDown: page(1); return true;
            case LV_KEY_BACKSPACE: case kKeyPageUp: page(-1); return true;
            case LV_KEY_HOME: top(); return true;
            case LV_KEY_END: bottom(); return true;
            case LV_KEY_RIGHT: next_link(1); return true;
            case LV_KEY_LEFT: next_link(-1); return true;
            case LV_KEY_ENTER: case '\r': follow(); return true;
            case 'g': case 'G': ask_address(); return true;
            case 'c': case 'C': open_contents(); return true;
            case 'f': case 'F': ask_find(); return true;
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
        if (contents_) {
            close_contents();
            return true;
        }
        if (job_) {   // stop loading
            stop();
            show_message(LV_SYMBOL_CLOSE " Stopped");
            return true;
        }
        if (history_.size() > 1) {
            history_.pop_back();
            open(history_.back().url, true, history_.back().top);
            return true;
        }
        return false;
    }

private:
    struct Visit {
        std::string url;   // with #fragment
        size_t top = 0;
    };

    int view_h() const { return kBodyH - kTitleH; }

    void set_actions() {
        std::vector<Action> a;
        a.push_back({"Open link", 0, "Enter", nullptr});
        a.push_back({"Page down", 0, "Space", nullptr});
        a.push_back({"Go to address", 'g', "G", [this] { ask_address(); }});
        a.push_back({"Links", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
        a.push_back({"Contents", 'c', "C", [this] { open_contents(); }});
        a.push_back({"Find on page", 'f', "F", [this] { ask_find(); }});
        a.push_back({"Reload", 'r', "R", [this] { reload(); }});
        a.push_back({"Home", 'h', "H", [this] { go(kHomeUrl); }});
        a.push_back({"Back", 0, "Esc", nullptr});
        a.push_back({"Page up", 0, "Bksp", nullptr});
        shell_set_actions(std::move(a));
    }

    // ---- navigation

    // Open a link or typed address as a new step in the history.
    void go(const std::string& url_in) {
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
        if (!job_ && frag_url == page_url_ && !frag.empty()) {
            history_.push_back({url, top_});
            jump_to(frag);
            return;
        }
        history_.push_back({url, 0});
        open(url, false, 0);
    }

    // Show a page: from the history (back) or fresh.
    void open(const std::string& url_with_frag, bool from_history, size_t restore_top) {
        stop();
        std::string url = url_with_frag;
        pending_frag_ = split_fragment(url);
        restore_top_ = from_history ? restore_top : 0;
        if (url == kHomeUrl) {
            Fetched f;
            f.ok = true;
            f.url = kHomeUrl;
            f.content_type = "text/markdown";
            f.body = home_page();
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
        auto run = [job, url, cache] {
            Fetched f = load(url, cache, &job->pid);
            std::lock_guard<std::mutex> l(job->m);
            job->result = std::move(f);
            job->done = true;
        };
#ifdef __EMSCRIPTEN__
        run();
#else
        worker_ = std::thread(run);
#endif
    }

    void reload() {
        if (history_.empty()) return;
        history_.back().top = top_;
        open(history_.back().url, false, top_);
        restore_top_ = top_;
    }

    void stop() {
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
        if (k == Kind::Unsupported) {
            show_message(LV_SYMBOL_WARNING " Can't show this kind of file yet", f.url);
            return;
        }
        doc_ = k == Kind::Markdown ? parse_markdown(f.body) : k == Kind::Html ? parse_html(f.body) : parse_text(f.body);
        page_url_ = f.url;
        split_fragment(page_url_);
        // Relative links resolve against the address after redirects.
        if (!history_.empty() && f.url != kHomeUrl && !f.from_cache) {
            std::string frag = split_fragment(history_.back().url);
            history_.back().url = page_url_ + (frag.empty() ? "" : "#" + frag);
        }
        std::string name = page_url_.substr(page_url_.find_last_of('/') + 1);
        title_text_ = f.url == kHomeUrl ? std::string(LV_SYMBOL_HOME " Home")
                      : !doc_.title.empty() ? doc_.title : !name.empty() ? name : short_url(page_url_);
        lines_ = layout(doc_, kScreenW - 2 * kPad, metrics_);
        link_line_.assign(doc_.links.size(), -1);
        for (size_t i = 0; i < lines_.size(); ++i)
            for (auto& p : lines_[i].pieces)
                if (p.link >= 0 && link_line_[size_t(p.link)] < 0) link_line_[size_t(p.link)] = int(i);
        focus_ = -1;
        top_ = std::min(restore_top_, lines_.empty() ? 0 : lines_.size() - 1);
        if (!pending_frag_.empty()) jump_to(pending_frag_, false);
        render();
        if (f.from_cache && !f.error.empty()) shell_toast("Offline: showing a saved copy");
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
            shell_toast("Pick a link with Left / Right first");
            return;
        }
        std::string href = doc_.links[size_t(focus_)];
        go(resolve_url(page_url_ == kHomeUrl ? std::string("https://") : page_url_, href));
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
            lv_obj_set_pos(u, kPad, 30);
            lv_obj_set_width(u, kScreenW - 2 * kPad);
            lv_label_set_long_mode(u, LV_LABEL_LONG_WRAP);
            lv_obj_t* h = label(view_, "R tries again, Esc goes back.", &zs_font_12, kMuted);
            lv_obj_set_pos(h, kPad, 64);
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
            if (ln.shade) {
                bool more = i + 1 < lines_.size() && lines_[i + 1].shade && lines_[i + 1].block == ln.block;
                box(kPad - 2, y, kScreenW - 2 * kPad + 4, more ? ln.h : ln.th + 1, kSurface);
            }
            if (ln.bar) box(kPad + 2, y, 2, ln.h, kSurface2);
            if (ln.rule) box(kPad, ln.pieces.empty() ? y + 4 : y + ln.th + 1, kScreenW - 2 * kPad, 1, kSurface2);
            for (auto& p : ln.pieces) {
                bool is_link = p.link >= 0;
                bool focused = is_link && p.link == focus_;
                lv_obj_t* l = lv_label_create(view_);
                lv_label_set_text(l, p.text.c_str());
                lv_obj_set_style_text_font(l, font_for(p.style), 0);
                lv_obj_set_style_text_color(l, c(focused ? kOnAccent : is_link ? kAccent : color_for(p.style)), 0);
                if (is_link && !focused) lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_UNDERLINE, 0);
                if (focused || p.style == Style::Code) {
                    lv_obj_set_style_bg_color(l, c(focused ? kAccent : kSurface), 0);
                    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(l, 2, 0);
                }
                // headings sit on the baseline of their (taller) line
                lv_obj_set_pos(l, kPad + p.x, y + ln.th - int(lv_font_get_line_height(font_for(p.style))));
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
    // One-line text box over the page. Enter accepts, Esc cancels.
    void ask(const char* title, const std::string& initial, Done done, const std::string& hint = "") {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 64);
        lv_obj_align(input_, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
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
    }

    void ask_address() {
        // Empty, so typing starts a new address; the current one is the hint.
        std::string cur = history_.empty() || history_.back().url == kHomeUrl ? "" : short_url(history_.back().url);
        ask("Address: example.com, owner/repo or ~/file", "", [this](const std::string& v) {
            std::string u = typed_url(v, home_dir());
            if (!u.empty()) go(u);
        }, cur);
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
        contents_ = lv_obj_create(root_);
        style_panel(contents_);
        lv_obj_set_size(contents_, kScreenW - 8, kBodyH - 4);
        lv_obj_set_pos(contents_, 4, 2);
        lv_obj_set_style_pad_all(contents_, 2, 0);
        lv_obj_remove_flag(contents_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* list = make_list(contents_, 0, 0, kScreenW - 12, kBodyH - 8);
        lv_obj_t* focus = nullptr;
        size_t here = lines_.empty() ? 0 : lines_[top_].block;
        for (size_t b : heads) {
            const Block& blk = doc_.blocks[b];
            std::string t(size_t(blk.level - 1) * 3, ' ');
            for (auto& r : blk.runs) t += r.text;
            lv_obj_t* btn = list_button(list, nullptr, t.c_str(), on_contents_pick, this);
            lv_obj_set_user_data(btn, (void*)(uintptr_t)b);
            if (b <= here) focus = btn;
        }
        finish_list(list);
        if (focus) {
            lv_group_focus_obj(focus);
            lv_obj_scroll_to_view(focus, LV_ANIM_OFF);
        }
    }

    void close_contents() {
        if (!contents_) return;
        lv_obj_t* c = contents_;
        contents_ = nullptr;
        lv_group_remove_all_objs(group);
        lv_obj_delete_async(c);
    }

    static void on_contents_pick(lv_event_t* e) {
        auto* self = static_cast<BrowserApp*>(lv_event_get_user_data(e));
        self->pick_ = size_t((uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        lv_async_call(contents_pick_cb, self);   // the list is deleted: not from its own event
    }

    static void contents_pick_cb(void* p) {
        auto* self = static_cast<BrowserApp*>(p);
        self->close_contents();
        for (size_t i = 0; i < self->lines_.size(); ++i)
            if (self->lines_[i].block >= self->pick_) {
                self->top_ = std::min(i, self->max_top());
                break;
            }
        if (!self->history_.empty()) self->history_.back().top = self->top_;
        self->focus_ = -1;
        self->render();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* pos_ = nullptr;
    lv_obj_t* view_ = nullptr;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* contents_ = nullptr;
    Done done_;

    Metrics metrics_;
    Doc doc_;
    std::vector<Line> lines_;
    std::vector<int> link_line_;   // first line of each link
    std::string page_url_, title_text_, loading_url_, pending_frag_, find_;
    std::vector<Visit> history_;
    size_t top_ = 0, restore_top_ = 0, pick_ = 0;
    int focus_ = -1;
    int dots_ = 0;

    std::shared_ptr<Job> job_;
    std::thread worker_;
};
}  // namespace

std::unique_ptr<App> make_browser_app() { return std::make_unique<BrowserApp>(); }

}  // namespace zs
