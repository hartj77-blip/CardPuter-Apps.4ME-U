# Browser

A text web browser for the Cardputer Zero. It started as a way to read ZeroOS's own docs and
READMEs on the device, and it's the first step towards a fuller browser.

Install it from **Apps**. It needs an internet connection for web pages; local files work offline.

## What it shows

| Page | How it looks |
| --- | --- |
| Markdown (`.md`, READMEs) | Formatted: headings, lists, tables, code blocks, quotes, links |
| HTML | The page's text, headings, lists, tables and links. No styling, scripts or forms |
| Anything else that is text | As it is, with line breaks kept |
| Images, archives, PDFs | Not yet. Images show as `[image: description]` |

GitHub links open the file behind them: `github.com/owner/repo` opens that repository's README,
and a `.../blob/...` link opens the raw file. So docs come as Markdown, which reads well on a small
screen, instead of GitHub's full web page.

The home page has bookmarks to the ZeroOS and Apps READMEs and every guide in `docs/`.

## Keys

| Key | |
| --- | --- |
| Up / Down | Scroll a line |
| Space / Backspace | Page down / up |
| Left / Right | Previous / next link on the screen. At the edge, turns the page |
| Enter | Open the link |
| Esc | Back. While a page is loading: stop |
| G | Type an address: `example.com`, `owner/repo`, `~/Notes`, `/etc/os-release` |
| C | Contents: the page's headings. Enter jumps to one |
| F | Find on the page. Enter again finds the next one |
| R | Reload |
| H | Home (bookmarks) |
| Home / End (Fn layer) | Top / bottom of the page |

Tab shows all of these on the device.

## Offline

Every page that loads is kept in `~/.cache/zeroos/browser/`. Going back uses that copy, and if a
page can't be reached it shows the saved copy with an "Offline" note. Delete the folder to clear it.

## For other apps

Another app can open a page or a file in Browser:

```cpp
shell_open_app("browser", "https://github.com/owner/repo");
shell_open_app("browser", "/home/pi/Documents/notes.md");
```

## How it works

- `page.cpp` turns Markdown, HTML or text into one list of blocks (headings, paragraphs, list items,
  code, table rows), then lays them out into lines that fit the 320 px screen. It has no LVGL in it,
  so it's unit tested on any PC (`tests/page_test.cpp`).
- `browser.cpp` draws only the lines on screen, so a long README uses a few dozen labels, not
  thousands. LVGL's memory for the whole UI is a fixed 512 KB.
- Pages are fetched with `curl` (part of ZeroOS) on a worker thread, so the screen never freezes.
  Downloads stop at 4 MB.
- The UI font only has Latin characters, so arrows and box drawing become ASCII (`->`, `+--+`) and
  emoji are dropped.

## Not yet

Images, forms, cookies and logins, JavaScript, bookmarks you add yourself, and a history list.
