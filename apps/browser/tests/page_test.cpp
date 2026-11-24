// The Browser's page engine without a screen: Markdown, HTML, layout and
// addresses. cmake ... && ctest, or run browser_test.
#include <cstdio>
#include <string>

#include "apps/browser/page.h"

using namespace zs::web;

static int failures = 0;
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
                        std::string(_a).c_str(), std::string(_b).c_str());                               \
            ++failures;                                                                                  \
        }                                                                                                \
    } while (0)

static std::string text_of(const Block& b) {
    std::string s;
    for (auto& r : b.runs) s += r.text;
    return s;
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
        CHECK_EQ(text_of(d.blocks[12]), "[image: logo] ref link");
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
    CHECK(unsupported_by_name("https://x.org/a.png"));
    CHECK(!unsupported_by_name("https://x.org/a.md"));

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("browser_test: all passed\n");
    return 0;
}
