# CardPuter-Apps.4ME-U

Apps for [ZeroOS](https://github.com/hartj77-blip/CardPuter-OS.4ME-U), the operating system for
the M5Stack Cardputer Zero.

ZeroOS ships with just four apps: **Apps**, **Settings**, **Update** and **Wi-Fi**. Everything
else is in this repository, one folder per app, and gets installed from the **Apps** app on the
device. Each app is versioned and released on its own, so updating one doesn't touch the others.

| Repository | What's in it |
| --- | --- |
| [CardPuter-OS.4ME-U](https://github.com/hartj77-blip/CardPuter-OS.4ME-U) | The OS: home screen, the four built-in apps, SD card image, OS updates |
| This one | Every other app, and the packages the Apps app downloads |

## Install an app

On the Cardputer, open **Apps**, pick one and press **Enter**. Press **U** to update every app
that has a newer version. Apps come from this repository's
[newest release](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/releases/latest).

If your device doesn't list this repository yet (ZeroOS 0.3.4 and older), add it once:
**Apps > S > Add a GitHub repo**, and type `hartj77-blip/CardPuter-Apps.4ME-U`. Or from a terminal:

```sh
sudo zeroos-apps add-source hartj77-blip/CardPuter-Apps.4ME-U
```

## The apps

| App | Folder | What it does |
| --- | --- | --- |
| Battery | [apps/battery](apps/battery) | Time left, power draw and power profile |
| Browser | [apps/browser](apps/browser/README.md) | Read the web as text: the ZeroOS docs, READMEs, Markdown and simple pages |
| Camera | [apps/camera](apps/camera/README.md) | Viewfinder, photos and a QR code reader that joins Wi-Fi |
| Car | [apps/car](apps/car/README.md) | USB stick mode for a car stereo, Bluetooth audio |
| Clock | [apps/clock](apps/clock) | Clock and stopwatch |
| Compass | [apps/compass](apps/compass) | Compass and spirit level |
| Dev Tools | [apps/dev](apps/dev/README.md) | Code, REPL, Git and Serial |
| Desktop | [apps/desktop](apps/desktop/README.md) | Remote desktop (VNC, RDP) on the built-in screen or a monitor |
| Dock | [apps/dock](apps/dock/README.md) | Desktop on an HDMI monitor |
| Files | [apps/files](apps/files) | Browse the files on the device |
| Games | [apps/games](apps/games/README.md) | NES, Game Boy and GBA games |
| GPS | [apps/gps](apps/gps) | Position, saved spots and GPX tracks |
| Grove | [apps/grove](apps/grove/README.md) | Light a NeoPixel LED strip on the Grove port, with effects |
| IR Remote | [apps/ir](apps/ir/README.md) | A universal remote, and remotes you teach it |
| Jellyfin | [apps/jellyfin](apps/jellyfin/README.md) | Music, movies and shows from your Jellyfin server, in Jellyfin's own look |
| Media | [apps/media](apps/media/README.md) | Classic MP3 player over a file explorer: music and video on the SD card, USB drives and server shares |
| Mesh | [apps/mesh](apps/mesh) | Meshtastic text messages over LoRa |
| Net Tools | [apps/net](apps/net/README.md) | Discover devices, scan ports, trace routes, DNS, packet capture, share Ethernet |
| Notes | [apps/notes](apps/notes) | Quick text notes |
| Recorder | [apps/recorder](apps/recorder/README.md) | Voice notes: record, play back, rename |
| Remote | [apps/remote](apps/remote/README.md) | Tailscale or Headscale, SSH, remote desktop, Taildrop |
| RF Tools | [apps/rf](apps/rf/README.md) | Spectrum, scanner, sensors, aircraft, APRS, FT8 and more for an RTL-SDR dongle |
| Router | [apps/router](apps/router/README.md) | Share your Tailscale exit node with a laptop over Ethernet or USB-C |
| Sync | [apps/sync](apps/sync/README.md) | Syncthing for Notes, Documents and Recordings |
| System | [apps/sysmon](apps/sysmon) | CPU, memory, storage, temperature |
| Terminal | [apps/terminal](apps/terminal) | Linux terminal on the built-in screen |
| Tether | [apps/tether](apps/tether/README.md) | Get online through your phone |
| AI | [apps/ai](apps/ai/README.md) | AI assistant |

Apps that need extra hardware: Camera needs a Cardputer with a camera, GPS a GPS module, Mesh the
Cap LoRa-1262, Grove an LED strip on the Grove port, and RF Tools an RTL-SDR dongle.

Most of these have only been tested in the simulator and CI so far, not on a real Cardputer. If
something doesn't work on your device, please
[open an issue](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/issues).

## Work on an app

Each app is self-contained in its own folder, so you can work on one without touching the others.

```
apps/<id>/
  <id>.cpp ...     the app's code
  app.cmake        lists the app: name, icon, colour, source files
  <id>.pkg         its package: name, summary, APP_VERSION, what it depends on
  files/           services and scripts the package installs (optional)
  prepare.sh       fetches what the package needs at build time (optional)
  tests/           unit tests (optional)
  README.md        how to use it (optional)
apps/kit/          code shared by Dev Tools and Games
base/              the OS, pinned (a git submodule): the headers apps are built against
tools/             packaging and release scripts
```

Get the code and try it on a PC:

```sh
git clone --recurse-submodules https://github.com/hartj77-blip/CardPuter-Apps.4ME-U
cd CardPuter-Apps.4ME-U
sudo apt install cmake g++ libsdl2-dev
sudo apt install libvncserver-dev freerdp3-dev   # optional: lets Desktop connect in the simulator
cmake -S . -B build/sim && cmake --build build/sim -j
./build/sim/zeroshell            # the home screen in a window, with every app
```

**Change an app:**

1. Edit the files in `apps/<id>/`.
2. Raise `APP_VERSION` in `apps/<id>/<id>.pkg`. A device only takes an update when the version
   goes up, and CI fails if you forget. (A change to `apps/kit` needs it for Dev Tools and Games.)
3. Open a pull request. When it merges, the new version is published by itself.

**Add an app:** copy a small one (`apps/clock` is the smallest), rename the folder and the
`.cpp` and `.pkg` files to your app's id, and edit `app.cmake`. The id is lower-case letters,
digits and `-`. Pick an `ORDER` for its place on the home screen (the OS's own apps use 100 and
200 to 220).

**Rename an app:** rename it as above, then add `REPLACES="<old id>"` to the new `.pkg`. Update all
on a device then installs the new app in place of the old one.

**Try a build on a device** before it is released: CI attaches the packages to every pull
request (the `zeroos-apps` artifact). Copy one over and run
`sudo zeroos-apps install-file zeroos-app-<id>_*.deb`.

What CI checks on every pull request:

| Check | What it catches |
| --- | --- |
| Version check | An app changed but its `APP_VERSION` didn't |
| Simulator and headless builds, unit tests | Code that doesn't compile, failing tests |
| Open every app | An app that crashes when opened |
| Apps as modules | A module that won't load into ZeroShell |
| The same, against ZeroOS `main` | An OS change that breaks an app, before the OS is released |
| Install every app on Debian trixie arm64 | A dependency that doesn't exist, two apps that clash, leftovers after removal |

## Compatibility

An app is a module that ZeroShell loads, so it has to be built against the same headers as the
ZeroShell on the device. ZeroOS numbers that with an **app level** (`ZS_APP_ABI` in the OS's
`shell/src/ui/app.h`). The rules:

1. **One package, one level.** A package's version ends in its level: `1.2.0-abi3` is version
   1.2.0 built for level 3. It depends on `zeroos-app-abi-3`, which only a matching ZeroOS provides.
2. **The index can list an app once per level.** The device only shows the builds for its own level.
3. **The OS raises the level** in the same change as any header change that breaks built apps.
4. **This repository follows the OS by itself.** Every 3 hours the Release workflow looks at the
   OS repository. It builds for the level of the newest ZeroOS release, and also for the level
   on the OS's `main` branch when that is different. So the apps for the next ZeroOS are
   published before that ZeroOS is released.
5. **The level below is kept.** Builds for the previous level are carried over from the previous
   release, so a device that hasn't updated ZeroOS yet still sees its apps.

What this means in practice:

| Situation | What happens |
| --- | --- |
| An app changes here | Merging publishes it. Devices see an update for that app only. |
| The OS raises the level on `main` | Within 3 hours the apps are built for the new level too. Nothing changes for devices yet. |
| That ZeroOS is released | Devices that update find the new builds waiting. **U** in Apps installs them. |
| An app no longer builds against OS `main` | CI here goes red on the "ZeroOS main" job. Fix the app before the OS is released. |

If the timed run has not caught up, start it by hand: **Actions > Release > Run workflow**.
GitHub pauses timed workflows after 60 days without a commit; running it by hand also wakes it.

## Releases

A release is the full set: every app's package for each level, and `zeroos-apps.index`, which is
what a device reads. The Release workflow publishes one:

- when a change to an app merges into `main`;
- when ZeroOS has a new release or a new app level (checked every 3 hours);
- when you press **Run workflow**.

Nothing is published unless CI passes first. Releases are built on arm64 Debian trixie (what
the Cardputer runs), because Desktop's viewer links Debian's VNC and RDP libraries. To build the
packages yourself, on an arm64 Debian trixie machine or container:

```sh
sudo apt install cmake g++ dpkg-dev curl gpg pkgconf libvncserver-dev freerdp3-dev
cmake -S . -B build/device -DZS_BACKEND=device
cmake --build build/device -j
tools/build.sh build/device build/apps     # packages and index, in build/apps
```

Cross-compiling (`-DCMAKE_TOOLCHAIN_FILE=$PWD/base/cmake/aarch64-linux-gnu.cmake`) still builds
every module, but not Desktop's viewer, so the Desktop package can't be made that way.

## The pinned OS

`base/` names one commit of the OS repository. Pull requests are built against it, and against
OS `main` as a second check. Releases don't use the pin: they build against the OS versions in
the table above. Move the pin when you want new OS headers while developing:

```sh
git -C base fetch origin && git -C base checkout origin/main
git add base && git commit -m "Move the OS pin"
```

## Licence

MIT, see [LICENSE](LICENSE). Third-party parts are listed in each app's README.
