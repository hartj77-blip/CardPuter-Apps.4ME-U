// The Browser app's page engine, with no LVGL in it so it is unit tested on
// any machine (tests/page_test.cpp).
//
//   text  --parse_markdown / parse_html / parse_text-->  Doc (blocks of styled runs)
//   Doc   --layout(width, metrics)-->                    Lines (pieces at x positions)
//
// The screen only ever draws the handful of lines that are visible, so a
// long README costs a few dozen LVGL labels, not thousands. (LVGL now uses
// malloc in ZeroShell, but up to ZeroOS 0.4.0 it had a fixed 512 KB pool.)
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
    int link = -1;    // index into Doc::links, or -1
    int image = -1;   // while parsing: this run is a picture (index into Doc::images); text is its alt
};

struct Block {
    enum Kind : uint8_t { Para, Heading, Item, Pre, Quote, Row, Rule, Image };
    Kind kind = Para;
    int level = 0;    // heading level 1..6, list depth 0.., Row: 1 for a header row
    bool code = false;   // Pre: code (shaded) rather than plain text
    int image = -1;      // Image: index into Doc::images
    std::vector<Run> runs;   // Pre: one run, lines separated by '\n'. Image: its alt text
};

// A picture on the page: <img> or ![alt](src). Small ones (icons, badges,
// SVG) aren't pictures here: they stay their alt text. The screen fetches
// and decodes the rest and tells layout() their size (Metrics::image_size).
struct Image {
    std::string src;    // as written (resolve with resolve_url); may be a data: URI
    std::string alt;
    int w = 0, h = 0;   // from width= / height=, 0 if not given
    int link = -1;      // the link it is inside, or -1
};

// A form control. It is on the page as a link whose href is
// field_href(index), so it is focused and chosen like any link.
struct Field {
    enum Type : uint8_t { Text, Password, Hidden, Checkbox, Radio, Submit, Select, TextArea };
    Type type = Text;
    std::string name, value;   // Select: value is unused (see options/selected)
    std::string label;         // Submit: its text; Text: the placeholder
    bool checked = false;      // Checkbox, Radio
    int form = -1;             // index into Doc::forms, or -1
    int link = -1;             // its index in Doc::links (-1 for Hidden)
    bool shown = false;        // its run is field_text() (not a <button>'s own text)
    std::vector<std::pair<std::string, std::string>> options;   // Select: (label, value)
    int selected = 0;
};

struct Form {
    std::string action;   // as written; empty: the page itself
    bool post = false;
};

struct Doc {
    std::string title;
    std::vector<Block> blocks;
    std::vector<std::string> links;            // hrefs as written (resolve with resolve_url)
    std::map<std::string, size_t> anchors;     // "#install" target -> block index
    std::vector<Image> images;
    std::vector<Field> fields;
    std::vector<Form> forms;
};

Doc parse_markdown(const std::string& text);
Doc parse_html(const std::string& html);
Doc parse_text(const std::string& text);

// ---- forms --------------------------------------------------------------

// The href a form control has in Doc::links, and back (-1: not a control).
std::string field_href(int field);
int href_field(const std::string& href);
// How a control looks on the page: "[text     ]", "[x]", "(*)", "[ Search ]".
std::string field_text(const Field& f);
// After a field's value changed: show its new text.
void update_field(Doc& doc, int field);
// Check a radio button (and uncheck the others in its group), or toggle a checkbox.
void toggle_field(Doc& doc, int field);
// The text boxes (Text, Password, TextArea) in a form.
int text_fields(const Doc& doc, int form);

struct Submission {
    std::string url;    // to load: for GET with the query in it
    std::string post;   // POST: the body (application/x-www-form-urlencoded)
    bool is_post = false;
};
// Submit `form` from the page at page_url. submitter: the button pressed,
// or -1 (Enter in a text box), which sends the form's first button.
Submission submit_form(const Doc& doc, int form, int submitter, const std::string& page_url);
// application/x-www-form-urlencoded: "a b&c" -> "a+b%26c".
std::string form_encode(const std::string& s);

// ---- text encoding ----------------------------------------------------------

// The page's bytes as UTF-8. charset= from Content-Type or a <meta> tag;
// Latin-1 and Windows-1252 (and bytes that aren't valid UTF-8) are converted.
std::string to_utf8(const std::string& body, const std::string& content_type);

// GitHub's heading ids: "How it works?" -> "how-it-works".
std::string slug(const std::string& heading);
// &amp; &lt; &#39; &#x2014; ... -> UTF-8.
std::string decode_entities(const std::string& s);

// ---- layout -------------------------------------------------------------

struct Metrics {
    std::function<int(const std::string&, Style)> width;   // pixels
    std::function<int(Style)> height;                       // line height, pixels
    // The size a loaded picture is drawn at (already fitted to the screen), or
    // false while it isn't loaded (it then shows as its alt text, if it has one).
    std::function<bool(int image, int& w, int& h)> image_size;
};

// Pictures are drawn in strips this high, so scrolling moves through a tall
// picture a line at a time like text.
constexpr int kImageStrip = 16;

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
    int image = -1;       // a strip of this picture (Doc::images)...
    int image_y = 0;      // ...starting this far down it...
    int image_x = 0;      // ...drawn this far in
    int image_w = 0, image_h = 0;   // the whole picture's size on screen
    int link = -1;        // the picture's link
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

// A search for what was typed (DuckDuckGo Lite: made for browsers without JavaScript).
std::string search_url(const std::string& query);
// Typed text that is a search, not an address ("cheap flights", "weather").
bool looks_like_search(const std::string& typed);

enum class Kind { Markdown, Html, Text, Unsupported };
// From the address and the server's Content-Type (may be empty) and body.
Kind content_kind(const std::string& url, const std::string& content_type, const std::string& body);
// Archives, packages, media: not something this browser can show.
bool unsupported_by_name(const std::string& url);
// A picture the browser can decode: PNG, JPEG, GIF (first frame), BMP.
bool image_by_name(const std::string& url);

}  // namespace zs::web
