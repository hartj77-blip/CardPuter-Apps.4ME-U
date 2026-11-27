# kit

Code shared by several apps. It isn't an app: there's no package, and each app that uses a file
here compiles it in (listing it in its `app.cmake`, or including the header).

| File | Used by | What |
| --- | --- | --- |
| `suite.h` | Net Tools, RF Tools | One app holding several tools |
| `run.h`, `run.cpp` | Dev Tools, Files, Libretro, Net Tools, Notes | Run programs in the background; small file helpers |
| `ui.h`, `ui.cpp` | Dev Tools, Files, Libretro, Net Tools, Notes | A text-input screen, a title row, running a command in the Terminal |
| `look.h` | Every app with its own colours | The UI 3 Monochrome palette and helpers |
| `tv.h`, `tv.cpp` | Jellyfin | A TV on the HDMI port without Dock mode: is one plugged in, claiming the port, a video on it |

**A change here is a change to every app that uses it.** `tools/check-versions.sh` asks for the
version of each app whose code names the changed file (`kit/run.` for `run.cpp` and `run.h`) to
go up, so devices get the fixed code.

## suite.h

A numbered menu of tools, and the chosen tool running inside the same app. Each tool is an
ordinary `App`; the suite builds it in its own root, passes it keys and ticks, and takes it down
when Esc leaves it. The menu takes **1** to **9**, Up/Down and Enter, or typing to filter, and
remembers the last tool used.

```cpp
std::unique_ptr<App> make_net_app() {
    return std::make_unique<kit::Suite>("net", std::vector<kit::Tool>{
        {"discover", "Discover", ZS_ICON_SEARCH, "Devices on the network", make_discover_app},
        {"ports",    "Ports",    ...},
    });
}
```

- One tool opens another with `kit::suite_open("ports", ip)`; the other reads it with
  `kit::tool_arg()` in `build()`. Esc goes back to the tool that opened it.
- Another app opens a suite straight at a tool: `shell_open_app("net", "ports:192.168.1.10")`.

It's header-only, so an app using it needs nothing added to its sources.

## look.h

The OS's UI 3 look, "Monochrome" (docs/UX.md in the OS repo): black, white and greys, the item in
focus lit white with black text, pop-ups as lifted sheets, square corners. Colour only says what
something is: `kGood` on, `kWarn` needs attention, `kInfo` in progress, `kBad` destructive or
recording.

Include it after `ui/theme.h`. In code inside `namespace zs { namespace { using namespace theme;`
the role names (`kAccent`, `kSurface`, `kMuted`, `style_panel` ...) then mean this look's values;
anywhere else write `look::kMuted`. Helpers: `look::button()`, `look::field()` (a text box),
`look::raised()` (a pop-up), `look::focus_fill()`.

The values are a copy of the OS's `ui/theme.h` from UI 3, so an app looks the same whichever OS
headers it was built against. It's header-only and calls nothing new in ZeroShell, so it doesn't
change the app level. When the OS drops the UI 2 names (UI 3 step 5), this goes and apps use
`theme.h` directly.

Not changed: Media's skins and Jellyfin's colours are those apps' own look, like an app icon, and
content keeps its colours (camera picture, radio spectrum, LED colours, game borders, QR codes).

## run.h

`simulated()` (true in the simulator), `have(prog)` (is a program installed), `quote()` for shell
arguments, a background runner that collects a program's output line by line without blocking
the screen, `capture()` for short commands, and file helpers: `list_dirs()`, `list_files()`,
`read_file()`, `write_file()`, `make_dirs()`.

## tv.h

For an app that uses a TV on the HDMI port itself, without Dock mode. List `../kit/tv.cpp` in the
app's sources.

- `tv_plugged()`: a monitor is on the HDMI port (sysfs; cheap, once a second is fine).
- `tv_claim(app_id)` / `tv_release()`: while the app uses the port, a monitor plugged in is for
  it. This writes `$XDG_RUNTIME_DIR/zeroos-hdmi-app`, which ZeroShell (newer than 0.5.1) reads
  so it doesn't open Dock over the app. Release only removes the app's own claim.
- `TvVideo`: plays a file or a stream on the TV. It runs mpv on the HDMI card alone (DRM, GPU
  scaling, H.264 in hardware, 720p or the TV's mode up to 1080p) and works it through mpv's IPC
  socket: `open()`, `poll()` each tick (it never waits), `state()` (loading, paused, position,
  duration, finished, error), `pause()`, `seek()`, `show_progress()` (mpv's bar on the TV),
  `close()`. mpv goes with ZeroShell if ZeroShell stops, so the TV is never left held. The
  built-in screen is another card, so the app's own screen carries on as the remote.

Sound comes out wherever the Cardputer's sound goes. Dock mode must not be running: its desktop
holds the same card (`hal().display().dock_running`). Libretro draws to the TV itself, frame by
frame (`apps/games/tv.h`), and doesn't use this yet.

`tests/tv_test.cpp` (built as `kit_tv_test` with Jellyfin) checks it against a fake sysfs
(`ZS_TV_SYSFS`) and a fake mpv.

## ui.h

`input_screen()` (a title, a one-line text box and a callback; it sets the screen's actions, so the
soft-key bar shows Enter and Esc, and the app sets its own again when it leaves the prompt),
`title_row()` (a title, with an optional muted note on the right, such as a count: never key
hints, the soft-key bar has those), `body_h(root)` (the app area's height now: shorter when the
screen has a soft-key bar), and `terminal_run(command, cwd)`, which runs a command in the Terminal
app in a new tmux window.

Every app follows the OS's interface spec, UI 2
([docs/UX.md](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/UX.md)): its
screens declare their actions with `shell_set_actions()` (the soft-key bar, the Tab menu and
Ctrl + a letter all come from that list), lists are `ListView`s (number keys pick, typing
filters), anything that can't be undone asks with `shell_confirm()`, and no app takes Tab, 0 or
the number keys for itself except on a Game screen.
