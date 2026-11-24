# Terminal

A real Linux terminal on the built-in screen, 53 columns by 14 lines, running
[tmux](https://github.com/tmux/tmux). Because it's tmux, whatever you start keeps running when you
go back to the home screen: open Terminal again and you're where you left off.

| Key | Does |
| --- | --- |
| Ctrl+Q or Alt+` | Back to the home screen; everything keeps running |
| `exit` | Close this shell (the last one closes the terminal) |
| Alt+N | A new window |
| Alt+Left / Alt+Right | Previous / next window |

tmux's own keys (Ctrl+B, then a key) work as well. The bar along the bottom lists the windows.

You're logged in as your user (`pi` unless you chose another). The first time, if the default
password is still set, you're asked to pick a new one first: see the OS README's
[Logging in](https://github.com/hartj77-blip/CardPuter-OS.4ME-U#logging-in).

Other apps use the Terminal too: Remote opens SSH sessions in it, and Dev Tools runs your code,
its editor and REPLs in it, each in a tmux window of its own. Removing Terminal takes one-key SSH
away from Remote (Remote says so).

The Terminal app is on the SD card image already. Like any app, you can remove it and install it
again from **Apps**.

## How it works

The app itself is small. Opening it asks ZeroShell to hand the screen to the console
(`shell_open_terminal()`): ZeroShell stops drawing and the OS's `zeroos-terminal.service` runs a
login on the Linux console, in tmux, until you detach. The service and its tmux settings are part
of ZeroOS, not this package, because SSH from Remote and the first-login password change need
them too.

Another app opens a command in the terminal with `shell_open_app("terminal", "<command>")`, which
starts it in a new tmux window.

In the simulator there's no console: the app shows what would run instead.
