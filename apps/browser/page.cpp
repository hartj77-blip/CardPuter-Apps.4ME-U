// The Browser's page engine: Markdown, HTML and plain text into one block
// model, and that model into lines that fit the screen. See page.h.
#include "apps/browser/page.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace zs::web {

namespace {

// ---- small string helpers --------------------------------------------------

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool blank(const std::string& s) { return s.find_first_not_of(" \t\r\n") == std::string::npos; }

int indent_of(const std::string& s) {
    int n = 0;
    for (char ch : s) {
        if (ch == ' ') ++n;
        else if (ch == '\t') n += 4;
        else break;
    }
    return n;
}

std::string expand_tabs(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '\t') out.append(4 - out.size() % 4, ' ');
        else if (ch != '\r') out += ch;
    }
    return out;
}

void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// Next code point at s[i]; advances i. Bad bytes come back as U+FFFD.
uint32_t next_cp(const std::string& s, size_t& i) {
    uint8_t c0 = uint8_t(s[i++]);
    if (c0 < 0x80) return c0;
    int n = (c0 >> 5) == 0x6 ? 1 : (c0 >> 4) == 0xE ? 2 : (c0 >> 3) == 0x1E ? 3 : -1;
    if (n < 0) return 0xFFFD;
    uint32_t cp = c0 & (0x3F >> n);
    for (int k = 0; k < n; ++k) {
        if (i >= s.size() || (uint8_t(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
    }
    return cp;
}

// The UI font has ASCII, Latin-1 and a little punctuation (shell's
// tools/fonts.sh). Anything else would draw as nothing, so arrows and box
// drawing become ASCII and emoji go.
std::string fold(const std::string& s) {
    bool plain = true;
    for (char ch : s)
        if (uint8_t(ch) >= 0x80) plain = false;
    if (plain) return s;
    std::string out;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = next_cp(s, i);
        if (cp < 0x100 || cp == 0x2013 || cp == 0x2014 || cp == 0x2018 || cp == 0x2019 || cp == 0x201C ||
            cp == 0x201D || cp == 0x2022 || cp == 0x2026) {
            put_utf8(out, cp);
            continue;
        }
        const char* r = nullptr;
        switch (cp) {
            case 0x2190: r = "<-"; break;
            case 0x2192: r = "->"; break;
            case 0x2191: r = "^"; break;
            case 0x2193: r = "v"; break;
            case 0x2194: r = "<->"; break;
            case 0x21D2: r = "=>"; break;
            case 0x23CE: case 0x21B5: r = "Enter"; break;
            case 0x21E5: r = "Tab"; break;
            case 0x232B: r = "Bksp"; break;
            case 0x2264: r = "<="; break;
            case 0x2265: r = ">="; break;
            case 0x2260: r = "!="; break;
            case 0x2248: r = "~"; break;
            case 0x2212: r = "-"; break;
            case 0x2010: case 0x2011: case 0x2012: case 0x2015: r = "-"; break;
            case 0x2032: r = "'"; break;
            case 0x2033: r = "\""; break;
            case 0x2122: r = "(TM)"; break;
            case 0x2713: case 0x2714: case 0x2705: r = "[x]"; break;
            case 0x2717: case 0x2718: case 0x274C: r = "[ ]"; break;
            case 0x2423: r = "Space"; break;
            case 0x2009: case 0x200A: case 0x2002: case 0x2003: case 0x202F: r = " "; break;
            default: break;
        }
        if (r) {
            out += r;
        } else if (cp >= 0x2500 && cp <= 0x257F) {   // box drawing
            static const uint32_t horiz[] = {0x2500, 0x2501, 0x2504, 0x2505, 0x2508, 0x2509, 0x254C, 0x254D, 0x2550};
            static const uint32_t vert[] = {0x2502, 0x2503, 0x2506, 0x2507, 0x250A, 0x250B, 0x254E, 0x254F, 0x2551};
            if (std::find(std::begin(horiz), std::end(horiz), cp) != std::end(horiz)) out += '-';
            else if (std::find(std::begin(vert), std::end(vert), cp) != std::end(vert)) out += '|';
            else out += '+';
        } else if (cp >= 0x2580 && cp <= 0x259F) {   // blocks
            out += '#';
        } else if (cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
                   cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) {
            // emoji, symbols, variation selectors, zero-width: drop
        } else {
            out += '?';
        }
    }
    return out;
}

void trim_runs(std::vector<Run>& runs);

void fold_doc(Doc& d) {
    d.title = fold(d.title);
    for (auto& b : d.blocks) {
        for (auto& r : b.runs) r.text = fold(r.text);
        if (b.kind != Block::Pre) trim_runs(b.runs);   // a dropped emoji can leave a space
    }
}

// ---- building runs -----------------------------------------------------------

void emit(std::vector<Run>& out, const std::string& text, Style style, int link) {
    if (text.empty()) return;
    if (!out.empty() && out.back().style == style && out.back().link == link) {
        out.back().text += text;
        return;
    }
    out.push_back({text, style, link});
}

bool has_text(const std::vector<Run>& runs) {
    for (auto& r : runs)
        if (!blank(r.text)) return true;
    return false;
}

void trim_runs(std::vector<Run>& runs) {
    while (!runs.empty() && blank(runs.front().text)) runs.erase(runs.begin());
    while (!runs.empty() && blank(runs.back().text)) runs.pop_back();
    if (runs.empty()) return;
    auto& f = runs.front().text;
    f.erase(0, f.find_first_not_of(" \t\n"));
    auto& b = runs.back().text;
    b.erase(b.find_last_not_of(" \t\n") + 1);
}

std::string runs_text(const std::vector<Run>& runs) {
    std::string s;
    for (auto& r : runs) s += r.text;
    return s;
}

// After parsing: title, and an anchor for each heading (GitHub's ids,
// with -1, -2 ... for repeats).
void finish(Doc& d) {
    std::map<std::string, int> seen;
    for (size_t i = 0; i < d.blocks.size(); ++i) {
        auto& b = d.blocks[i];
        if (b.kind != Block::Heading) continue;
        std::string text = trim(runs_text(b.runs));
        if (d.title.empty() && b.level == 1) d.title = text;
        std::string id = slug(text);
        int n = seen[id]++;
        if (n) id += "-" + std::to_string(n);
        d.anchors.emplace(id, i);
    }
    if (d.title.empty())
        for (auto& b : d.blocks)
            if (b.kind == Block::Heading) {
                d.title = trim(runs_text(b.runs));
                break;
            }
    fold_doc(d);
}

// ---- Markdown ------------------------------------------------------------------

using Refs = std::map<std::string, std::string>;

bool is_punct(char ch) { return std::ispunct((unsigned char)ch) != 0; }
bool is_word(char ch) { return std::isalnum((unsigned char)ch) || uint8_t(ch) >= 0x80; }

// The link destination in "(url "title")", from just after '('. Returns
// the position after ')' or npos.
size_t link_dest(const std::string& s, size_t i, std::string& url) {
    while (i < s.size() && s[i] == ' ') ++i;
    if (i < s.size() && s[i] == '<') {
        size_t e = s.find('>', i);
        if (e == std::string::npos) return std::string::npos;
        url = s.substr(i + 1, e - i - 1);
        i = e + 1;
    } else {
        int depth = 0;
        size_t st = i;
        for (; i < s.size(); ++i) {
            char ch = s[i];
            if (ch == '\\' && i + 1 < s.size()) { ++i; continue; }
            if (ch == '(') ++depth;
            else if (ch == ')') {
                if (depth == 0) break;
                --depth;
            } else if (ch == ' ') break;
        }
        url = s.substr(st, i - st);
    }
    // optional title
    while (i < s.size() && s[i] == ' ') ++i;
    if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
        size_t e = s.find(s[i], i + 1);
        if (e == std::string::npos) return std::string::npos;
        i = e + 1;
        while (i < s.size() && s[i] == ' ') ++i;
    }
    if (i >= s.size() || s[i] != ')') return std::string::npos;
    return i + 1;
}

// Index of the ']' closing the '[' at s[i], or npos.
size_t close_bracket(const std::string& s, size_t i) {
    int depth = 0;
    for (; i < s.size(); ++i) {
        char ch = s[i];
        if (ch == '\\') { ++i; continue; }
        if (ch == '`') {   // brackets inside code don't count
            size_t e = s.find('`', i + 1);
            if (e != std::string::npos) i = e;
            continue;
        }
        if (ch == '[') ++depth;
        else if (ch == ']' && --depth == 0) return i;
    }
    return std::string::npos;
}

std::string attr(const std::string& tag, const char* name);

void inline_md(const std::string& s, Style base, int link, Doc& doc, const Refs& refs, std::vector<Run>& out) {
    bool strong = false;
    std::string buf;
    auto style = [&] { return strong && (base == Style::Body || base == Style::Quote) ? Style::Strong : base; };
    auto flush = [&] {
        emit(out, buf, style(), link);
        buf.clear();
    };
    auto add_link = [&](const std::string& text_md, const std::string& url) {
        flush();
        int idx = int(doc.links.size());
        doc.links.push_back(url);
        std::vector<Run> inner;
        inline_md(text_md, base, idx, doc, refs, inner);
        if (!has_text(inner)) emit(inner, url, base, idx);
        for (auto& r : inner) emit(out, r.text, r.style, r.link);
    };

    for (size_t i = 0; i < s.size();) {
        char ch = s[i];
        if (ch == '\\' && i + 1 < s.size() && is_punct(s[i + 1])) {
            buf += s[i + 1];
            i += 2;
            continue;
        }
        if (ch == '`') {
            size_t n = 0;
            while (i + n < s.size() && s[i + n] == '`') ++n;
            std::string fence(n, '`');
            size_t e = s.find(fence, i + n);
            while (e != std::string::npos && e + n < s.size() && s[e + n] == '`') e = s.find(fence, e + n + 1);
            if (e == std::string::npos) {
                buf += fence;
                i += n;
                continue;
            }
            std::string code = s.substr(i + n, e - i - n);
            if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') code = code.substr(1, code.size() - 2);
            flush();
            emit(out, code, Style::Code, link);
            i = e + n;
            continue;
        }
        if (ch == '!' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t cb = close_bracket(s, i + 1);
            std::string url;
            size_t end = cb != std::string::npos && cb + 1 < s.size() && s[cb + 1] == '(' ? link_dest(s, cb + 2, url)
                                                                                         : std::string::npos;
            if (end != std::string::npos) {
                std::string alt = trim(s.substr(i + 2, cb - i - 2));
                flush();
                emit(out, alt.empty() ? "[image]" : "[image: " + alt + "]", Style::Muted, link);
                i = end;
                continue;
            }
        }
        if (ch == '[' && link < 0) {
            size_t cb = close_bracket(s, i);
            if (cb != std::string::npos) {
                std::string text = s.substr(i + 1, cb - i - 1);
                std::string url;
                if (cb + 1 < s.size() && s[cb + 1] == '(') {
                    size_t end = link_dest(s, cb + 2, url);
                    if (end != std::string::npos) {
                        add_link(text, url);
                        i = end;
                        continue;
                    }
                }
                std::string label = text;
                size_t end = cb + 1;
                if (cb + 1 < s.size() && s[cb + 1] == '[') {
                    size_t cb2 = s.find(']', cb + 2);
                    if (cb2 != std::string::npos) {
                        if (cb2 > cb + 2) label = s.substr(cb + 2, cb2 - cb - 2);
                        end = cb2 + 1;
                    }
                }
                auto r = refs.find(lower(trim(label)));
                if (r != refs.end()) {
                    add_link(text, r->second);
                    i = end;
                    continue;
                }
            }
        }
        if (ch == '<') {
            size_t e = s.find('>', i);
            if (e != std::string::npos) {
                std::string in = s.substr(i + 1, e - i - 1);
                if ((starts_with(in, "http://") || starts_with(in, "https://") || starts_with(in, "mailto:")) &&
                    in.find(' ') == std::string::npos) {
                    if (link < 0) add_link(in, in);
                    else buf += in;
                    i = e + 1;
                    continue;
                }
                if (!in.empty() && (std::isalpha((unsigned char)in[0]) || in[0] == '/' || in[0] == '!')) {
                    std::string name = lower(in.substr(in[0] == '/' ? 1 : 0, in.find_first_of(" /\t\n", 1)));
                    if (name == "br") buf += ' ';
                    else if (name == "img") {
                        std::string alt = attr(in, "alt");
                        flush();
                        emit(out, alt.empty() ? "[image]" : "[image: " + alt + "]", Style::Muted, link);
                    } else if (name == "a" && in[0] != '/' && link < 0) {
                        // <a href="x">text</a> inside Markdown
                        std::string href = attr(in, "href");
                        size_t close = lower(s).find("</a>", e);
                        if (!href.empty() && close != std::string::npos) {
                            add_link(s.substr(e + 1, close - e - 1), href);
                            i = close + 4;
                            continue;
                        }
                    }
                    i = e + 1;
                    continue;
                }
            }
        }
        if (ch == '*' || ch == '_') {
            size_t n = 0;
            while (i + n < s.size() && s[i + n] == ch) ++n;
            char prev = i ? s[i - 1] : ' ';
            char next = i + n < s.size() ? s[i + n] : ' ';
            bool word_inside = ch == '_' && is_word(prev) && is_word(next);   // snake_case
            if (!word_inside) {
                if (n >= 2) {
                    std::string mark(2, ch);
                    if (strong || s.find(mark, i + n) != std::string::npos) {
                        flush();
                        strong = !strong;
                        i += n;
                        continue;
                    }
                } else {
                    bool opens = next != ' ' && s.find(ch, i + 1) != std::string::npos;
                    bool closes = prev != ' ';
                    if (opens || closes) {   // *emphasis*: no italic font, so just the text
                        i += 1;
                        continue;
                    }
                }
            }
            buf.append(n, ch);
            i += n;
            continue;
        }
        if (ch == '~' && i + 1 < s.size() && s[i + 1] == '~') {
            i += 2;
            continue;
        }
        if (ch == '&') {
            size_t e = s.find(';', i);
            if (e != std::string::npos && e - i <= 10) {
                std::string d = decode_entities(s.substr(i, e - i + 1));
                if (d != s.substr(i, e - i + 1)) {
                    buf += d;
                    i = e + 1;
                    continue;
                }
            }
        }
        if (link < 0 && (ch == 'h') && (starts_with(s.substr(i, 8), "https://") || starts_with(s.substr(i, 7), "http://")) &&
            (i == 0 || !is_word(s[i - 1]))) {
            size_t e = i;
            while (e < s.size() && !std::isspace((unsigned char)s[e]) && s[e] != '<') ++e;
            while (e > i && std::strchr(".,:;!?)'\"*_", s[e - 1])) --e;
            std::string url = s.substr(i, e - i);
            add_link(url, url);
            i = e;
            continue;
        }
        buf += ch;
        ++i;
    }
    flush();
}

bool is_rule(const std::string& t) {
    if (t.empty() || !std::strchr("-*_", t[0])) return false;
    int n = 0;
    for (char ch : t) {
        if (ch == t[0]) ++n;
        else if (ch != ' ') return false;
    }
    return n >= 3;
}

bool is_table_sep(const std::string& line) {
    std::string t = trim(line);
    if (t.find('-') == std::string::npos) return false;
    for (char ch : t)
        if (!std::strchr("|:- ", ch)) return false;
    return t.find('|') != std::string::npos || t.find(':') != std::string::npos;
}

std::vector<std::string> table_cells(const std::string& line) {
    std::string t = trim(line);
    if (!t.empty() && t.front() == '|') t.erase(0, 1);
    if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) t.pop_back();
    std::vector<std::string> cells;
    std::string cur;
    bool code = false;
    for (size_t i = 0; i < t.size(); ++i) {
        char ch = t[i];
        if (ch == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cur += '|';
            ++i;
        } else if (ch == '`') {
            code = !code;
            cur += ch;
        } else if (ch == '|' && !code) {
            cells.push_back(trim(cur));
            cur.clear();
        } else {
            cur += ch;
        }
    }
    cells.push_back(trim(cur));
    return cells;
}

// A list marker at the start of a line: "- ", "* ", "+ ", "1. ", "2) ".
bool list_marker(const std::string& line, int& indent, std::string& bullet, std::string& rest) {
    indent = indent_of(line);
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos) return false;
    if (std::strchr("-*+", line[i])) {
        if (i + 1 >= line.size() || line[i + 1] != ' ') return false;
        bullet.clear();
        rest = line.substr(i + 2);
    } else {
        size_t j = i;
        while (j < line.size() && std::isdigit((unsigned char)line[j]) && j - i < 9) ++j;
        if (j == i || j + 1 >= line.size() || (line[j] != '.' && line[j] != ')') || line[j + 1] != ' ') return false;
        bullet = line.substr(i, j - i + 1);
        rest = line.substr(j + 2);
    }
    rest = trim(rest);
    if (starts_with(rest, "[ ] ")) {
        bullet = "[ ]";
        rest = rest.substr(4);
    } else if (starts_with(rest, "[x] ") || starts_with(rest, "[X] ")) {
        bullet = "[x]";
        rest = rest.substr(4);
    }
    return true;
}

}  // namespace

Doc parse_markdown(const std::string& input) {
    Doc doc;
    // HTML comments go first: they can hide whole sections.
    std::string text;
    for (size_t i = 0; i < input.size();) {
        if (input.compare(i, 4, "<!--") == 0) {
            size_t e = input.find("-->", i + 4);
            i = e == std::string::npos ? input.size() : e + 3;
            continue;
        }
        text += input[i++];
    }

    std::vector<std::string> lines;
    for (size_t i = 0; i <= text.size();) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        lines.push_back(expand_tabs(text.substr(i, e - i)));
        i = e + 1;
    }

    // Reference definitions: [name]: url
    Refs refs;
    bool in_fence = false;
    for (auto& l : lines) {
        std::string t = trim(l);
        if (starts_with(t, "```") || starts_with(t, "~~~")) in_fence = !in_fence;
        if (in_fence || indent_of(l) > 3 || t.empty() || t[0] != '[') continue;
        size_t cb = t.find("]:");
        if (cb == std::string::npos || cb < 2) continue;
        std::string url = trim(t.substr(cb + 2));
        url = url.substr(0, url.find(' '));
        if (url.size() > 1 && url.front() == '<' && url.back() == '>') url = url.substr(1, url.size() - 2);
        if (url.empty()) continue;
        refs[lower(t.substr(1, cb - 1))] = url;
        l = "\x01";   // a line that is skipped below
    }

    // The block being gathered from consecutive lines.
    std::string pending;
    Block::Kind kind = Block::Para;
    int level = 0;
    std::string bullet;
    std::vector<int> list_indents;   // indent of each open list level
    bool in_list = false;

    auto flush = [&] {
        if (pending.empty()) return;
        Block b;
        b.kind = kind;
        b.level = level;
        Style st = kind == Block::Quote ? Style::Quote : Style::Body;
        if (kind == Block::Heading) st = level == 1 ? Style::H1 : level == 2 ? Style::H2 : Style::H3;
        if (kind == Block::Item) {
            static const char* const dots[] = {"\xE2\x80\xA2", "\xE2\x80\x93", "\xC2\xB7"};   // • – ·
            b.runs.push_back({bullet.empty() ? dots[level % 3] : bullet, Style::Muted, -1});
            b.runs.back().text += ' ';
        }
        inline_md(pending, st, -1, doc, refs, b.runs);
        trim_runs(b.runs);
        if (b.runs.size() > (kind == Block::Item ? 1u : 0u)) doc.blocks.push_back(std::move(b));
        pending.clear();
        kind = Block::Para;
        level = 0;
    };
    auto push_text = [&](Block::Kind k, int lv, const std::string& t) {
        if (!pending.empty() && (k != kind || lv != level)) flush();
        kind = k;
        level = lv;
        if (!pending.empty()) pending += ' ';
        pending += trim(t);
    };

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        if (line == "\x01") continue;
        std::string t = trim(line);

        // fenced code
        if (starts_with(t, "```") || starts_with(t, "~~~")) {
            flush();
            char fc = t[0];
            size_t flen = t.find_first_not_of(fc);
            if (flen == std::string::npos) flen = t.size();
            int ind = indent_of(line);
            std::string code;
            size_t j = li + 1;
            for (; j < lines.size(); ++j) {
                std::string tj = trim(lines[j]);
                if (tj.size() >= flen && tj.find_first_not_of(fc) >= flen && std::string(flen, fc) == tj.substr(0, flen) &&
                    trim(tj.substr(flen)).empty())
                    break;
                std::string l = lines[j];
                int k = 0;
                while (k < ind && !l.empty() && l[0] == ' ') {
                    l.erase(0, 1);
                    ++k;
                }
                if (!code.empty() || j > li + 1) code += '\n';
                code += l;
            }
            Block b;
            b.kind = Block::Pre;
            b.code = true;
            b.level = in_list && ind >= 2 ? int(list_indents.size()) : 0;
            b.runs.push_back({code, Style::Pre, -1});
            doc.blocks.push_back(std::move(b));
            li = j;   // past the closing fence
            continue;
        }

        if (t.empty()) {
            flush();
            continue;
        }

        // setext heading: text, then === or ---
        if (!pending.empty() && kind == Block::Para && level == 0 && indent_of(line) < 4 &&
            (t.find_first_not_of('=') == std::string::npos || t.find_first_not_of('-') == std::string::npos)) {
            kind = Block::Heading;
            level = t[0] == '=' ? 1 : 2;
            flush();
            continue;
        }

        // ATX heading
        if (indent_of(line) < 4 && t[0] == '#') {
            size_t n = t.find_first_not_of('#');
            if (n <= 6 && (n == std::string::npos || t[n] == ' ')) {
                flush();
                std::string h = n == std::string::npos ? "" : trim(t.substr(n));
                size_t e = h.find_last_not_of('#');
                if (e != std::string::npos && e + 1 < h.size() && h[e] == ' ') h = trim(h.substr(0, e));
                push_text(Block::Heading, int(n == std::string::npos ? t.size() : n), h);
                flush();
                in_list = false;
                continue;
            }
        }

        if (indent_of(line) < 4 && is_rule(t)) {
            flush();
            Block b;
            b.kind = Block::Rule;
            doc.blocks.push_back(b);
            in_list = false;
            continue;
        }

        // table: a row of cells followed by |---|---|
        if (t.find('|') != std::string::npos && li + 1 < lines.size() && is_table_sep(lines[li + 1])) {
            flush();
            in_list = false;
            bool header = true;
            size_t j = li;
            for (; j < lines.size(); ++j) {
                if (j == li + 1) continue;   // the separator
                std::string tj = trim(lines[j]);
                if (tj.empty() || tj.find('|') == std::string::npos) break;
                Block b;
                b.kind = Block::Row;
                b.level = header ? 1 : 0;
                auto cells = table_cells(tj);
                for (size_t c = 0; c < cells.size(); ++c) {
                    if (cells[c].empty()) continue;
                    if (has_text(b.runs)) emit(b.runs, "  \xC2\xB7  ", Style::Muted, -1);   // " · "
                    Style st = header ? Style::Muted : c == 0 && cells.size() > 1 ? Style::Strong : Style::Body;
                    std::vector<Run> rs;
                    inline_md(cells[c], st, -1, doc, refs, rs);
                    for (auto& r : rs) emit(b.runs, r.text, r.style, r.link);
                }
                trim_runs(b.runs);
                if (has_text(b.runs)) doc.blocks.push_back(std::move(b));
                header = false;
            }
            li = j - 1;
            continue;
        }

        // blockquote
        if (indent_of(line) < 4 && t[0] == '>') {
            std::string q = t.substr(1);
            while (!q.empty() && q[0] == '>') q.erase(0, 1);   // nested: flattened
            q = trim(q);
            if (q.empty()) {
                flush();
                continue;
            }
            if (kind != Block::Quote) flush();
            push_text(Block::Quote, 0, q);
            continue;
        }

        // list item
        int ind;
        std::string bul, rest;
        if (list_marker(line, ind, bul, rest) && !(kind == Block::Para && !pending.empty() && !in_list && !bul.empty() && bul != "1." && bul != "1)")) {
            flush();
            if (!in_list) list_indents.clear();
            while (!list_indents.empty() && ind < list_indents.back()) list_indents.pop_back();
            if (list_indents.empty() || ind > list_indents.back()) list_indents.push_back(ind);
            in_list = true;
            bullet = bul;
            push_text(Block::Item, int(list_indents.size()) - 1, rest);
            continue;
        }

        // indented code (not inside a list or paragraph)
        if (indent_of(line) >= 4 && pending.empty() && !in_list) {
            std::string code;
            size_t j = li;
            for (; j < lines.size(); ++j) {
                if (!blank(lines[j]) && indent_of(lines[j]) < 4) break;
                if (j > li) code += '\n';
                code += lines[j].size() > 4 ? lines[j].substr(4) : "";
            }
            while (!code.empty() && code.back() == '\n') code.pop_back();
            Block b;
            b.kind = Block::Pre;
            b.code = true;
            b.runs.push_back({code, Style::Pre, -1});
            doc.blocks.push_back(std::move(b));
            li = j - 1;
            continue;
        }

        // text: continues the open paragraph, item or quote
        if (!pending.empty()) {
            pending += ' ';
            pending += t;
            continue;
        }
        if (in_list && indent_of(line) >= 2) {
            push_text(Block::Para, int(list_indents.size()), t);   // a paragraph inside an item
            continue;
        }
        in_list = false;
        push_text(Block::Para, 0, t);
    }
    flush();
    finish(doc);
    return doc;
}

// ---- HTML ------------------------------------------------------------------------

namespace {

std::string attr(const std::string& tag, const char* name) {
    std::string lt = lower(tag);
    size_t n = std::strlen(name);
    for (size_t i = lt.find(name); i != std::string::npos; i = lt.find(name, i + 1)) {
        if (i > 0 && !std::isspace((unsigned char)lt[i - 1])) continue;
        size_t j = i + n;
        while (j < lt.size() && lt[j] == ' ') ++j;
        if (j >= lt.size() || lt[j] != '=') continue;
        ++j;
        while (j < lt.size() && lt[j] == ' ') ++j;
        if (j >= lt.size()) return "";
        if (tag[j] == '"' || tag[j] == '\'') {
            size_t e = tag.find(tag[j], j + 1);
            return decode_entities(tag.substr(j + 1, (e == std::string::npos ? tag.size() : e) - j - 1));
        }
        size_t e = tag.find_first_of(" \t\n>", j);
        return decode_entities(tag.substr(j, (e == std::string::npos ? tag.size() : e) - j));
    }
    return "";
}

}  // namespace

Doc parse_html(const std::string& h) {
    Doc doc;
    std::vector<Run> runs;
    Block::Kind kind = Block::Para;
    int level = 0;
    int link = -1, strong = 0, code = 0, pre = 0, quote = 0;
    bool first_cell = true, row_header = false;
    std::vector<int> lists;   // 0: <ul>, else the next number of an <ol>
    std::string title, pre_text;
    bool space = true;   // the last thing written was a space (or nothing)

    auto flush = [&] {
        trim_runs(runs);
        if (has_text(runs)) {
            Block b;
            b.kind = quote && kind == Block::Para ? Block::Quote : kind;
            b.level = kind == Block::Row ? (row_header ? 1 : 0) : level;
            b.runs = std::move(runs);
            if (b.kind == Block::Quote)
                for (auto& r : b.runs)
                    if (r.style == Style::Body) r.style = Style::Quote;
            doc.blocks.push_back(std::move(b));
        }
        runs.clear();
        space = true;
        if (kind != Block::Row) {
            kind = Block::Para;
            level = 0;
        }
    };
    auto style = [&] {
        if (code) return Style::Code;
        if (kind == Block::Heading) return level == 1 ? Style::H1 : level == 2 ? Style::H2 : Style::H3;
        if (strong) return Style::Strong;
        return Style::Body;
    };
    auto text = [&](const std::string& raw) {
        if (pre) {
            pre_text += decode_entities(raw);
            return;
        }
        std::string t;
        for (char ch : raw) {
            if (std::isspace((unsigned char)ch)) {
                if (!space) t += ' ';
                space = true;
            } else {
                t += ch;
                space = false;
            }
        }
        if (!t.empty()) emit(runs, decode_entities(t), style(), link);
    };

    for (size_t i = 0; i < h.size();) {
        if (h[i] != '<') {
            size_t e = h.find('<', i);
            if (e == std::string::npos) e = h.size();
            text(h.substr(i, e - i));
            i = e;
            continue;
        }
        if (h.compare(i, 4, "<!--") == 0) {
            size_t e = h.find("-->", i + 4);
            i = e == std::string::npos ? h.size() : e + 3;
            continue;
        }
        // the tag, honouring quotes so a '>' inside an attribute doesn't end it
        size_t e = i + 1;
        char q = 0;
        for (; e < h.size(); ++e) {
            if (q) {
                if (h[e] == q) q = 0;
            } else if (h[e] == '"' || h[e] == '\'') {
                q = h[e];
            } else if (h[e] == '>') {
                break;
            }
        }
        std::string tag = h.substr(i + 1, e - i - 1);
        i = e + 1;
        if (tag.empty() || tag[0] == '!' || tag[0] == '?') continue;
        bool closing = tag[0] == '/';
        size_t ns = closing ? 1 : 0;
        size_t ne = tag.find_first_of(" \t\r\n/", ns);
        std::string name = lower(tag.substr(ns, ne == std::string::npos ? std::string::npos : ne - ns));
        if (name.empty() || !std::isalpha((unsigned char)name[0])) {
            text("<" + tag + ">");
            continue;
        }

        // elements whose content isn't text to read
        if (!closing && (name == "script" || name == "style" || name == "template" || name == "svg" ||
                         name == "noscript" || name == "title" || name == "head" || name == "select")) {
            std::string end = "</" + name;
            // case-insensitive search for the end tag
            size_t c;
            for (c = h.find("</", i); c != std::string::npos; c = h.find("</", c + 2))
                if (lower(h.substr(c, end.size())) == end) break;
            if (name == "title" && title.empty())
                title = trim(decode_entities(h.substr(i, (c == std::string::npos ? h.size() : c) - i)));
            if (name == "head") {
                // <title> lives in <head>
                std::string head = lower(h.substr(i, (c == std::string::npos ? h.size() : c) - i));
                size_t t0 = head.find("<title");
                if (t0 != std::string::npos && title.empty()) {
                    size_t t1 = head.find('>', t0), t2 = head.find("</title", t0);
                    if (t1 != std::string::npos && t2 != std::string::npos && t2 > t1)
                        title = trim(decode_entities(h.substr(i + t1 + 1, t2 - t1 - 1)));
                }
            }
            i = c == std::string::npos ? h.size() : h.find('>', c) + 1;
            if (i == 0) i = h.size();
            continue;
        }

        if (!closing) {
            std::string id = attr(tag, "id");
            if (id.empty() && name == "a") id = attr(tag, "name");
            if (!id.empty()) doc.anchors.emplace(id, doc.blocks.size() + (has_text(runs) ? 1 : 0));
        }

        static const char* const blocks[] = {"p", "div", "section", "article", "main", "header", "footer", "nav",
                                             "aside", "form", "fieldset", "figure", "figcaption", "details",
                                             "summary", "dl", "dt", "dd", "address", "center", "body", "html",
                                             "caption", "legend", "label"};
        bool is_block = std::find_if(std::begin(blocks), std::end(blocks),
                                     [&](const char* b) { return name == b; }) != std::end(blocks);

        if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
            flush();
            if (!closing) {
                kind = Block::Heading;
                level = name[1] - '0';
            }
        } else if (is_block) {
            if (kind != Block::Item || closing) flush();
            else if (has_text(runs)) flush();
        } else if (name == "br") {
            if (pre) pre_text += '\n';
            else {
                Block::Kind k = kind;
                int lv = level;
                flush();
                kind = k;
                level = lv;
            }
        } else if (name == "hr") {
            flush();
            Block b;
            b.kind = Block::Rule;
            doc.blocks.push_back(b);
        } else if (name == "ul" || name == "ol" || name == "menu") {
            flush();
            if (closing) {
                if (!lists.empty()) lists.pop_back();
            } else {
                lists.push_back(name == "ol" ? 1 : 0);
            }
        } else if (name == "li") {
            flush();
            if (!closing) {
                kind = Block::Item;
                level = lists.empty() ? 0 : int(lists.size()) - 1;
                static const char* const dots[] = {"\xE2\x80\xA2 ", "\xE2\x80\x93 ", "\xC2\xB7 "};
                std::string b = !lists.empty() && lists.back() ? std::to_string(lists.back()++) + ". " : dots[level % 3];
                emit(runs, b, Style::Muted, -1);
                space = true;
            }
        } else if (name == "blockquote") {
            flush();
            quote += closing ? -1 : 1;
            if (quote < 0) quote = 0;
        } else if (name == "pre") {
            flush();
            if (!closing) {
                pre = 1;
                pre_text.clear();
            } else if (pre) {
                pre = 0;
                if (!pre_text.empty() && pre_text[0] == '\n') pre_text.erase(0, 1);
                while (!pre_text.empty() && (pre_text.back() == '\n' || pre_text.back() == ' ')) pre_text.pop_back();
                Block b;
                b.kind = Block::Pre;
                b.code = true;
                b.runs.push_back({expand_tabs(pre_text), Style::Pre, -1});
                if (!pre_text.empty()) doc.blocks.push_back(std::move(b));
                pre_text.clear();
            }
        } else if (name == "tr") {
            flush();
            if (!closing) {
                kind = Block::Row;
                first_cell = true;
                row_header = false;
            } else {
                kind = Block::Para;
            }
        } else if (name == "td" || name == "th") {
            if (!closing) {
                if (kind != Block::Row) {
                    flush();
                    kind = Block::Row;
                    first_cell = true;
                }
                if (!first_cell) emit(runs, "  \xC2\xB7  ", Style::Muted, -1);
                first_cell = false;
                if (name == "th") row_header = true;
                space = true;
            }
            strong += name == "th" ? (closing ? -1 : 1) : 0;
        } else if (name == "table" || name == "thead" || name == "tbody" || name == "tfoot") {
            flush();
            kind = Block::Para;
        } else if (name == "a") {
            if (closing) link = -1;
            else {
                std::string href = attr(tag, "href");
                if (!href.empty() && !starts_with(lower(href), "javascript:")) {
                    link = int(doc.links.size());
                    doc.links.push_back(href);
                }
            }
        } else if (name == "b" || name == "strong") {
            strong += closing ? -1 : 1;
        } else if (name == "code" || name == "kbd" || name == "tt" || name == "samp") {
            if (!pre) code += closing ? -1 : 1;
        } else if (name == "img" && !closing) {
            std::string alt = trim(attr(tag, "alt"));
            if (!alt.empty()) {
                if (!space) emit(runs, " ", style(), link);
                emit(runs, "[image: " + alt + "] ", Style::Muted, link);
                space = true;
            }
        } else if (name == "input" && !closing) {
            std::string v = attr(tag, "value");
            if (v.empty()) v = attr(tag, "placeholder");
            if (!v.empty() && attr(tag, "type") != "hidden") emit(runs, "[" + v + "] ", Style::Muted, link);
        }
        if (strong < 0) strong = 0;
        if (code < 0) code = 0;
    }
    flush();
    doc.title = title;
    std::string t = doc.title;
    finish(doc);
    if (!t.empty()) doc.title = fold(t);
    return doc;
}

Doc parse_text(const std::string& text) {
    Doc doc;
    Block b;
    b.kind = Block::Pre;
    std::string t = expand_tabs(text);
    while (!t.empty() && t.back() == '\n') t.pop_back();
    b.runs.push_back({t, Style::Body, -1});
    doc.blocks.push_back(std::move(b));
    fold_doc(doc);
    return doc;
}

std::string slug(const std::string& heading) {
    std::string out;
    for (char ch : lower(trim(heading))) {
        if (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_' || uint8_t(ch) >= 0x80) out += ch;
        else if (ch == ' ') out += '-';
    }
    return out;
}

std::string decode_entities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    static const std::map<std::string, uint32_t> named = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"copy", 0xA9}, {"reg", 0xAE}, {"middot", 0xB7}, {"mdash", 0x2014}, {"ndash", 0x2013},
        {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"bull", 0x2022}, {"times", 0xD7}, {"rarr", 0x2192}, {"larr", 0x2190}, {"deg", 0xB0},
        {"trade", 0x2122}, {"laquo", 0xAB}, {"raquo", 0xBB}};
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            size_t e = s.find(';', i);
            if (e != std::string::npos && e - i <= 10) {
                std::string n = s.substr(i + 1, e - i - 1);
                uint32_t cp = 0;
                if (n.size() > 1 && n[0] == '#') {
                    bool hex = n[1] == 'x' || n[1] == 'X';
                    std::string digits = n.substr(hex ? 2 : 1);
                    bool ok = !digits.empty();
                    for (char ch : digits)
                        if (!(hex ? std::isxdigit((unsigned char)ch) : std::isdigit((unsigned char)ch))) ok = false;
                    if (ok) cp = uint32_t(std::stoul(digits, nullptr, hex ? 16 : 10));
                } else {
                    auto it = named.find(n);
                    if (it != named.end()) cp = it->second;
                }
                if (cp && cp < 0x110000) {
                    put_utf8(out, cp);
                    i = e;
                    continue;
                }
            }
        }
        out += s[i];
    }
    return out;
}

// ---- layout ------------------------------------------------------------------------

namespace {

struct Wrapper {
    const Metrics& m;
    int width;
    std::vector<Line>& out;
    size_t block;

    Line line;
    int x = 0, x_next = 0;
    bool has_word = false;

    void start(int first_x, int next_x) {
        line = Line{};
        line.block = block;
        x = first_x;
        x_next = next_x;
        has_word = false;
    }

    void put(const std::string& t, Style st, int link, int w) {
        if (!line.pieces.empty() && line.pieces.back().style == st && line.pieces.back().link == link)
            line.pieces.back().text += t;
        else
            line.pieces.push_back({t, st, link, x});
        x += w;
        line.h = std::max(line.h, m.height(st));
    }

    void end_line() {
        // trailing spaces would only make a focused link's box wider
        while (!line.pieces.empty()) {
            auto& t = line.pieces.back().text;
            t.erase(t.find_last_not_of(' ') + 1);
            if (!t.empty()) break;
            line.pieces.pop_back();
        }
        if (line.h == 0) line.h = m.height(Style::Body);
        line.th = line.h;
        out.push_back(line);
        size_t b = line.block;
        bool shade = line.shade, bar = line.bar;
        line = Line{};
        line.block = b;
        line.shade = shade;
        line.bar = bar;
        x = x_next;
        has_word = false;
    }

    void word(const std::string& w, Style st, int link) {
        int ww = m.width(w, st);
        if (has_word && x + ww > width) end_line();
        if (x + ww <= width) {
            put(w, st, link, ww);
            has_word = true;
            return;
        }
        // longer than a whole line (a long URL): break between characters
        std::string part;
        int pw = 0;
        for (size_t i = 0; i < w.size();) {
            size_t s = i;
            next_cp(w, i);
            std::string ch = w.substr(s, i - s);
            int cw = m.width(ch, st);
            if (x + pw + cw > width && !part.empty()) {
                put(part, st, link, pw);
                end_line();
                part.clear();
                pw = 0;
            }
            part += ch;
            pw += cw;
        }
        if (!part.empty()) {
            put(part, st, link, pw);
            has_word = true;
        }
    }

    void space(Style st, int link) {
        if (!has_word) return;
        int sw = m.width(" ", st);
        if (x + sw <= width) put(" ", st, link, sw);
    }

    void runs(const std::vector<Run>& rs) {
        for (auto& r : rs) {
            const std::string& t = r.text;
            for (size_t i = 0; i < t.size();) {
                if (t[i] == ' ' || t[i] == '\n') {
                    space(r.style, r.link);
                    ++i;
                    continue;
                }
                size_t e = t.find_first_of(" \n", i);
                if (e == std::string::npos) e = t.size();
                word(t.substr(i, e - i), r.style, r.link);
                i = e;
            }
        }
    }
};

}  // namespace

std::vector<Line> layout(const Doc& doc, int width, const Metrics& m) {
    std::vector<Line> out;
    const int kIndent = 14, kGap = 6, kTight = 2;
    auto gap_after = [&](int g) {
        if (!out.empty()) out.back().h += g;
    };

    for (size_t bi = 0; bi < doc.blocks.size(); ++bi) {
        const Block& b = doc.blocks[bi];
        const Block* next = bi + 1 < doc.blocks.size() ? &doc.blocks[bi + 1] : nullptr;
        Wrapper w{m, width, out, bi, Line{}, 0, 0, false};

        switch (b.kind) {
            case Block::Rule: {
                Line l;
                l.block = bi;
                l.h = l.th = 9;
                l.rule = true;
                out.push_back(l);
                break;
            }
            case Block::Heading: {
                if (bi > 0) gap_after(4);
                Style hs = b.level == 1 ? Style::H1 : b.level == 2 ? Style::H2 : Style::H3;
                std::vector<Run> rs = b.runs;
                for (auto& r : rs)
                    if (r.style != Style::Code && r.style != Style::Muted) r.style = hs;
                w.start(0, 0);
                w.runs(rs);
                w.end_line();
                if (b.level <= 2) {
                    out.back().rule = true;
                    out.back().h += 3;
                }
                gap_after(4);
                break;
            }
            case Block::Para:
            case Block::Quote: {
                int ind = b.kind == Block::Quote ? 10 : b.level * kIndent;
                w.start(ind, ind);
                w.line.bar = b.kind == Block::Quote;
                w.runs(b.runs);
                w.end_line();
                gap_after(kGap);
                break;
            }
            case Block::Item: {
                int ind = b.level * kIndent;
                int bw = b.runs.empty() ? 0 : m.width(b.runs[0].text, b.runs[0].style);
                w.start(ind, ind + bw);
                if (!b.runs.empty()) w.put(b.runs[0].text, b.runs[0].style, -1, bw);
                std::vector<Run> rest(b.runs.begin() + (b.runs.empty() ? 0 : 1), b.runs.end());
                w.has_word = false;
                w.runs(rest);
                w.end_line();
                bool more = next && (next->kind == Block::Item || (next->kind == Block::Para && next->level > 0) ||
                                     (next->kind == Block::Pre && next->level > 0));
                gap_after(more ? kTight : kGap);
                break;
            }
            case Block::Row: {
                w.start(0, 12);
                w.runs(b.runs);
                w.end_line();
                gap_after(next && next->kind == Block::Row ? kTight + 1 : kGap);
                break;
            }
            case Block::Pre: {
                // Line by line, keeping indentation; long lines wrap at the
                // character that doesn't fit (code has no word boundaries
                // worth keeping on a 320 px screen).
                const Run& r = b.runs.empty() ? Run{} : b.runs[0];
                int ind = b.level * kIndent + (b.code ? 4 : 0);
                int right = width - (b.code ? 4 : 0);
                const std::string& t = r.text;
                for (size_t i = 0; i <= t.size();) {
                    size_t e = t.find('\n', i);
                    if (e == std::string::npos) e = t.size();
                    std::string src = t.substr(i, e - i);
                    Line l;
                    l.block = bi;
                    l.shade = b.code;
                    l.h = l.th = m.height(r.style);
                    std::string cur;
                    int x = ind, cw_sum = 0;
                    for (size_t k = 0; k < src.size();) {
                        size_t s = k;
                        next_cp(src, k);
                        std::string ch = src.substr(s, k - s);
                        int cw = m.width(ch, r.style);
                        if (x + cw_sum + cw > right && !cur.empty()) {
                            l.pieces.push_back({cur, r.style, -1, x});
                            out.push_back(l);
                            l.pieces.clear();
                            cur.clear();
                            cw_sum = 0;
                            x = ind + 8;   // continuation, a little further in
                        }
                        cur += ch;
                        cw_sum += cw;
                    }
                    if (!cur.empty()) l.pieces.push_back({cur, r.style, -1, x});
                    out.push_back(l);
                    i = e + 1;
                }
                gap_after(kGap);
                break;
            }
        }
    }
    return out;
}

// ---- addresses ------------------------------------------------------------------------

namespace {

struct Url {
    std::string scheme, host, path, query, frag;
};

bool has_scheme(const std::string& s) {
    size_t i = 0;
    if (s.empty() || !std::isalpha((unsigned char)s[0])) return false;
    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '+' || s[i] == '.' || s[i] == '-')) ++i;
    return i < s.size() && s[i] == ':';
}

Url parse_url(const std::string& u) {
    Url r;
    std::string s = u;
    size_t h = s.find('#');
    if (h != std::string::npos) {
        r.frag = s.substr(h + 1);
        s.erase(h);
    }
    size_t q = s.find('?');
    if (q != std::string::npos) {
        r.query = s.substr(q + 1);
        s.erase(q);
    }
    size_t c = s.find("://");
    if (c != std::string::npos && has_scheme(s)) {
        r.scheme = lower(s.substr(0, c));
        size_t p = s.find('/', c + 3);
        r.host = s.substr(c + 3, p == std::string::npos ? std::string::npos : p - c - 3);
        r.path = p == std::string::npos ? "/" : s.substr(p);
    } else {
        r.path = s;
    }
    return r;
}

std::string normalize_path(const std::string& p) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t e = p.find('/', i);
        if (e == std::string::npos) e = p.size();
        std::string seg = p.substr(i, e - i);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (seg != "." && !seg.empty()) {
            parts.push_back(seg);
        }
        i = e + 1;
    }
    std::string out;
    for (auto& s : parts) out += "/" + s;
    bool dir = !p.empty() && (p.back() == '/' || (p.size() >= 2 && p.compare(p.size() - 2, 2, "/.") == 0) ||
                              (p.size() >= 3 && p.compare(p.size() - 3, 3, "/..") == 0));
    if (out.empty() || dir) out += "/";
    return out;
}

std::string join(const Url& u) {
    std::string s = u.scheme + "://" + u.host + u.path;
    if (!u.query.empty()) s += "?" + u.query;
    if (!u.frag.empty()) s += "#" + u.frag;
    return s;
}

std::string extension(const std::string& url) {
    Url u = parse_url(url);
    size_t slash = u.path.rfind('/');
    std::string name = u.path.substr(slash == std::string::npos ? 0 : slash + 1);
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : lower(name.substr(dot + 1));
}

}  // namespace

std::string resolve_url(const std::string& base, const std::string& ref_in) {
    std::string ref = trim(ref_in);
    if (ref.empty()) return base;
    if (has_scheme(ref)) return ref;
    Url b = parse_url(base);
    if (starts_with(ref, "//")) return b.scheme + ":" + ref;
    if (ref[0] == '#') {
        b.frag = ref.substr(1);
        return join(b);
    }
    Url r = parse_url(ref);
    b.frag = r.frag;
    b.query = r.query;
    if (ref[0] == '?') {
        return join(b);
    }
    if (r.path.empty()) return join(b);
    if (r.path[0] == '/') {
        b.path = normalize_path(r.path);
    } else {
        size_t slash = b.path.rfind('/');
        std::string dir = slash == std::string::npos ? "/" : b.path.substr(0, slash + 1);
        b.path = normalize_path(dir + r.path);
    }
    return join(b);
}

std::string rewrite_url(const std::string& url) {
    Url u = parse_url(url);
    if ((u.scheme != "https" && u.scheme != "http") || (u.host != "github.com" && u.host != "www.github.com"))
        return url;
    std::vector<std::string> seg;
    for (size_t i = 1; i <= u.path.size();) {
        size_t e = u.path.find('/', i);
        if (e == std::string::npos) e = u.path.size();
        if (e > i) seg.push_back(u.path.substr(i, e - i));
        i = e + 1;
    }
    std::string raw = "https://raw.githubusercontent.com/";
    std::string frag = u.frag.empty() ? "" : "#" + u.frag;
    if (seg.size() == 2) return raw + seg[0] + "/" + seg[1] + "/HEAD/README.md" + frag;
    if (seg.size() >= 4 && (seg[2] == "blob" || seg[2] == "raw" || seg[2] == "tree")) {
        std::string path;
        for (size_t i = 4; i < seg.size(); ++i) path += "/" + seg[i];
        if (seg[2] == "tree") path += "/README.md";
        if (path.empty()) return url;
        return raw + seg[0] + "/" + seg[1] + "/" + seg[3] + path + frag;
    }
    return url;
}

std::string typed_url(const std::string& typed, const std::string& home) {
    std::string t = trim(typed);
    if (t.empty()) return "";
    if (t.find("://") != std::string::npos) return t;
    if (t[0] == '/') return "file://" + t;
    if (t[0] == '~') return "file://" + home + t.substr(1);
    size_t slash = t.find('/');
    std::string first = t.substr(0, slash);
    bool simple = true;
    for (char ch : t)
        if (!(std::isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '/')) simple = false;
    if (simple && slash != std::string::npos && slash + 1 < t.size() && first.find('.') == std::string::npos &&
        first != "localhost")
        return "https://github.com/" + t;   // owner/repo, owner/repo/blob/main/x.md
    return "https://" + t;
}

std::string split_fragment(std::string& url) {
    size_t h = url.find('#');
    if (h == std::string::npos) return "";
    std::string f = url.substr(h + 1);
    url.erase(h);
    return f;
}

std::string short_url(const std::string& url) {
    std::string s = url;
    split_fragment(s);
    if (starts_with(s, "https://raw.githubusercontent.com/")) {
        // owner/repo/ref/path -> github.com/owner/repo/path
        std::string rest = s.substr(34);
        size_t a = rest.find('/'), b = a == std::string::npos ? a : rest.find('/', a + 1);
        size_t c = b == std::string::npos ? b : rest.find('/', b + 1);
        if (c != std::string::npos) return "github.com/" + rest.substr(0, b) + rest.substr(c);
    }
    size_t c = s.find("://");
    if (c != std::string::npos) s = s.substr(c + 3);
    if (starts_with(s, "www.")) s = s.substr(4);
    return s;
}

bool unsupported_by_name(const std::string& url) {
    static const char* const exts[] = {"png", "jpg", "jpeg", "gif", "webp", "svg", "ico", "bmp", "mp3",
                                       "mp4", "mkv", "ogg", "wav", "flac", "zip", "gz", "xz", "tar",
                                       "deb", "img", "iso", "bin", "pdf", "so", "7z", "woff", "woff2", "ttf"};
    std::string e = extension(url);
    for (auto x : exts)
        if (e == x) return true;
    return false;
}

Kind content_kind(const std::string& url, const std::string& content_type, const std::string& body) {
    std::string ext = extension(url), ct = lower(content_type);
    if (ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mkd" || ct.find("markdown") != std::string::npos)
        return Kind::Markdown;
    if (ct.find("html") != std::string::npos || ext == "html" || ext == "htm") return Kind::Html;
    if (starts_with(ct, "image/") || starts_with(ct, "audio/") || starts_with(ct, "video/") ||
        ct.find("zip") != std::string::npos || ct.find("octet-stream") != std::string::npos ||
        ct.find("pdf") != std::string::npos)
        return Kind::Unsupported;
    size_t n = std::min<size_t>(body.size(), 4096);
    if (std::memchr(body.data(), 0, n)) return Kind::Unsupported;
    std::string head = lower(trim(body.substr(0, 512)));
    if (starts_with(head, "<!doctype html") || starts_with(head, "<html")) return Kind::Html;
    return Kind::Text;
}

}  // namespace zs::web
