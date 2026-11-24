// The Browser app's page engine, with no LVGL in it so it is unit tested on
// any machine (tests/page_test.cpp).
//
//   text  --parse_markdown / parse_html / parse_text-->  Doc (blocks of styled runs)
//   Doc   --layout(width, metrics)-->                    Lines (pieces at x positions)
//
// The screen only ever draws the handful of lines that are visible, so a
// long README costs a few dozen LVGL labels, not thousands: LVGL's memory
// pool is fixed at 512 KB for the whole UI.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace zs::web {

// How a piece of text looks. The screen maps each to a font and colour.
enum class Style : uint8_t {
    Body,
    Strong,    // **bold**: there is no bold font, so it is brighter
    Code,      // `inline code`
    Pre,       // a line of a code block
    Muted,     // [image: ...], table headers, list bullets
    Quote,     // > blockquote
    H1,
    H2,
    H3,        // h3 and smaller
};

struct Run {
    std::string text;
    Style style = Style::Body;
    int link = -1;   // index into Doc::links, or -1
};

struct Block {
    enum Kind : uint8_t { Para, Heading, Item, Pre, Quote, Row, Rule };
    Kind kind = Para;
    int level = 0;    // heading level 1..6, list depth 0.., Row: 1 for a header row
    bool code = false;   // Pre: code (shaded) rather than plain text
    std::vector<Run> runs;   // Pre: one run, lines separated by '\n'
};

struct Doc {
    std::string title;
    std::vector<Block> blocks;
    std::vector<std::string> links;            // hrefs as written (resolve with resolve_url)
    std::map<std::string, size_t> anchors;     // "#install" target -> block index
};

Doc parse_markdown(const std::string& text);
Doc parse_html(const std::string& html);
Doc parse_text(const std::string& text);

// GitHub's heading ids: "How it works?" -> "how-it-works".
std::string slug(const std::string& heading);
// &amp; &lt; &#39; &#x2014; ... -> UTF-8.
std::string decode_entities(const std::string& s);

// ---- layout -------------------------------------------------------------

struct Metrics {
    std::function<int(const std::string&, Style)> width;   // pixels
    std::function<int(Style)> height;                       // line height, pixels
};

struct Piece {
    std::string text;
    Style style;
    int link;
    int x;
};

struct Line {
    std::vector<Piece> pieces;
    int h = 0;          // height including the gap below it
    int th = 0;         // height of the text itself
    size_t block = 0;   // the block it came from
    bool shade = false;   // code block background
    bool bar = false;     // quote bar on the left
    bool rule = false;    // a horizontal line (a rule, or under an h1/h2)
};

std::vector<Line> layout(const Doc& doc, int width, const Metrics& m);

// ---- addresses ----------------------------------------------------------

// A link resolved against the page it is on: absolute URLs as they are,
// "/x", "../x", "x", "#frag" and "//host/x" relative to base.
std::string resolve_url(const std::string& base, const std::string& ref);
// What to fetch for a URL: GitHub pages become the raw file behind them
// (github.com/o/r -> its README, .../blob/b/path -> the file), so docs come
// as Markdown, not 300 KB of GitHub's HTML. Fragment kept.
std::string rewrite_url(const std::string& url);
// What was typed into the address box: "example.com" -> https://...,
// "owner/repo" -> that GitHub repository, "/path" or "~/x" -> a local file.
std::string typed_url(const std::string& typed, const std::string& home);
// Split off "#fragment" (returned without '#').
std::string split_fragment(std::string& url);
// The address without scheme, for showing ("github.com/o/r/README.md").
std::string short_url(const std::string& url);

enum class Kind { Markdown, Html, Text, Unsupported };
// From the address and the server's Content-Type (may be empty) and body.
Kind content_kind(const std::string& url, const std::string& content_type, const std::string& body);
// Images, archives, packages: not something a text browser can show.
bool unsupported_by_name(const std::string& url);

}  // namespace zs::web
