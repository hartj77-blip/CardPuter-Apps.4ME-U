# CardPuter-Apps.4ME-U

The apps for [ZeroOS](https://github.com/hartj77-blip/CardPuter-OS.4ME-U), the operating system
for the M5Stack Cardputer Zero.

ZeroOS itself ships with just four apps: **Apps**, **Settings**, **Update** and **Wi-Fi**. That's
enough to get online, keep the OS up to date and install things. Everything else is in this
repository, one folder per app: 26 packages, from a clock to a software-defined radio. You install
them from the **Apps** app on the device, and each one is versioned and released on its own, so
updating one app never touches another.

This is a community project. It isn't made, endorsed or supported by M5Stack.

- [Newest release of the apps](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/releases/latest)
- [Report a problem](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/issues)
- [The OS repository](https://github.com/hartj77-blip/CardPuter-OS.4ME-U)
- [Try every app in your browser](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/) ([the emulator's repository](https://github.com/hartj77-blip/emu-cardputer-os.4me-u))
- [M5Stack's app store on ZeroOS: compatibility checklist](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/M5-APPS.md)

| Repository | What's in it |
| --- | --- |
| [CardPuter-OS.4ME-U](https://github.com/hartj77-blip/CardPuter-OS.4ME-U) | The OS: the home screen (ZeroShell), the four built-in apps, the hardware layer, the SD card image, OS updates |
| This one | Every other ZeroOS app, the packages the Apps app downloads, and the index that lists them |
| [emu-cardputer-os.4me-u](https://github.com/hartj77-blip/emu-cardputer-os.4me-u) | The browser emulator: the real home screen and apps compiled to WebAssembly, [running here](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/) |
| [M5Stack's app store](https://github.com/CardputerZero/packages) | Not ours: about 180 community apps made for M5's own launcher, which ZeroOS can install too. [Which ones work on ZeroOS](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/M5-APPS.md) |

## Contents

1. [Status: what is tested and what isn't](#status-what-is-tested-and-what-isnt)
2. [Install an app](#install-an-app)
   - [Apps from M5Stack's app store](#apps-from-m5stacks-app-store)
3. [The apps](#the-apps)
   - [Everyday](#everyday)
   - [Music, pictures and games](#music-pictures-and-games)
   - [Networks and other computers](#networks-and-other-computers)
   - [Extra hardware](#extra-hardware)
   - [Code and AI](#code-and-ai)
4. [Where your files go](#where-your-files-go)
5. [What keeps running after you leave an app](#what-keeps-running-after-you-leave-an-app)
6. [Troubleshooting](#troubleshooting)
7. [How it works](#how-it-works)
   - [An app is a module](#an-app-is-a-module)
   - [What an app can use](#what-an-app-can-use)
   - [One app, several tools: the kit](#one-app-several-tools-the-kit)
   - [An app is also a Debian package](#an-app-is-also-a-debian-package)
   - [The .pkg file](#the-pkg-file)
   - [The index](#the-index)
   - [App levels: staying compatible with ZeroOS](#app-levels-staying-compatible-with-zeroos)
   - [How a release is made](#how-a-release-is-made)
8. [Security and what you are trusting](#security-and-what-you-are-trusting)
9. [Testing](#testing)
10. [Repository layout](#repository-layout)
11. [Building it yourself](#building-it-yourself)
12. [Work on an app](#work-on-an-app)
13. [Write a new app](#write-a-new-app)
14. [Run your own app repository](#run-your-own-app-repository)
15. [The pinned OS](#the-pinned-os)
16. [Licence and third-party code](#licence-and-third-party-code)

## Status: what is tested and what isn't

The apps are young, like ZeroOS, and are being written faster than they can be tried on hardware.
This is where things stand.

| | State |
| --- | --- |
| Every app builds for arm64, as a module and into the PC simulator | Yes, on every pull request |
| Every app (and every tool inside Net Tools and RF Tools) opens without crashing | Yes, in CI, in a headless build that saves a screenshot of each |
| Every app loads as a module into a real ZeroShell | Yes, in CI |
| Every app installs, side by side with all the others, and removes cleanly with the real apt on Debian trixie arm64 | Yes, in CI, in a container with Raspberry Pi's repository added |
| The apps still build against the next ZeroOS (its `main` branch) | Yes, in CI, as a second build of everything |
| Unit tests for the parts that can run without a screen (AI requests, Browser's page engine, Desktop's host list, Grove's LED effects, RF Tools' DSP, Router's script) | Yes, in CI |
| The apps on a real Cardputer Zero | Mostly not yet. Owners have run some; most have only been run in the simulator and CI |
| Hardware add-ons: GPS module, Cap LoRa-1262, LED strip, RTL-SDR dongle, USB stick mode in a car, USB networking to a laptop | Not checked on a device yet. Each app's README has a "to confirm" list |

In short: every app is checked by machines on every change, but very few have been held in a
hand. If something doesn't work on your device,
[open an issue](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/issues) with the app's
version (**Apps**, the app's page) and your ZeroOS version (`cat /etc/zeroos-release`).

## Install an app

On the Cardputer, open **Apps**, pick one and press **Enter**. The app downloads, installs with
whatever it needs from Debian, and appears on the home screen. Press **U** in Apps to update every
app that has a newer version (**Tab > U**, or **Ctrl + U**). Removing an app from Apps also removes the extra software it
brought, unless another app still needs it.

Apps come from this repository's
[newest release](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/releases/latest). The
Apps app needs an internet connection; see
[Getting online](https://github.com/hartj77-blip/CardPuter-OS.4ME-U#getting-online) in the OS's
README.

**Terminal**, **Files** and **Dock** are already on the SD card image. They are ordinary apps
from here, so you can remove and reinstall them like any other.

If your device doesn't list this repository yet (ZeroOS 0.3.4 and older), add it once:
**Apps > S > Add a GitHub repo**, and type `hartj77-blip/CardPuter-Apps.4ME-U`. Or from a terminal:

```sh
sudo zeroos-apps add-source hartj77-blip/CardPuter-Apps.4ME-U
```

From a terminal you can also do everything the Apps app does:

```sh
zeroos-apps list                          # what's available, and what's installed
sudo zeroos-apps install media            # install one app by its id
sudo zeroos-apps remove media
sudo zeroos-apps upgrade                  # every app with a newer version
zeroos-apps sources                       # where apps come from, most trusted first
sudo zeroos-apps source-up <owner/repo>   # move a source up the list (source-down: down)
sudo zeroos-apps install-file zeroos-app-media_2.0.0-abi4_arm64.deb   # a package you copied over
```

The id of each app is in the tables below (the folder name).

### Apps from M5Stack's app store

M5Stack runs its own app store for the Cardputer Zero: about 180 community apps, listed in
[CardputerZero/packages](https://github.com/CardputerZero/packages) and reviewed there before
they are published. They are made for M5's own launcher, which ZeroOS replaces, but they are
ordinary Debian packages, so ZeroOS installs them too. This needs a ZeroOS newer than 0.4.0.

- **In Apps** they're in the same list as the apps from this repository, marked **M5**, with ids
  `m5-<package>`. To see only M5's apps, press **Left** or **Right** on the source row at the top
  of Apps (or **Tab > S**) until it says **M5Stack** (ZeroOS 0.5.1 or newer). An app's page says it's from M5Stack's store, and shows the notes M5's store
  gives ("Runs in the background", "Needs extra hardware").
- **Installing** reads M5's list (`registry.json`, published from CardputerZero/packages), checks
  the download against the SHA-256 it names, and installs it with apt, the same way as any app.
  Update all and Remove work the same too.
- **Opening one** hands it the whole screen, the way Camera works: ZeroShell stops drawing and the
  app draws on the LCD and reads the keyboard itself. **Hold Esc for 3 seconds** to leave an app
  that has no way out of its own. Apps marked to run in a terminal open in the Terminal.

How they differ from the apps here:

| | Apps from this repository | Apps from M5Stack's store |
| --- | --- | --- |
| What it is | A ZeroShell module, drawn by the home screen | A separate program (Python, LVGL, SDL...) that takes over the screen |
| Checked by | This repository's CI, on every change | M5's community review. Not by this project: ZeroOS only checks each download is the file M5's list names |
| Built for | ZeroOS, rebuilt for each app level | M5's own system and launcher. Most only need the screen and keyboard and work; one that talks to M5's launcher or its services won't |
| Icon | Its own | Its first letter, for now |
| After you leave it | Stops, apart from [these](#what-keeps-running-after-you-leave-an-app) | Background apps (a battery saver with a service) keep running until you remove them |

M5's store is the last source in the list, so if an app here and an app there ever shared an id,
this repository's would win (they can't, in practice: M5's ids all start with `m5-`). To take
M5's store out of the list: **Apps > Tab > P**, highlight it, **Tab > D**. On the same page,
**Tab > K** and **Tab > J** move a source up and down (ZeroOS 0.5.1 or newer). To put it back, **Add
a GitHub repo** and type `m5stack`. How it works on the device is in the OS's
[apps guide](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/APPS.md#m5stacks-app-store).

Not every M5 app works on ZeroOS. The OS repository keeps a
[compatibility checklist](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/M5-APPS.md) of all of them: what a look inside each package
found, and what has been checked on a real Cardputer.

## The apps

Every app is keyboard-only, like the rest of ZeroOS: arrow keys move, **Enter** opens, **Esc**
goes back. Most screens show their keys in a bar along the bottom, and **Tab** opens a menu with
everything the screen can do. Apps with a README have a full guide; the rest are short enough to
describe here.

The **Brings** column is what the package installs from Debian (or another repository) alongside
the app. It all goes again when you remove the app.

### Everyday

| App | Id | What it does | Brings |
| --- | --- | --- | --- |
| [Terminal](apps/terminal/README.md) | `terminal` | A Linux terminal on the built-in screen (53 x 14), in tmux, so sessions keep running when you leave | nothing (tmux is in ZeroOS) |
| [Files](apps/files/README.md) | `files` | Browse the device's files; rename, copy, move and delete them. Opens text in Notes, plays music, plays video on a monitor, sends a file with Taildrop | nothing |
| [Notes](apps/notes/README.md) | `notes` | Plain-text notes in `~/Notes` that save as you type: a list of notes, find in a note, share with Taildrop | nothing |
| [Browser](apps/browser/README.md) | `browser` | Browse the web on the small screen: pages with their pictures (PNG, JPEG, GIF), forms and sign-ins, DuckDuckGo search, bookmarks and history, the ZeroOS docs, Markdown and local files. Keeps pages to read offline. No JavaScript | nothing (pages are fetched with curl, which is in ZeroOS) |
| [Clock](apps/clock/README.md) | `clock` | Time and date from the battery-backed clock, and a stopwatch with laps | nothing |
| [Battery](apps/battery/README.md) | `battery` | Charge, time left, power draw, battery health, and the power profile | nothing |
| [System](apps/sysmon/README.md) | `sysmon` | CPU, memory and what uses it, storage, temperature, versions | nothing |
| [Compass](apps/compass/README.md) | `compass` | Compass and spirit level (standard model; the Lite has no motion sensor) | nothing |

### Music, pictures and games

| App | Id | What it does | Brings |
| --- | --- | --- | --- |
| [Media](apps/media/README.md) | `media` | A music player in the style of the classic desktop MP3 players, over a file explorer, with pixel-art skins (a 1970s hi-fi, liquid chrome) and classic Winamp skins (`.wsz`): music on the SD card, USB drives and mounted server shares (SMB, NFS), and http(s) addresses | mpv |
| [Jellyfin](apps/jellyfin/README.md) | `jellyfin` | Music, movies and shows from every library on your Jellyfin server, in Jellyfin's own look: home screen cards, Continue Watching, a now-playing bar | mpv, libmpv |
| [Recorder](apps/recorder/README.md) | `recorder` | Voice notes from the microphone: record, play back, rename, delete | sox |
| [Camera](apps/camera/README.md) | `camera` | M5Stack's camera app (viewfinder, photos, gallery), and a QR code reader that can join Wi-Fi. Standard model only | rpicam-apps-lite, M5Stack's CameraApp |
| [Games](apps/games/README.md) | `games` | NES, Game Boy and GBA games on the built-in screen or a TV on the HDMI port; SNES, Mega Drive, PlayStation and more with downloadable libretro cores. Two (up to four) players with USB or Bluetooth gamepads. Borders around the picture, fast forward, and carry on where you left off. Bring your own games | libretro cores for Nestopia, Gambatte and mGBA; curl, unzip |
| [Car](apps/car/README.md) | `car` | Music in the car: the Cardputer as a USB stick of your music, Bluetooth audio with the car's buttons, or your phone's music through the Cardputer to the aux | mtools, dosfstools, fdisk |

### Networks and other computers

| App | Id | What it does | Brings |
| --- | --- | --- | --- |
| [Remote](apps/remote/README.md) | `remote` | Tailscale or your own Headscale server: sign in with a QR code or an auth key, SSH or mosh to your devices, remote desktop, Taildrop files into `~/Downloads` | tailscale (from Tailscale's repository), mosh, FreeRDP |
| [Desktop](apps/desktop/README.md) | `desktop` | Remote desktop to a PC or Mac (RDP or VNC), on the built-in screen or full size on a monitor | libvncclient, FreeRDP 3, gvncviewer |
| [Dock](apps/dock/README.md) | `dock` | Plug into an HDMI monitor and get a desktop: browser, video player, terminal, file manager | nothing (the desktop is part of ZeroOS) |
| [Sync](apps/sync/README.md) | `sync` | Keep Notes, Documents and Recordings the same on your PC and phone, with Syncthing | syncthing, python3 |
| [Tether](apps/tether/README.md) | `tether` | Get online through your phone: Wi-Fi hotspot, USB or Bluetooth | usbmuxd, python3 |
| [Router](apps/router/README.md) | `router` | Plug in a laptop (Ethernet or USB-C) and send all its traffic through a Tailscale exit node, with a kill switch. Needs Remote | python3, nftables, dnsmasq |
| [Net Tools](apps/net/README.md) | `net` | Seven tools: Discover devices, scan Ports, Trace routes, DNS, packet Capture, Netinfo, and Share the Ethernet port with a laptop | arp-scan, nmap, mtr, dig, tcpdump, iproute2, dnsmasq, nftables |

### Extra hardware

| App | Id | What it does | Needs | Brings |
| --- | --- | --- | --- | --- |
| [IR Remote](apps/ir/README.md) | `ir` | A universal remote with about 2,000 TVs, projectors, sound bars, AC units and lights, and remotes you teach it | nothing (the IR LED and receiver are built in) | v4l-utils, ir-keytable, the code database |
| [GPS](apps/gps/README.md) | `gps` | Position, speed, saved spots and GPX track recording | the Cap LoRa-1262 (it has GPS), a Grove GPS unit, or a USB GPS | gpsd |
| [Mesh](apps/mesh/README.md) | `mesh` | Meshtastic text messages over LoRa, with no phone signal or internet | the Cap LoRa-1262 | meshtasticd (from Meshtastic's repository) |
| [Grove](apps/grove/README.md) | `grove` | Light a NeoPixel (WS2812 / SK6812) LED strip on the Grove port, with effects | an LED strip on the Grove port | its own LED service |
| [RF Tools](apps/rf/README.md) | `rf` | Nine tools for a USB software-defined radio: Spectrum, Scanner, Sensors (433 MHz weather stations), Aircraft (ADS-B), APRS, FT8, Logbook, SDR Server and Station | an RTL-SDR dongle (RTL-SDR Blog V4 first) | rtl-sdr, rtl-433, multimon-ng, Hamlib, sox |

Power the Cardputer off before you plug anything into the Grove or expansion port: neither is
hot-swap. The OS's [expansion guide](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/EXPANSION.md)
covers the Cap LoRa-1262 and Grove GPS units.

### Code and AI

| App | Id | What it does | Brings |
| --- | --- | --- | --- |
| [Dev Tools](apps/dev/README.md) | `dev` | One package, four icons: **Code** (projects in `~/Code`, run with one key), **REPL** (Python, Lua, Node.js, bc, SQLite, a shell), **Git** (status, commit, pull, push, clone) and **Serial** (a serial monitor for Arduino, ESP32 and Pico boards) | git, micro, make, gcc, Lua 5.4, bc, sqlite3 |
| [AI](apps/ai/README.md) | `ai` | Chat with Claude, ChatGPT, Gemini, OpenRouter, Groq, Mistral, a local Ollama server or any OpenAI-compatible server, using your own API key | nothing (requests go through curl, which is in ZeroOS) |

## Where your files go

Apps keep your things in plain folders in your home folder, so you can reach them from Files, the
Terminal, SSH, or another computer.

| Folder or file | What's in it | Written by |
| --- | --- | --- |
| `~/Notes/` | Your notes (`notes.txt` and any more you make) | Notes |
| `~/Documents/waypoints.txt` | Spots you saved, one per line | GPS |
| `~/Documents/Tracks/` | Recorded tracks, as GPX | GPS |
| `~/Documents/logbook.adi` | Your radio contacts, as ADIF | RF Tools (Logbook, FT8) |
| `~/Recordings/` | Voice notes (16 kHz WAV), and packet captures (`.pcap`) | Recorder, Net Tools (Capture) |
| `~/Music/` | Music to play, and what Car's USB stick mode copies | you; Media and Car read it |
| `~/Skins/` | Classic Winamp skins (`.wsz`) for Media | you; Media reads it |
| `~/Pictures/DCIM/Camera/` | Photos | Camera |
| `~/Downloads/` | Files sent to the Cardputer with Taildrop | Remote |
| `~/Games/` | Your games (a folder per console), saves, downloaded cores | you, Games |
| `~/Games/states/` | Save states, and `<game>.resume.state` to carry on where you left off | Games |
| `~/Games/borders/` | PNG border pictures (`<game>.png`, `<console>.png` or `default.png`) | you; Games reads it |
| `~/Games/settings` | Games' border, picture, carry-on and TV settings | Games |
| `~/Code/` | Your projects | Dev Tools |
| `~/IR/` | Remotes you taught or saved | IR Remote |
| `~/.cache/zeroos/browser/` | Pages and pictures you opened, to read offline. Safe to delete | Browser |
| `~/.config/zeroos/browser/` | Browser's bookmarks, history, settings and cookies (`cookies.txt` holds your sign-ins; mode 600) | Browser |
| `~/.config/zeroos/ai.conf` | AI's settings and API keys (mode 600) | AI |
| `~/.config/zeroos/desktop-hosts` | Computers you saved in Desktop | Desktop |
| `~/.config/zeroos/rf-memories.tsv` | Saved frequencies | RF Tools |
| `~/.config/zeroos/settings.conf` | Small settings: Jellyfin server and sign-in token, Grove's last effect, your callsign, Router's mode | several apps, through ZeroOS |

Removing an app never deletes these. Sync shares **Notes**, **Documents** and **Recordings** with
your other devices, so if Sync is on, packet captures and your logbook travel too.

## What keeps running after you leave an app

Most apps stop completely when you press Esc. A few are meant to keep going, and these are the
ones to know about for battery life.

| App | What keeps going | How to stop it |
| --- | --- | --- |
| Terminal, Dev Tools | Your tmux session, and anything running in it | `exit` in each window, or `tmux kill-server` |
| Media, Jellyfin, Recorder | The track or note that's playing | Stop in the app, or the media keys |
| GPS | A track you're recording (and the expansion port's power, for the GPS) | **R** in GPS |
| Mesh | The radio (`meshtasticd`) and the expansion port's power | **Turn off mesh** in Mesh |
| Grove | The LED strip stays lit | Open Grove, press **Space** |
| Remote | Tailscale, by default for 15 minutes after you last used it | Settings > Tailscale |
| Sync | Syncthing, by default only while charging | Settings > Sync |
| Router | Routing for the laptop, until you turn it off | Turn it off in Router |
| Car | USB stick mode, until you turn it off | Enter on the row again in Car |

## Troubleshooting

**An app isn't in the list in Apps.** Check the device is online, then press **Tab > R** in Apps
to reload the list. If none of these apps show at all, your device may not have this repository as a
source yet: see [Install an app](#install-an-app). An app built for a different ZeroOS than yours
isn't shown; update ZeroOS with the **Update** app.

**"Update all" in Apps offers nothing after a ZeroOS update.** The apps for a new ZeroOS are
usually built before it's released. If they aren't there yet, the release workflow catches up
within 3 hours (see [App levels](#app-levels-staying-compatible-with-zeroos)).

**An app installed but isn't on the home screen**, or opening it does nothing. ZeroShell only
loads a module built for its own app level. Check the journal for the reason:

```sh
journalctl -b -u zeroshell | grep -i module
```

**An app's install fails.** The Apps app shows the last lines of apt's output. The whole log is in
the journal:

```sh
journalctl -b -u 'zeroos-apps@*'
```

Most failures are network ones (a dropped connection, a captive Wi-Fi portal). Try again. If an
app that needs another repository (Remote, Mesh) fails, check the device's date and time: apt
refuses repository signatures from "the future".

**An app crashes.** Please open an issue with the app's version and what you did. The crash is in `journalctl -b -u zeroshell`.

**Hardware isn't found** (GPS, LoRa, LED strip, dongle). Power off, reseat the module, power on.
Each of those apps' READMEs has a "to confirm on hardware" section listing what is still
unchecked, and RF Tools has `zeroos-rf-bringup` to run over SSH.

## How it works

This section is for people who want to change an app, write one, or just know what they are
installing. The OS side (how ZeroShell loads modules, how `zeroos-apps` installs them) is
described in more depth in the OS README's
[How apps are loaded](https://github.com/hartj77-blip/CardPuter-OS.4ME-U#how-apps-are-loaded)
and [How apps are installed](https://github.com/hartj77-blip/CardPuter-OS.4ME-U#how-apps-are-installed).

### An app is a module

ZeroShell, the home screen, is a C++ program drawing with [LVGL](https://lvgl.io). Every app here
is a C++ class that ZeroShell creates when you open the app and destroys when you leave.

On the device, each app is compiled into a shared library, `/usr/lib/zeroos/apps/<id>.so`. A
module links nothing itself: LVGL, the hardware layer and the shell's own functions all come from
the `zeroshell` binary, which exports them. That keeps modules small and means every app uses exactly the LVGL the shell does.

An app is listed once, in its folder's `app.cmake`:

```cmake
zs_app(clock "Clock" ZS_ICON_CLOCK 0x4a5bd6 make_clock_app ORDER 170
       SOURCES clock.cpp)
```

That's the id, the name on the home screen, the icon, the icon's colour, the function that makes
the app, its place on the home screen, and its source files. `zs_app()` comes from the OS
(`base/shell/cmake/ZeroShellApps.cmake`). It generates a small file that exports one C function,
`zs_app_module()`, returning that information plus the app level the module was built for.
ZeroShell loads the module with `dlopen(RTLD_NOW)`, so a missing symbol is caught at load time,
and refuses it if its level isn't the shell's.

A package can hold more than one app: Dev Tools' `app.cmake` lists Code, REPL, Git and Serial
with `MODULE dev`, so all four go into `dev.so` and get four icons.

In the simulator and headless builds there are no modules: every app is compiled straight into
one `zeroshell` binary, together with the OS's own four apps.

### What an app can use

The whole interface an app has is in a few OS headers, included through `apps/common.h`:

```cpp
class App {
public:
    virtual void build(lv_obj_t* root) = 0;      // draw your screen into root (320 x 150)
    virtual void tick() {}                       // about 10 times a second
    virtual bool on_key(uint32_t key);           // a key, before LVGL sees it; true = handled
    virtual bool on_back();                      // Esc; true = handled inside the app
    lv_group_t* group;                           // the focus group, for lists and text boxes
};
```

| From | What |
| --- | --- |
| `ui/app.h` | `App`, and the shell's services: `shell_toast()`, `shell_open_app(id, arg)` (open another app and hand it a word), `shell_app_arg()`, `shell_open_terminal(cmd)`, `shell_set_actions()` (the soft-key bar and the Tab menu), `shell_confirm()` (the one way ZeroOS asks before something destructive) |
| `hal/hal.h`, as `hal()` | The hardware: battery, motion sensor, GPS (through gpsd), the mesh radio, audio playback, the expansion port's power, network status, system figures, Dock |
| `ui/theme.h` | Colours, fonts (`zs_font_12` to `zs_font_28`), screen sizes, `label()`, `make_list()`, `kv_row()` and other helpers, so every app looks the same |
| `ui/prefs.h` | `prefs::get_str()` and `set_str()`: small settings, kept in `~/.config/zeroos/settings.conf` |
| `ui/fileio.h` | `write_file_atomic()`: write a temp file and rename it, so a power cut never leaves a half-written file |
| `ui/power.h` | The power profile, time left, whether Dock is on |

The hardware layer has a simulated side, so an app runs on a PC with no Cardputer: the battery,
the motion sensor, the GPS and the mesh radio all return made-up readings there.

Apps keep the screen responsive by never blocking in `build()`, `tick()` or `on_key()`. Anything
slow (a network request, a scan, a download) runs as a separate program or on a thread, and the
app looks at it from `tick()`.

### One app, several tools: the kit

`apps/kit/` is code that more than one app uses. It isn't an app and has no package; each app
that uses it compiles it in.

| File | Used by | What |
| --- | --- | --- |
| `kit/suite.h` | Net Tools, RF Tools | One app holding several tools: a numbered menu (1 to 9, or type to filter), each tool an ordinary `App` run inside the suite. Esc goes back to the menu. `shell_open_app("net", "ports:192.168.1.10")` opens a suite straight at a tool |
| `kit/run.h`, `run.cpp` | Dev Tools, Files, Games, Net Tools, Notes | Run a program in the background and read its output line by line, without blocking the screen; small file helpers |
| `kit/ui.h`, `ui.cpp` | Dev Tools, Files, Games, Net Tools, Notes | A text-input screen, a title row, running a command in the Terminal app |

A change to a kit file counts as a change to every app that uses it: CI asks for each of those
apps' versions to go up.

### An app is also a Debian package

What the device downloads is an ordinary Debian package, `zeroos-app-<id>_<version>_arm64.deb`,
made by `tools/build.sh` with the OS's `base/image/apps/make-app-deb.sh`. It holds:

- the module, in `/usr/lib/zeroos/apps/`;
- whatever the app's `payload()` adds: systemd services, helper scripts, udev and polkit rules,
  a sudoers rule, a data file;
- `Depends:` on `zeroos`, on `zeroos-app-abi-<level>` (see [App levels](#app-levels-staying-compatible-with-zeroos))
  and on the Debian packages the app needs.

Because it's a normal package, apt does the hard parts: it installs the dependencies, refuses an
app built for a different ZeroOS, and on removal takes away everything the app brought unless
another app still uses it. The package's own install scripts (generated, the same for every app)
do only this: enable or disable the services the `.pkg` names, put back any settings file the app
replaced, and reload systemd and udev.

### The .pkg file

Each app's `<id>.pkg` says what the package is. It is a bash fragment, read by
`make-app-deb.sh`. The smallest:

```sh
NAME="Clock"
SUMMARY="Clock and stopwatch"
APP_VERSION="1.0.0"
```

Everything it can set:

| Field | What it does | Example |
| --- | --- | --- |
| `NAME` | Name shown in Apps | `"Net Tools"` |
| `SUMMARY` | One line, shown in Apps | |
| `APP_VERSION` | The app's version. Must go up for a device to take an update | `"2.0.0"` |
| `DEPENDS` | Debian packages it needs, in `Depends:` syntax | `"mpv, libmpv2"` |
| `NEEDS` | What Apps shows as "needs", if `DEPENDS` reads badly | `"mpv"` |
| `ENABLE` | systemd units to enable (and start) on install, disable on removal | `"gpsd.socket"` |
| `DISABLE_DEPS` | Units a dependency brings that should *not* start at boot | `"tailscaled.service"` |
| `STOP_ON_REMOVE` | Units to stop when the app is removed | `"meshtasticd.service"` |
| `REPLACES` | Ids of apps this one takes over (a renamed app) | `"zclaw"` |
| `EXTRA_FILE`, `EXTRA_URL`, `EXTRA_SHA256` | A second package `zeroos-apps` downloads and checks before installing the app (Camera: M5Stack's CameraApp) | |
| `HOMEPAGE` | Link in the package's control file | |
| `payload()` | A bash function that copies extra files into the package. `$D` is the package root, `$F` the app's `files/` folder, `$WORK` what `prepare.sh` made, `$ZEROOS_BUILD` the build folder | see `apps/router/router.pkg` |

An app's optional `prepare.sh` runs before packing to fetch or build what `payload()` needs:
IR Remote builds its code database from Flipper-IRDB, Remote fetches Tailscale's signing key,
Mesh fetches Meshtastic's key and checks it against a pinned fingerprint.

A settings file that belongs to another package (GPS replaces gpsd's `/etc/default/gpsd`) goes in
the package under `/usr/share/zeroos/app-overrides/<id>/`. Installing copies it into place and
keeps the original as `<file>.zeroos-old`; removing the app puts the original back.

### The index

A release holds every package and one text file, `zeroos-apps.index`, which is what a device
reads. One block per package:

```
app=media
name=Media
summary=Music player with a classic look, music and video from every Jellyfin library
version=2.0.0-abi4
abi=4
file=zeroos-app-media_2.0.0-abi4_arm64.deb
sha256=<64 hex characters>
size=<bytes>
needs=mpv
```

On the device, `zeroos-apps` parses it with awk; it is never run or sourced. Blocks that aren't
exactly the expected shape are dropped, so are blocks for another app level, and every download
is checked against its `sha256` before apt sees it.

### App levels: staying compatible with ZeroOS

A module is compiled against the OS's headers, so it has to match the ZeroShell on the device.
ZeroOS numbers that with an **app level**: `ZS_APP_ABI` in the OS's `shell/src/ui/app.h`
(currently 4). The OS raises it, in the same change, whenever it changes a header in a way that
would break already-built apps. The rules:

1. **One package, one level.** A package's version ends in its level: `1.2.0-abi4` is version
   1.2.0 built for level 4. It depends on `zeroos-app-abi-4`, which only a matching ZeroOS
   provides, so apt itself won't install it anywhere else.
2. **The index can list an app once per level.** A device only sees the builds for its own level.
3. **This repository follows the OS by itself.** Every 3 hours the Release workflow looks at the
   OS repository. It builds for the level of the newest ZeroOS release, and also for the level on
   the OS's `main` branch when that's different. So the apps for the next ZeroOS are published
   before that ZeroOS is.
4. **The level below is kept.** Builds for the previous level are carried over from the previous
   release, so a device that hasn't updated ZeroOS yet still sees its apps.

In practice:

| Situation | What happens |
| --- | --- |
| An app changes here | Merging publishes it. Devices see an update for that app only |
| The OS raises the level on `main` | Within 3 hours the apps are built for the new level too. Nothing changes for devices yet |
| That ZeroOS is released | Devices that update find the new builds waiting. **U** in Apps installs them |
| An app no longer builds against OS `main` | CI here goes red on the "main" build. Fix the app before the OS is released |

If the timed run hasn't caught up, start it by hand: **Actions > Release > Run workflow**. GitHub
pauses timed workflows after 60 days without a commit; running it by hand wakes it.

### How a release is made

`.github/workflows/release.yml`, step by step:

1. **Plan** (`tools/levels.sh`): ask the OS repository for its newest release and the app level
   of that release and of `main`. On a timed run, stop here unless that differs from what the
   newest release of the apps was built for (it is written at the top of the index).
2. **CI**: the whole of `ci.yml` runs. Nothing is published unless it passes.
3. **Build**, once per level, on an arm64 runner in a Debian trixie container (what the Cardputer
   runs): check out the OS at that release (or `main`), build every module for the Cardputer's
   Cortex-A53, strip them, and pack them with `tools/build.sh`. Building on arm64 trixie rather
   than cross-compiling matters: Desktop's viewer links Debian's VNC and RDP libraries, and RF
   Tools' radio daemon must link Debian's `librtlsdr.so.0`, not Ubuntu's.
4. **Publish** (`tools/assemble.sh`): put every level's packages together, carry over the level
   below from the previous release, write one index, check that every package it lists is there
   and matches its checksum, and publish it as a new release, `apps-<date>-<run>`, marked latest.
   The release notes list every app and its version.

A release is triggered when a change under `apps/`, `tools/`, `CMakeLists.txt` or the OS pin
lands on `main`; every 3 hours if ZeroOS moved on; or by hand.

## Security and what you are trusting

Installing an app runs its package's scripts as root, and an app runs inside ZeroShell with the
same access as the home screen (your user, with the groups it needs for the screen, audio,
network, I2C, GPIO and Bluetooth). So installing an app means trusting this repository. This is
what that involves, including the parts that aren't as strong as they could be.

**Where packages come from.** Packages are built by GitHub Actions from this repository's `main`
branch and published as a GitHub release. The index's SHA-256 checksums prove a download is
complete and is the file the index names, but the index comes from the same release, so it says
nothing about who made it. You are trusting whoever can merge into this repository, and GitHub.
Signing the index with a key kept off GitHub is not done yet.

**Other repositories apps add.** Two apps add a Debian repository of their own, because what they
need isn't in Debian, with that repository's signing key in their package:

| App | Repository | Key |
| --- | --- | --- |
| Remote | `pkgs.tailscale.com` (Tailscale) | Fetched from Tailscale over HTTPS at build time |
| Mesh | Meshtastic's openSUSE Build Service repository | Fetched at build time and checked against a pinned fingerprint (`apps/mesh/prepare.sh`) |

Removing the app removes the repository and its key. While it's installed, apt trusts that
repository for the packages it holds.

**Downloads from elsewhere.** Camera downloads M5Stack's CameraApp package from its GitHub
release, checked against a SHA-256 pinned in `camera.pkg`. Games downloads extra emulator cores
from libretro's nightly builds (`buildbot.libretro.com`) when you ask it to, over HTTPS, with no
checksum to check against: they are native code that runs inside ZeroShell. The cores that come
with Games are Debian's.

**Extra permissions some apps add**, all removed with the app:

| App | What it adds | Why |
| --- | --- | --- |
| Net Tools | File capabilities on `arp-scan` (`cap_net_raw`) and `tcpdump` (`cap_net_raw,cap_net_admin`), which become `root:netdev 0750`, so members of `netdev` can scan and capture. Nothing runs as root. Removing the app puts both back to `root:root 0755` | Discover and Capture need raw sockets. Version 2.0 used a sudo rule instead, which amounted to passwordless root (`sudo tcpdump -z` runs any program); 2.1 removes it, so upgrade |
| Router | A polkit rule letting your user start and stop `zeroos-router@ethernet` and `zeroos-router@usb` | Router turns routing on and off without asking for a password |
| RF Tools | A udev rule giving your user the RTL-SDR dongle, and a modprobe rule keeping the kernel's DVB-T driver off it | So the radio daemon runs as you, not as root |
| Car | Loads the `uinput` and `libcomposite` kernel modules at boot, and adds WirePlumber settings (Bluetooth audio without a login; no hands-free role for phones) | The car's buttons become key presses; the Cardputer can act as a USB stick; Bluetooth audio works from a service, and a phone's calls stay on the phone |

**Your data.** AI's API keys are in `~/.config/zeroos/ai.conf` with mode 600, and are handed to
curl through a private temporary file so they never appear in the process list. The Jellyfin app uses your
password once to get a sign-in token, keeps the token, and doesn't keep the password.
Sync shares only the three folders it names. Nothing in these apps sends usage data anywhere.
AI sends what you type to the provider you picked, and Browser fetches the pages you open (its
home page links point at GitHub, and its searches go to DuckDuckGo); nothing else talks to a service you didn't set up.

**Use the network tools on your own networks.** Net Tools' port scanner and packet capture, and
RF Tools' receivers, are for networks and radio you're allowed to look at. The rules on
receiving and transmitting differ by country.

Please report security problems privately, through this repository's Security tab.

## Testing

Every pull request runs `.github/workflows/ci.yml`, twice over: against the OS commit this
repository pins (`base/`), and against the OS's `main` branch, so an OS change that breaks an app
shows here before the OS is released.

| CI step | What it does | What it catches |
| --- | --- | --- |
| Every changed app has a new version | `tools/check-versions.sh`: compares each app folder with `main`; a change to a kit file counts for every app that uses it. README and test changes don't count | An app that changed without a new version, which devices would never be offered |
| Simulator and headless builds | Builds every app into the SDL and headless ZeroShell | Code that doesn't compile |
| Unit tests | `ctest`: AI's requests, replies and settings (no network); Browser's Markdown and HTML parsing, forms, pictures, layout and addresses; Desktop's host list; Grove's LED effects; RF Tools' DSP, and an integration test of its radio daemon against a simulated radio | Logic that broke |
| App script tests | `apps/<id>/tests/*-test.sh`: Router's routing script against stand-ins | A broken helper script |
| Open every app | `tools/screenshots.sh`: drives the headless build through the home screen and every app, and every tool inside Net Tools and RF Tools, and saves a screenshot of each (uploaded as an artifact). Games plays a test-pattern core; AI gets a canned answer; RF Tools runs against a simulated radio | An app that crashes or hangs when opened |
| Apps as modules | Builds every app as a module and loads each into a ZeroShell that has none built in | A module with a missing symbol or the wrong app level |
| Install every app for real | On arm64 Debian trixie with Raspberry Pi's repository added, as on the device: builds the real packages, installs them all side by side with the real apt, then removes each and checks nothing is left | A dependency that doesn't exist, two apps that clash, leftovers after removal |

The packages from that last job are attached to every pull request as the `zeroos-apps`
artifact, so you can try a change on a device before it merges:

```sh
sudo zeroos-apps install-file zeroos-app-<id>_*.deb
```

What CI can't check is the hardware: whether the GPS gets a fix, the LED strip lights, the car
takes the USB stick, the dongle tunes. Those lists are in each app's README. If you have the
hardware, working through one and reporting back is the most useful thing you can do.

## Repository layout

```
apps/<id>/              one folder per app
  <id>.cpp ...          its code
  app.cmake             lists it: name, icon, colour, place, sources, tests
  <id>.pkg              its package: name, summary, APP_VERSION, what it depends on, payload()
  files/                services, scripts and rules the package installs (optional)
  prepare.sh            fetches what the package needs at build time (optional)
  tests/                unit tests, and *-test.sh script tests (optional)
  README.md             how to use it, and how it works
apps/kit/               code shared by Dev Tools, Games, Net Tools and RF Tools
apps/rf/core/           RF Tools' radio side: the zrfd daemon, its client, DSP, simulators
base/                   the OS, pinned (a git submodule): the headers apps are built against
tools/
  build.sh              build every package and an index, for one app level
  assemble.sh           put a release together from several levels
  levels.sh             which ZeroOS versions to build for
  check-versions.sh     fail if an app changed but its version didn't
  screenshots.sh        open every app headless and save a picture of each
CMakeLists.txt          builds ZeroShell from base/ with every app here
.github/workflows/
  ci.yml                checks on every pull request
  release.yml           publishes a release
```

## Building it yourself

### Run it on a PC

You need Linux (or WSL) with CMake, a C++17 compiler and SDL2:

```sh
git clone --recurse-submodules https://github.com/hartj77-blip/CardPuter-Apps.4ME-U
cd CardPuter-Apps.4ME-U
sudo apt install cmake g++ libsdl2-dev
cmake -S . -B build/sim && cmake --build build/sim -j
./build/sim/zeroshell
```

That opens the home screen in a window, with the OS's four apps and every app here, running on
simulated hardware. The PC keyboard stands in for the Cardputer's. Optional extras:

```sh
sudo apt install libvncserver-dev freerdp3-dev   # Desktop can really connect
sudo apt install librtlsdr-dev                   # RF Tools can use a real dongle
```

To try RF Tools with a simulated radio:

```sh
ZRFD_BIN=$PWD/build/sim/bin/zrfd ZRF_SOURCE=sim ./build/sim/zeroshell
```

To build headless (no window) and run what CI runs:

```sh
cmake -S . -B build/headless -DZS_BACKEND=headless && cmake --build build/headless -j
ctest --test-dir build/headless --output-on-failure
tools/screenshots.sh build/headless build/screenshots     # a PNG of every app
```

| Build option | Values | |
| --- | --- | --- |
| `ZS_BACKEND` | `sim` (default), `headless`, `device` | SDL window, no window, or the Cardputer's framebuffer and keyboard |
| `ZS_APPS_AS_MODULES` | `ON` / `OFF` | Build each app as a module. On by default for `device` |
| `ZEROOS_BASE` | a path | Build against another checkout of the OS instead of `base/` |

### Build the packages

Releases are built on arm64 Debian trixie. To build them yourself, on an arm64 Debian trixie
machine or container (a Raspberry Pi 4 or 5 on Raspberry Pi OS trixie works):

```sh
sudo apt install cmake g++ dpkg-dev curl gpg python3 pkgconf libvncserver-dev freerdp3-dev librtlsdr-dev
cmake -S . -B build/device -DZS_BACKEND=device
cmake --build build/device -j
tools/build.sh build/device build/apps           # every package, and the index, in build/apps
tools/build.sh build/device build/apps clock     # or only some apps
```

Cross-compiling from x86 (`-DCMAKE_TOOLCHAIN_FILE=$PWD/base/cmake/aarch64-linux-gnu.cmake`)
still builds every module, but not Desktop's viewer or RF Tools' daemon, so those two packages
can't be made that way.

## Work on an app

Each app is self-contained in its folder, so you can work on one without touching the others.

1. Edit the files in `apps/<id>/`.
2. Run it in the simulator (above).
3. Raise `APP_VERSION` in `apps/<id>/<id>.pkg`. A device only takes an update when the version
   goes up, and CI fails if you forget. A change only to `README.md` or `tests/` doesn't need it.
   A change to `apps/kit` needs it for each app that uses that file.
4. Open a pull request. CI attaches the packages, so you can try it on a device. When it merges,
   the new version is published by itself.

**Rename an app:** rename the folder, the `.cpp` and `.pkg` files and the id in `app.cmake`, then
add `REPLACES="<old id>"` to the new `.pkg`. Update all on a device then installs the new app in
place of the old one (AI was once called `zclaw`).

## Write a new app

1. Copy the smallest app, `apps/clock`, to `apps/<id>`. The id is lower-case letters, digits and
   `-`. Rename `clock.cpp` and `clock.pkg` to `<id>.cpp` and `<id>.pkg`.
2. In `app.cmake`, set the id, name, icon (an `LV_SYMBOL_*` or `ZS_ICON_*`, see the OS's
   `shell/src/ui/theme.h`), colour, factory function and `ORDER`, the place on the home screen.
   Lower comes first; these are taken:

   | Order | Apps |
   | --- | --- |
   | 10 to 90 | Terminal 10, Remote 20, Desktop 25, Sync 30, Files 40, Notes 50, Recorder 55, AI 60, Browser 65, Media 70, Jellyfin 72, Grove 75, Camera 80, Car 90 |
   | 100 | the OS's own apps |
   | 110 to 190 | Tether 110, Router 115, IR Remote 120, GPS 130, Mesh 140, Compass 150, Battery 160, Clock 170, Dock 180, System 190 |
   | 200 to 220 | the OS's own apps, Net Tools 220 |
   | 300 and up | Dev Tools 300 to 330, Games 400, RF Tools 500 |

3. In `<id>.pkg`, set `NAME`, `SUMMARY` and `APP_VERSION="1.0.0"`, and `DEPENDS` for any Debian
   packages it needs. Everything it can set is under [The .pkg file](#the-pkg-file).
4. Write the app. A minimal one:

   ```cpp
   #include "apps/common.h"

   namespace zs {
   namespace {
   using namespace theme;

   class HelloApp : public App {
   public:
       void build(lv_obj_t* root) override {
           text_ = label(root, "Hello", &zs_font_20);
           lv_obj_center(text_);
       }
       bool on_key(uint32_t key) override {
           if (key != LV_KEY_ENTER) return false;
           lv_label_set_text(text_, fmt("Pressed %d times", ++n_).c_str());
           return true;
       }
   private:
       lv_obj_t* text_ = nullptr;
       int n_ = 0;
   };
   }  // namespace

   std::unique_ptr<App> make_hello_app() { return std::make_unique<HelloApp>(); }
   }  // namespace zs
   ```

5. Build the simulator. The app appears on the home screen; `tools/screenshots.sh` and CI pick it
   up by themselves.
6. Write a `README.md`: what it does, its keys, where it keeps files, what it needs, how it works,
   and what hasn't been checked on a device.
7. Add it to the tables in this README, and open a pull request.

Things that make an app fit in: keyboard only; nothing slow on the screen's thread; say what a
key does in the soft-key bar (`shell_set_actions()`); ask before deleting anything
(`shell_confirm()`); save your user's work as they go; and work, or say plainly what's missing,
in the simulator and on a device without the hardware. The OS's
[UX guide](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/UX.md) has the rest.

## Run your own app repository

You don't need this repository to publish apps for ZeroOS. A device can take apps from any
number of **sources**, listed in `/etc/zeroos/apps.sources`, most trusted first:

```
hartj77-blip/CardPuter-Apps.4ME-U     # this repository
hartj77-blip/CardPuter-OS.4ME-U       # the OS's releases, a fallback while apps finish moving
m5stack                               # M5Stack's app store
```

A source is one of:

- `owner/repo`: a GitHub repository whose **newest release** (not a draft or pre-release) holds
  `zeroos-apps.index` and the packages it lists. The device downloads
  `https://github.com/<owner>/<repo>/releases/latest/download/zeroos-apps.index`, and each
  package from the same place;
- an `https://` address that serves `zeroos-apps.index` and the packages next to it (any web
  server, GitHub Pages, a bucket). Plain `http://` isn't accepted;
- the address of a `.json` file, read as an M5Stack-style store (below).

Someone adds yours with **Apps > Tab > P > Add a GitHub repo** and your `owner/repo` or address,
or `sudo zeroos-apps add-source <owner/repo>`. It goes at the end of their list.

### The easy way: start from this repository

Everything here works in a copy: the build, CI, the version check, the levels, the releases.

1. Create your repository from this one (fork it, or copy it with its `base` submodule).
2. Delete the app folders you don't want to publish, and add yours under `apps/`. A folder per
   app, exactly as in [Write a new app](#write-a-new-app). Keep `apps/kit` if any of your apps
   use it.
3. Give your apps **ids no other source uses**. When two sources offer the same id, the one
   higher in the device's list wins, and yours is added at the bottom, so an id that clashes
   with an app here is simply hidden. The Debian package names (`zeroos-app-<id>`) have to be
   unique on the device too. A prefix of your own (`jo-weather`) avoids both problems.
4. In your copy's GitHub settings, turn on Actions. On a fork, timed workflows stay off until you
   open the Actions tab and enable them; without that, your apps won't be rebuilt for new ZeroOS
   versions by themselves. The Release workflow needs permission to write releases (Settings >
   Actions > General > Workflow permissions > Read and write).
5. Merge a change, or run **Actions > Release > Run workflow**. Your newest release now holds the
   packages and the index, and your repository works as a source.

The release workflow watches the OS repository (`ZEROOS_REPO` in `tools/levels.sh`) and builds
for the levels its newest release and `main` use, so your apps follow ZeroOS just like these do.

### The minimal way: publish the files yourself

All a device needs is the index and the packages. The OS has the tool that makes both,
`image/apps/make-app-deb.sh`, which needs only bash and `dpkg-deb`:

```sh
# Your module, built against the OS's headers for the device's app level (here, 3)
ZEROOS_APP_ABI=4 ZEROOS_FILES=myapp/files \
  base/image/apps/make-app-deb.sh myapp/jo-weather.pkg out 1.0.0-abi4 build/jo-weather.so \
  >> out/zeroos-apps.index
```

It writes `out/zeroos-app-jo-weather_1.0.0-abi4_arm64.deb` and prints the package's block for the
index, which you append. Do that for each app, then publish everything in `out/` as a GitHub
release (or on a web server). Things to get right:

- **The level.** Build each module against the OS checked out at the release your users run, and
  set `ZEROOS_APP_ABI` to its `ZS_APP_ABI` (`shell/src/ui/app.h`). A device drops index blocks for
  any other level, and apt refuses the package anyway. When ZeroOS raises the level, build again
  and list both builds in one index (one block per level) until your users have updated.
- **arm64.** The module must be built for arm64. Cross-compiling works
  (`-DCMAKE_TOOLCHAIN_FILE=base/cmake/aarch64-linux-gnu.cmake`) unless your app links a library
  of its own, in which case build on arm64 Debian trixie.
- **The version goes up** with every change, or devices won't offer the update.
- **The checksum** in the index must be of the file you publish. Don't re-upload a package
  without writing the index again.

The block format and the rules a device applies to it are under [The index](#the-index).

### Apps that aren't modules: the M5 way

If your app is a standalone program (Python with pygame or Pillow, an SDL or LVGL program of its
own) rather than a ZeroShell module, package it the way M5Stack's store does: an ordinary Debian
package that puts a launcher entry, a `.desktop` file with `Name`, `Exec` and optionally
`Terminal=true`, in `/usr/share/APPLaunch/applications/`. ZeroOS lists each entry on the home
screen and hands it the screen when opened. It runs on M5's own system too. You can then:

- submit it to [CardputerZero/packages](https://github.com/CardputerZero/packages), M5's store,
  so it reaches every Cardputer Zero, whichever system it runs; or
- publish your own `registry.json` in the same format as M5's, and have people add its
  `https://` address as a source.

### What you're asking people to trust

Your users give your apps the same access as these: your packages' scripts run as root, and a
module runs inside ZeroShell. Say plainly in your README what each app installs, which
permissions it adds and what it sends over the network, as the
[security section](#security-and-what-you-are-trusting) here does. And keep the release
workflow, and who can merge, as tight as you would for anything that installs as root.

## The pinned OS

`base/` is a git submodule naming one commit of the OS repository. Pull requests are built
against it, and against OS `main` as a second check. Releases don't use the pin: they build
against the OS versions described under [App levels](#app-levels-staying-compatible-with-zeroos).
Move the pin when you want newer OS headers while developing:

```sh
git -C base fetch origin && git -C base checkout origin/main
git add base && git commit -m "Move the OS pin"
```

If `cmake` says the OS wasn't found, the submodule isn't checked out:

```sh
git submodule update --init base
```

## Licence and third-party code

This repository is MIT licensed, see [LICENSE](LICENSE).

Parts built in or shipped from elsewhere, each described in that app's README:

| Part | Used by | Licence |
| --- | --- | --- |
| [LVGL](https://lvgl.io) 9.2 | every app, through ZeroShell | MIT |
| [quirc](https://github.com/dlbeer/quirc) | Camera's QR reader | ISC |
| [stb_image.h](https://github.com/nothings/stb) v2.34 | Games' PNG border pictures (vendored); Browser's pictures in pages (fetched at build time, same commit) | Public domain or MIT, see [apps/games/third_party](apps/games/third_party/README.md) |
| [M5Stack CameraApp](https://github.com/CardputerZero/CameraApp) | Camera (installed as M5's own package) | MIT |
| [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) | IR Remote's code database | CC0-1.0, see [apps/ir/README.md](apps/ir/README.md#licence) |
| ft8_lib and others | RF Tools | see [apps/rf/files/THIRD_PARTY.md](apps/rf/files/THIRD_PARTY.md) |

Everything an app installs from Debian, Raspberry Pi, Tailscale or Meshtastic keeps its own
licence; apt installs those packages as published.
