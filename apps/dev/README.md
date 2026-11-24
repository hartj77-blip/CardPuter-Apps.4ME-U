# Dev Tools

Dev Tools turns the Cardputer Zero into a pocket workstation: write code on the keyboard, run it
with one key, keep it in Git and talk to the boards on your desk. It is one pack from the **Apps**
app that adds four apps to the home screen:

| App | What it does |
| --- | --- |
| **Code** | Your projects in `~/Code`: start one from a template, browse and edit its files, run it with **Ctrl+R** |
| **REPL** | An interactive prompt: Python, Lua, Node.js, a calculator (bc), SQLite or a shell |
| **Git** | Status, log and changes at a glance; commit, pull and push with one key; clone a repository |
| **Serial** | A serial monitor for Arduino, ESP32, Raspberry Pi Pico and other boards on the USB port or the Grove UART |

Editing, running and REPLs happen in the **Terminal** app (tmux on the built-in screen, 53 x 14),
so a program that asks for input works as usual. **Alt+`** comes back to the home screen and
leaves it running; open the app again to pick up where you were.

The pack brings `git`, the `micro` editor, `make`, `gcc`, Lua 5.4, `bc` and `sqlite3` (Python is
part of ZeroOS). Bigger toolchains are one `apt` away when a project needs them:
`sudo apt install nodejs`, `golang-go`, `cargo` or `g++`.

## Code

The projects and a project's files are lists: type to filter them, press **1** to **9** to pick a
row. Their other actions are in the menu (**Tab**, then the letter), or **Ctrl** + the letter
straight away. The bar along the bottom of the screen shows the keys that work right now.

| Key | Projects | Files of a project |
| --- | --- | --- |
| Enter | open the project | edit the file in micro (folders open) |
| N (menu) | new project | new file |
| R (menu) | | run the project |
| T (menu) | | a shell in this folder |
| G (menu) | | the project in Git |
| Backspace | | up a folder (once the filter is empty) |
| Esc | back | up a folder, then back to the projects |

A new project comes from a template: **Python**, **C** (with a Makefile), **Shell script**,
**Lua**, **Web page** or **Empty**, and starts as a Git repository.

**Run** (**Ctrl+R**) runs the project in the Terminal. The bottom line of the file list shows the
command it will run. Code picks it from the project's files, in this order:

| Project has | Run runs |
| --- | --- |
| `.zeroos-run` | the command on its first line (yours to choose: `python3 app.py --debug`) |
| `Makefile` | `make run` if it has a `run:` target, else `make` |
| `Cargo.toml` / `go.mod` / `package.json` | `cargo run` / `go run .` / `npm start` |
| `platformio.ini` | `pio run -t upload && pio device monitor` |
| `main.py` (or any `.py`) | `python3 main.py` |
| `main.sh` / `main.lua` / `main.js` | `bash`, `lua5.4`, `node` |
| `main.c` / `main.cpp` | compile with `cc` / `c++`, then run `./main` |
| `index.html` | `python3 -m http.server 8000`: open `http://<the device's address>:8000` on your phone |

A cloned project can bring its own `.zeroos-run`. The bottom line shows the command before you
run it, so read it first.

**micro** works like a desktop editor: **Ctrl+S** saves, **Ctrl+Q** quits, **Ctrl+F** finds,
**Ctrl+Z** undoes. Without micro, Code falls back to nano, then vi.

## REPL

Pick a language and press **Enter**. It opens in the Terminal, in `~/Code`. A language that isn't
installed shows which package brings it. SQLite opens `~/Code/scratch.db`, which is kept.

## Git

The list shows every Git repository in `~/Code`. **Clone a repository** at the bottom (or **C** in
the menu) asks for an address (`https://github.com/you/project.git` or `git@github.com:you/project.git`) and clones it
into `~/Code`.

In a repository the letters are shortcuts. The bar shows the first few, and **Tab** lists them
all:

| Key | In a repository |
| --- | --- |
| S | status: branch, ahead/behind, changed files |
| C | commit everything (`git add -A`), with a message you type |
| P | push (a new branch is pushed to `origin` and set to track it) |
| U | pull (fast-forward only: it never makes a merge for you) |
| L | the last 40 commits |
| D | what changed, file by file (**V** shows the full diff in the Terminal) |
| F | fetch |
| T | a shell in the repository |

Push moved from U to P, and pull from P to U, in Dev Tools 1.1.0.

Git here never stops to ask for a password. If a push or pull needs you to sign in, Git says so:
press **T** and run `git push` in the Terminal once. Better, set up a key so it never asks:

```sh
ssh-keygen -t ed25519            # in the Terminal; press Enter for the defaults
cat ~/.ssh/id_ed25519.pub        # add this key to GitHub / GitLab / your server
```

Then clone with the `git@...` address. The first commit asks who you are:

```sh
git config --global user.name "Your Name"
git config --global user.email you@example.com
```

## Serial

Plug a board into the USB-C port (with an OTG adapter or a USB-C cable) and open **Serial**.

| Key | Ports | Monitor |
| --- | --- | --- |
| Enter | open the port | send the line you typed |
| Left / Right | on **Speed**: 9600 to 921600 baud (115200 to start); on **Line ending**: what is sent after each line, `\r\n`, `\n` or none | |
| Ctrl + R | look for boards again | reset the board (pulses RTS, as `esptool` does) |
| Esc | back | close the port |

Both are also in the menu (**Tab**).

USB serial adapters (CP2102, CH340, FTDI) show as `ttyUSB0`, boards with native USB (Arduino
Leonardo, Pico, ESP32-S3) as `ttyACM0`, and the Grove/GPIO UART as `serial0`. The list shows each
board's USB name. Your user is in the `dialout` group, so no `sudo` is needed.

To flash an ESP32, close the monitor and use `esptool` in the Terminal (`sudo apt install esptool`),
or **Run** (**Ctrl+R**) in Code in a PlatformIO project.
