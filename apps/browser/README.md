# Browser

A web browser for the Cardputer Zero. It shows pages as text laid out for the 320 px screen, with
their pictures and forms: you can search, sign in to simple sites, and read the ZeroOS docs and
READMEs. There is no CSS or JavaScript, so sites that need JavaScript (Google's search results,
most web apps) won't work; plenty of the web still does.

Install it from **Apps**. It needs an internet connection for web pages; local files work offline.

## What it shows

| Page | How it looks |
| --- | --- |
| HTML | Its text, headings, lists, tables, links, pictures and forms. No styling or scripts |
| Markdown (`.md`, READMEs) | Formatted: headings, lists, tables, code blocks, quotes, links, pictures |
| Pictures (PNG, JPEG, GIF, BMP) | In the page, shrunk to fit the screen. A link to a picture opens it on its own |
| Anything else that is text | As it is, with line breaks kept |
| SVG, WebP, video, archives, PDFs | Not yet. Pictures it can't show become `[image: description]` |

Pictures are fetched after the page shows, and appear as they arrive. Icons, badges and tracking
pixels are left out. **I** turns pictures off (and on again): pages load faster without them.

Pages in Latin-1 or Windows-1252 (older sites, and some of Google's) are converted, so accents
show properly.

GitHub links open the file behind them: `github.com/owner/repo` opens that repository's README,
and a `.../blob/...` link opens the raw file. So docs come as Markdown, which reads well on a small
screen, instead of GitHub's full web page.

## Searching

**S** asks what to search for and searches DuckDuckGo (its Lite page, made for browsers without
JavaScript). The address box (**G**) searches too when what you type isn't an address: two or more
words, or one word with no dot. Google's home page loads and its search box works, but Google now
only shows results to browsers with JavaScript.

## Forms

Text boxes, password boxes, tick boxes, radio buttons, drop-down lists and buttons are links on
the page: move to one with **Left** / **Right** and press **Enter**.

| Control | Looks like | Enter |
| --- | --- | --- |
| Text box | `[hello      ]` | Type in it. In a search box (the form's only text box) Enter sends the form; otherwise it moves on to the next box |
| Password | `[*****      ]` | Type in it (hidden) |
| Tick box, radio button | `[x]`, `(*)` | Tick or untick, choose |
| Drop-down list | `[English v]` | A list to pick from |
| Button | `[ Search ]` | Sends the form (GET or POST) |

Cookies are kept, so a site you sign in to remembers you: they are in
`~/.config/zeroos/browser/cookies.txt`, readable only by you. Delete the file to sign out of
everything.

## Bookmarks and history

**B** bookmarks the page (or removes the bookmark); bookmarks are at the top of the home page
(**H**). **L** lists the pages you visited, newest first. Both are kept in
`~/.config/zeroos/browser/`.

## Keys

The soft-key bar along the bottom shows the main keys; **Tab** opens the menu (Ctrl + its letter
runs one straight away) and **0** shows every key. On a page, letters are shortcuts:

| Key | |
| --- | --- |
| Up / Down, J / K | Scroll a line |
| Fn + Page Down / Page Up | A screen down / up |
| Left / Right | Previous / next link or form control on the screen. At the edge, turns the page |
| Enter | Open the link, or use the form control |
| Esc, Backspace | Back. While a page is loading: stop |
| G | Type an address (`example.com`, `owner/repo`, `~/Notes`, `/etc/os-release`) or words to search for |
| S | Search the web |
| B | Bookmark this page (again: remove it) |
| L | History: the pages you visited |
| C | Contents: the page's headings, as a list. Type to narrow it, Enter (or 1 to 9) jumps to one |
| F | Find on the page. Enter again finds the next one |
| I | Pictures on / off |
| R | Reload |
| H | Home (bookmarks and the ZeroOS docs) |
| Home / End (Fn layer) | Top / bottom of the page |

Space does nothing on a page (Page Down is on the Fn layer).

## In the browser simulator

Browser works in the [browser simulator](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/)
too, with one limit the Cardputer doesn't have: a web page may only read sites that allow it
(CORS). GitHub READMEs and docs (the home page's bookmarks), raw files and many picture hosts
work; most other sites (Google, DuckDuckGo) show "The simulator can't open this site". There it
fetches with the page's own `fetch()` instead of curl, and doesn't keep cookies.

## Offline

Every page and picture that loads is kept in `~/.cache/zeroos/browser/`. Going back uses that
copy, and if a page can't be reached it shows the saved copy with an "Offline" note. Delete the
folder to clear it.

## For other apps

Another app can open a page or a file in Browser:

```cpp
shell_open_app("browser", "https://github.com/owner/repo");
shell_open_app("browser", "/home/pi/Documents/notes.md");
```

## How it works

- `page.cpp` turns Markdown, HTML or text into one list of blocks (headings, paragraphs, list items,
  code, table rows, pictures), with the page's form controls, then lays them out into lines that
  fit the 320 px screen. It has no LVGL in it, so it's unit tested on any PC (`tests/page_test.cpp`).
- `picture.cpp` decodes pictures with [stb_image](https://github.com/nothings/stb) (fetched by
  CMake at a pinned commit), shrinks them with a box filter and keeps them as RGB565, the screen's
  own format. A picture is laid out as strips one line high, so scrolling moves through it like text.
- `browser.cpp` draws only the lines on screen, so a long README uses a few dozen labels, not
  thousands.
- Pages are fetched with `curl` (part of ZeroOS) on a worker thread, and pictures on a second one,
  so the screen never freezes. Downloads stop at 4 MB, and a page shows at most 40 pictures.
- The UI font only has Latin characters, so arrows and box drawing become ASCII (`->`, `+--+`) and
  emoji are dropped.

## Not yet

CSS, JavaScript, SVG and WebP pictures, file uploads, and tabs.
