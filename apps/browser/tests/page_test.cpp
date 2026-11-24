// The Browser's page engine without a screen: Markdown, HTML, layout and
// addresses. cmake ... && ctest, or run browser_test.
#include <cstdio>
#include <string>

#include "apps/browser/page.h"
#include "apps/browser/picture.h"

using namespace zs::web;

static int failures = 0;
static std::string show(const std::string& s) { return s; }
static std::string show(const char* s) { return s; }
template <class T>
static std::string show(T v) {
    return std::to_string(v);
}
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        auto _a = (a);                                                                                   \
        auto _b = (b);                                                                                   \
        if (!(_a == _b)) {                                                                               \
            std::printf("FAIL %s:%d  %s\n   got: %s\n  want: %s\n", __FILE__, __LINE__, #a,              \
                        show(_a).c_str(), show(_b).c_str());                               \
            ++failures;                                                                                  \
        }                                                                                                \
    } while (0)

static std::string text_of(const Block& b) {
    std::string s;
    for (auto& r : b.runs) s += r.text;
    return s;
}

// A w x h 24-bit BMP, every pixel (r, g, b).
static std::string bmp(int w, int h, int r, int g, int b) {
    int row = (w * 3 + 3) & ~3;
    int size = 54 + row * h;
    std::string s(size_t(size), '\0');
    auto put = [&](int at, int v, int n) {
        for (int i = 0; i < n; ++i) s[size_t(at + i)] = char((v >> (8 * i)) & 0xFF);
    };
    s[0] = 'B';
    s[1] = 'M';
    put(2, size, 4);
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, w, 4);
    put(22, h, 4);
    put(26, 1, 2);
    put(28, 24, 2);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t p = size_t(54 + y * row + x * 3);
            s[p] = char(b);
            s[p + 1] = char(g);
            s[p + 2] = char(r);
        }
    return s;
}

static std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t v = uint32_t((unsigned char)in[i]) << 16;
        if (i + 1 < in.size()) v |= uint32_t((unsigned char)in[i + 1]) << 8;
        if (i + 2 < in.size()) v |= uint32_t((unsigned char)in[i + 2]);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return out;
}

// Text with no-break spaces (form controls use them) as plain spaces.
static std::string shown(const Block& b) {
    std::string s = text_of(b), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, "\xC2\xA0") == 0) {
            out += ' ';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Every character 6 px wide, every line 14 px high.
static Metrics mono() {
    Metrics m;
    m.width = [](const std::string& s, Style) {
        int n = 0;
        for (unsigned char ch : s)
            if ((ch & 0xC0) != 0x80) ++n;
        return n * 6;
    };
    m.height = [](Style st) { return st == Style::H1 ? 20 : 14; };
    return m;
}

int main() {
    // ---- Markdown blocks
    {
        Doc d = parse_markdown(
            "# ZeroOS\n"
            "\n"
            "Some **bold** and `code`, a [link](docs/KEYS.md) and https://example.com.\n"
            "continued line.\n"
            "\n"
            "## How it works\n"
            "\n"
            "- one\n"
            "- two\n"
            "  - nested\n"
            "1. first\n"
            "\n"
            "> quoted\n"
            "\n"
            "```sh\n"
            "  indented code\n"
            "```\n"
            "\n"
            "| Key | What |\n"
            "| --- | --- |\n"
            "| Esc | Back \\| home |\n"
            "\n"
            "---\n"
            "<!-- hidden -->\n"
            "![logo](x.png) [ref link][r]\n"
            "\n"
            "[r]: https://ref.example/\n");
        CHECK_EQ(d.title, "ZeroOS");
        CHECK(d.blocks.size() >= 12);
        CHECK(d.blocks[0].kind == Block::Heading && d.blocks[0].level == 1);
        const Block& p = d.blocks[1];
        CHECK(p.kind == Block::Para);
        CHECK_EQ(text_of(p), "Some bold and code, a link and https://example.com. continued line.");
        bool strong = false, code = false;
        for (auto& r : p.runs) {
            if (r.text == "bold") strong = r.style == Style::Strong;
            if (r.text == "code") code = r.style == Style::Code;
        }
        CHECK(strong);
        CHECK(code);
        CHECK_EQ(d.links.at(0), "docs/KEYS.md");
        CHECK_EQ(d.links.at(1), "https://example.com");
        CHECK(d.anchors.count("how-it-works"));
        CHECK(d.blocks[3].kind == Block::Item && d.blocks[3].level == 0);
        CHECK(d.blocks[5].kind == Block::Item && d.blocks[5].level == 1);
        CHECK_EQ(text_of(d.blocks[6]), "1. first");
        CHECK(d.blocks[7].kind == Block::Quote);
        CHECK(d.blocks[8].kind == Block::Pre && d.blocks[8].code);
        CHECK_EQ(text_of(d.blocks[8]), "  indented code");
        CHECK(d.blocks[9].kind == Block::Row && d.blocks[9].level == 1);
        CHECK_EQ(text_of(d.blocks[10]), "Esc  \xC2\xB7  Back | home");
        CHECK(d.blocks[11].kind == Block::Rule);
        Doc t = parse_markdown("| | State |\n| --- | --- |\n| a | b |\n");
        CHECK_EQ(text_of(t.blocks.at(0)), "State");
        // a picture is a block of its own; its alt text is kept
        CHECK(d.blocks[12].kind == Block::Image && d.blocks[12].image == 0);
        CHECK_EQ(text_of(d.blocks[12]), "logo");
        CHECK_EQ(d.images.at(0).src, "x.png");
        CHECK_EQ(text_of(d.blocks[13]), "ref link");
        CHECK_EQ(d.links.back(), "https://ref.example/");
    }
    // snake_case isn't emphasis, a lone * isn't either, unicode folds to the font
    {
        Doc d = parse_markdown("run zeroos_apps_update now, 2 * 3 \xE2\x86\x92 6 \xF0\x9F\x98\x80\n");
        CHECK_EQ(text_of(d.blocks.at(0)), "run zeroos_apps_update now, 2 * 3 -> 6");
    }
    // setext heading, duplicate heading ids
    {
        Doc d = parse_markdown("Title\n=====\n\n## Notes\n\n## Notes\n");
        CHECK(d.blocks.at(0).kind == Block::Heading);
        CHECK_EQ(d.title, "Title");
        CHECK(d.anchors.count("notes") && d.anchors.count("notes-1"));
    }
    CHECK_EQ(slug("Status: what is tested and what isn't"), "status-what-is-tested-and-what-isnt");
    CHECK_EQ(slug("M5Stack's app store"), "m5stacks-app-store");

    // ---- HTML
    {
        Doc d = parse_html(
            "<!doctype html><html><head><title>Hi &amp; bye</title><style>p{}</style></head>"
            "<body><script>var x = '<p>no</p>';</script><h1 id=top>Hello</h1>"
            "<p>Some <b>bold</b>\n   and <a href=\"/x\">a link</a>.</p>"
            "<ul><li>one</li><li>two <code>c</code></li></ul>"
            "<pre>line 1\n  line 2</pre><table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>"
            "</body></html>");
        CHECK_EQ(d.title, "Hi & bye");
        CHECK(d.blocks.at(0).kind == Block::Heading);
        CHECK_EQ(text_of(d.blocks.at(1)), "Some bold and a link.");
        CHECK_EQ(d.links.at(0), "/x");
        CHECK(d.anchors.count("top") && d.anchors.at("top") == 0);
        CHECK(d.blocks.at(2).kind == Block::Item);
        CHECK_EQ(text_of(d.blocks.at(3)), "\xE2\x80\xA2 two c");
        CHECK(d.blocks.at(4).kind == Block::Pre);
        CHECK_EQ(text_of(d.blocks.at(4)), "line 1\n  line 2");
        CHECK(d.blocks.at(5).kind == Block::Row);
        CHECK_EQ(text_of(d.blocks.at(6)), "1  \xC2\xB7  2");
    }
    CHECK_EQ(decode_entities("&lt;a&gt; &#39;x&#x27; &copy;"), "<a> 'x' \xC2\xA9");

    // ---- layout
    {
        Doc d = parse_markdown("# T\n\nword word word word word word word word\n\n- item text that wraps around\n");
        auto lines = layout(d, 60, mono());   // 10 characters a line
        CHECK(lines.size() >= 5);
        CHECK(lines.at(0).rule);
        // "word word " fits, the third word wraps
        CHECK_EQ(lines.at(1).pieces.at(0).text, "word word");
        // wrapped list lines hang under the text, not the bullet
        size_t i = 1;
        while (i < lines.size() && lines[i].block != 2) ++i;
        CHECK(i + 1 < lines.size());
        CHECK_EQ(lines.at(i).pieces.at(0).text, "\xE2\x80\xA2 ");
        CHECK(lines.at(i + 1).pieces.at(0).x == 12);
        // nothing runs past the width
        for (auto& l : lines)
            for (auto& p : l.pieces) CHECK(p.x + mono().width(p.text, p.style) <= 60);
    }
    {
        // a word longer than the line breaks between characters
        Doc d = parse_markdown("https://example.com/a/very/long/path\n");
        auto lines = layout(d, 60, mono());
        CHECK(lines.size() >= 3);
        CHECK(lines.at(0).pieces.at(0).link == 0 && lines.at(1).pieces.at(0).link == 0);
    }

    // ---- addresses
    const std::string raw = "https://raw.githubusercontent.com/o/r/HEAD/README.md";
    CHECK_EQ(resolve_url(raw, "docs/KEYS.md#x"), "https://raw.githubusercontent.com/o/r/HEAD/docs/KEYS.md#x");
    CHECK_EQ(resolve_url("https://a.com/x/y/z.md", "../q.md"), "https://a.com/x/q.md");
    CHECK_EQ(resolve_url("https://a.com/x/y.md", "/root"), "https://a.com/root");
    CHECK_EQ(resolve_url("https://a.com/x/y.md", "#sec"), "https://a.com/x/y.md#sec");
    CHECK_EQ(resolve_url("https://a.com/x/y.md", "//b.org/p"), "https://b.org/p");
    CHECK_EQ(resolve_url("https://a.com/x", "https://c.net/"), "https://c.net/");
    CHECK_EQ(resolve_url("file:///home/pi/Notes/a.md", "b.md"), "file:///home/pi/Notes/b.md");
    CHECK_EQ(rewrite_url("https://github.com/o/r"), raw);
    CHECK_EQ(rewrite_url("https://github.com/o/r/blob/main/docs/A.md#k"),
             "https://raw.githubusercontent.com/o/r/main/docs/A.md#k");
    CHECK_EQ(rewrite_url("https://github.com/o/r/tree/main/apps/x"),
             "https://raw.githubusercontent.com/o/r/main/apps/x/README.md");
    CHECK_EQ(rewrite_url("https://github.com/o/r/releases/latest"), "https://github.com/o/r/releases/latest");
    CHECK_EQ(rewrite_url("https://example.com/"), "https://example.com/");
    CHECK_EQ(typed_url("example.com", "/home/pi"), "https://example.com");
    CHECK_EQ(typed_url("o/r", "/home/pi"), "https://github.com/o/r");
    CHECK_EQ(typed_url("~/Notes", "/home/pi"), "file:///home/pi/Notes");
    CHECK_EQ(typed_url("o/r/blob/main/A.md", "/home/pi"), "https://github.com/o/r/blob/main/A.md");
    CHECK_EQ(typed_url("example.com/a/b", "/home/pi"), "https://example.com/a/b");
    CHECK_EQ(typed_url("http://x", "/home/pi"), "http://x");
    CHECK_EQ(short_url(raw + "#a"), "github.com/o/r/README.md");
    CHECK(content_kind(raw, "text/plain; charset=utf-8", "# hi") == Kind::Markdown);
    CHECK(content_kind("https://x.org/", "text/html", "") == Kind::Html);
    CHECK(content_kind("https://x.org/LICENSE", "text/plain", "MIT") == Kind::Text);
    CHECK(content_kind("https://x.org/a", "", "<!DOCTYPE html><p>") == Kind::Html);
    CHECK(!unsupported_by_name("https://x.org/a.png"));
    CHECK(image_by_name("https://x.org/a.PNG"));
    CHECK(unsupported_by_name("https://x.org/a.svg"));
    CHECK(!unsupported_by_name("https://x.org/a.md"));

    // ---- pictures: block ones, and the ones that stay text
    {
        Doc d = parse_html(
            "<p>Before <img src=\"/logo.png\" alt=\"Logo\" width=\"272\" height=\"92\"> after</p>"
            "<p>Icon <img src=\"i.png\" width=\"16\" height=\"16\" alt=\"star\"> and "
            "<img src=\"b.svg\" alt=\"badge\"> <a href=\"/x\"><img src=\"p.jpg\"></a></p>");
        CHECK_EQ(d.images.size(), size_t(2));
        CHECK_EQ(d.images.at(0).src, "/logo.png");
        CHECK_EQ(d.images.at(0).w, 272);
        CHECK_EQ(d.images.at(1).src, "p.jpg");
        CHECK_EQ(d.images.at(1).link, 0);
        CHECK_EQ(text_of(d.blocks.at(0)), "Before");
        CHECK(d.blocks.at(1).kind == Block::Image);
        CHECK_EQ(text_of(d.blocks.at(2)), "after");
        CHECK_EQ(text_of(d.blocks.at(3)), "Icon [image: star] and [image: badge]");
        CHECK(d.blocks.at(4).kind == Block::Image && d.blocks.at(4).image == 1);

        // not loaded: alt text (or nothing); loaded: strips of kImageStrip
        Metrics m = mono();
        std::vector<Line> ls = layout(d, 300, m);
        int strips = 0;
        for (auto& l : ls) strips += l.image >= 0;
        CHECK_EQ(strips, 0);
        m.image_size = [](int i, int& w, int& h) {
            if (i != 0) return false;
            w = 272;
            h = 40;
            return true;
        };
        ls = layout(d, 300, m);
        std::vector<int> ys;
        for (auto& l : ls)
            if (l.image == 0) {
                ys.push_back(l.image_y);
                CHECK_EQ(l.image_x, 14);   // centred
            }
        CHECK(ys == std::vector<int>({0, 16, 32}));

        // a picture in Markdown, in a link
        Doc md = parse_markdown("[![Shot](shot.png)](https://x.org/)\n\n![badge](https://img.shields.io/b)\n");
        CHECK_EQ(md.images.size(), size_t(1));
        CHECK_EQ(md.images.at(0).link, 0);
        CHECK_EQ(text_of(md.blocks.at(1)), "[image: badge]");
    }
    // ---- decoding pictures
    {
        Picture p;
        CHECK(decode_picture(bmp(400, 100, 255, 0, 0), 0, 0, 308, 120, 0, p));
        CHECK_EQ(p.w, 308);   // shrunk to the width, shape kept
        CHECK_EQ(p.h, 77);
        CHECK_EQ(p.pixels.size(), size_t(308 * 77));
        CHECK_EQ(int(p.pixels[0]), 0xF800);   // red in RGB565
        CHECK(decode_picture(bmp(100, 50, 0, 0, 255), 0, 0, 308, 120, 0, p));
        CHECK_EQ(p.w, 100);   // never enlarged
        CHECK(decode_picture(bmp(544, 184, 0, 255, 0), 272, 92, 308, 120, 0, p));
        CHECK_EQ(p.w, 272);   // the size the page asks for
        CHECK_EQ(p.h, 92);
        CHECK(decode_picture(bmp(100, 400, 0, 0, 0), 0, 0, 308, 120, 0, p));
        CHECK_EQ(p.h, 120);   // tall: fits the screen's height
        CHECK_EQ(p.w, 30);
        CHECK(!decode_picture(bmp(1, 1, 0, 0, 0), 0, 0, 308, 120, 0, p));   // a tracking pixel
        CHECK(!decode_picture("<html>", 0, 0, 308, 120, 0, p));
        std::string bytes;
        CHECK(data_uri("data:image/bmp;base64," + base64(bmp(40, 20, 0, 255, 0)), bytes));
        CHECK(decode_picture(bytes, 0, 0, 308, 120, 0, p));
        CHECK_EQ(int(p.pixels[5]), 0x07E0);
        CHECK(data_uri("data:text/plain,a%20b", bytes));
        CHECK_EQ(bytes, "a b");
        CHECK(!data_uri("https://x.org/a.png", bytes));
    }
    // ---- forms
    {
        Doc d = parse_html(
            "<form action=\"/search\" name=\"f\">"
            "<input name=\"ie\" value=\"ISO-8859-1\" type=\"hidden\">"
            "<input name=\"q\" title=\"Search\" value=\"\">"
            "<input value=\"Google Search\" name=\"btnG\" type=\"submit\">"
            "<input value=\"Lucky\" name=\"btnI\" type=\"submit\">"
            "</form>"
            "<form method=\"post\" action=\"login\">"
            "<input name=\"user\"><input type=\"password\" name=\"pw\">"
            "<label><input type=\"checkbox\" name=\"keep\"> Remember me</label>"
            "<input type=\"radio\" name=\"s\" value=\"a\" checked><input type=\"radio\" name=\"s\" value=\"b\">"
            "<select name=\"lang\"><option value=\"en\">English<option value=\"fr\" selected>Fran&ccedil;ais</select>"
            "<textarea name=\"note\">hi there</textarea>"
            "<button>Log in</button>"
            "</form>");
        CHECK_EQ(d.forms.size(), size_t(2));
        CHECK_EQ(d.fields.size(), size_t(12));
        CHECK_EQ(text_fields(d, 0), 1);
        CHECK_EQ(text_fields(d, 1), 3);
        int q = 1;
        CHECK_EQ(href_field(d.links.at(size_t(d.fields[1].link))), q);
        CHECK_EQ(shown(d.blocks.at(0)), "[              ] [ Google Search ] [ Lucky ]");
        d.fields[1].value = "zero os";
        update_field(d, 1);
        CHECK_EQ(shown(d.blocks.at(0)), "[zero os       ] [ Google Search ] [ Lucky ]");
        Submission s = submit_form(d, 0, -1, "https://www.google.com/?hl=en#top");
        CHECK(!s.is_post);
        CHECK_EQ(s.url, "https://www.google.com/search?ie=ISO-8859-1&q=zero+os&btnG=Google+Search");
        s = submit_form(d, 0, 3, "https://www.google.com/");
        CHECK_EQ(s.url, "https://www.google.com/search?ie=ISO-8859-1&q=zero+os&btnI=Lucky");

        d.fields[4].value = "me";
        d.fields[5].value = "a&b c";
        update_field(d, 5);
        toggle_field(d, 6);   // the checkbox
        toggle_field(d, 8);   // the second radio
        CHECK(!d.fields[7].checked);
        s = submit_form(d, 1, -1, "https://x.org/a/b");
        CHECK(s.is_post);
        CHECK_EQ(s.url, "https://x.org/a/login");
        CHECK_EQ(s.post, "user=me&pw=a%26b+c&keep=on&s=b&lang=fr&note=hi+there");
        std::string all;
        for (auto& b : d.blocks) all += shown(b) + "|";
        CHECK(all.find("[*****         ]") != std::string::npos);
        CHECK(all.find("[x] Remember me") != std::string::npos);
        CHECK(all.find("( ) (*)") != std::string::npos);
        CHECK(all.find("[Fran\xC3\xA7" "ais v]") != std::string::npos);
        CHECK(all.find("[ Log in ]") != std::string::npos);
    }
    // ---- text encoding, search
    {
        CHECK_EQ(to_utf8("caf\xE9", "text/html; charset=ISO-8859-1"), "caf\xC3\xA9");
        CHECK_EQ(to_utf8("\x93hi\x94", "text/html; charset=windows-1252"), "\xE2\x80\x9Chi\xE2\x80\x9D");
        CHECK_EQ(to_utf8("caf\xC3\xA9", "text/html"), "caf\xC3\xA9");
        CHECK_EQ(to_utf8("<meta charset=\"latin1\">caf\xE9", ""), "<meta charset=\"latin1\">caf\xC3\xA9");
        CHECK_EQ(to_utf8("bad \xE9 bytes", ""), "bad \xC3\xA9 bytes");
        CHECK_EQ(typed_url("raspberry pi", "/home/pi"), "https://lite.duckduckgo.com/lite/?q=raspberry+pi");
        CHECK_EQ(typed_url("weather", "/home/pi"), "https://lite.duckduckgo.com/lite/?q=weather");
        CHECK_EQ(typed_url("localhost", "/home/pi"), "https://localhost");
        CHECK_EQ(typed_url("localhost:8080", "/home/pi"), "https://localhost:8080");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("browser_test: all passed\n");
    return 0;
}
