# kit

Code shared by several apps. It isn't an app: there's no package, and each app that uses a file
here compiles it in (listing it in its `app.cmake`, or including the header).

| File | Used by | What |
| --- | --- | --- |
| `suite.h` | Net Tools, RF Tools | One app holding several tools |
| `run.h`, `run.cpp` | Dev Tools, Files, Games, Net Tools, Notes | Run programs in the background; small file helpers |
| `ui.h`, `ui.cpp` | Dev Tools, Files, Games, Net Tools, Notes | A text-input screen, a title row, running a command in the Terminal |

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

## run.h

`simulated()` (true in the simulator), `have(prog)` (is a program installed), `quote()` for shell
arguments, a background runner that collects a program's output line by line without blocking
the screen, `capture()` for short commands, and file helpers: `list_dirs()`, `list_files()`,
`read_file()`, `write_file()`, `make_dirs()`.

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
