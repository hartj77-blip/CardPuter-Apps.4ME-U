# Desktop

Remote desktop to your other computers: a Windows PC (RDP), a Mac or a Linux
machine (VNC). You see it on the Cardputer's own screen, or full size on a
monitor in Dock mode. Works on your home network, and from anywhere with
Tailscale ([Remote](../remote/README.md)).

## First time

1. Turn on remote access on the computer:
   - **Windows** (Pro, Enterprise or Education): Settings > System > Remote
     Desktop > on. Windows Home has no RDP server; install a VNC server
     (TightVNC, UltraVNC) and use VNC.
   - **Mac**: System Settings > General > Sharing > Screen Sharing > on. Use
     your Mac user name and password.
   - **Linux**: a VNC server (`wayvnc` on Wayland, `x11vnc` or TigerVNC on X), or
     `xrdp` for RDP.
2. Open **Desktop**, pick **Add a computer** and type its address: a name
   (`office-pc`), an IP address, or `name:port`.
3. Pick **Connect**.

With Tailscale on, your devices are listed under your saved computers
(marked **+**). Enter on one fills in a new computer for you: RDP for
Windows, VNC for the rest. **D** on a device in Remote does the same.

## On the computer list

The list is a List screen: type to find a computer, or press its number (1 to 9). The rest is in
the menu: **Tab**, then the letter, or **Ctrl** and the letter straight away. The bar along the
bottom of the screen shows the keys.

| Key | Does |
| --- | --- |
| Enter | Connect, on the Cardputer's screen |
| Tab, then N | New computer |
| Tab, then E | Edit: name, address, VNC or RDP, user, password, RDP size, domain |
| Tab, then M | Open on the monitor (in Dock mode) |
| Tab, then D | Delete the computer (it asks first) |
| Esc | Back |

In Edit, Enter on a row types it (or, on **Type** and **Size**, Left and Right change it), Tab
then D deletes the computer, and Esc saves and goes back.

Passwords are asked for each time unless you save one in Edit. A saved
password is kept in `~/.config/zeroos/desktop-hosts`, readable only by you,
but not encrypted.

## While connected

The remote screen fills the app area and has every key, like a game: there is no bar along the
bottom. Keys type on the remote computer. The pointer is moved from the keyboard in mouse mode.
**Esc** opens the menu for everything else:

| Esc, then | Does |
| --- | --- |
| Esc | Send Esc to the computer |
| Q | Leave |
| M | Mouse mode on or off |
| Z | Zoom: whole screen, 1:1, 2x (remembered) |
| C, A, S, W | Hold Ctrl, Alt, Shift or Win (Cmd on a Mac) for the next key; they add up (Esc C, Esc A, then T) |
| D | Ctrl+Alt+Del |
| F, then 1-9, 0, -, = | F1 to F12 |
| G | Type a backtick (the Cardputer's ` key is Esc) |
| Tab | Shift+Tab |

In **mouse mode**:

| Key | Does |
| --- | --- |
| Arrows | Move the pointer (hold to go faster) |
| Enter | Click |
| R | Right-click |
| D | Start dragging; Enter or D again drops |
| W, S | Scroll up, down |

Other keys still type. When zoomed in, the view follows the pointer.

The screen is 320 x 150, so a whole desktop is small. For RDP, Desktop asks
Windows for a desktop of the size you pick (Edit > Size; 960 x 450 by
default), which is easier to read than a full HD one. VNC shows the
computer's screen at its own size; use zoom.

## On the monitor

In Dock mode, **Open on the monitor** (Tab, then M) opens the computer full size on the monitor:

- **RDP**: FreeRDP in a window, which asks for the password there. It
  resizes with the window, with sound and the clipboard.
- **VNC**: gvncviewer. It needs a port from 5900 to 5999 (`mac`, `pc:1`,
  `pc:5901`).

Use the monitor's USB keyboard and mouse, or the Cardputer's keyboard.

## How it works

| Piece | What |
| --- | --- |
| `desktop.so` | The app: the list, the editor, and the viewer, which scales the remote screen into the 320 x 150 area (averaging when it shrinks it) and draws the pointer. |
| `/usr/lib/zeroos/zeroos-desktop-view` | The connection, in its own process so a protocol problem can't take ZeroShell down. libvncclient for VNC (asks the server for 16-bit colour, which is what the screen shows, and half the data), FreeRDP 3 for RDP (16-bit, software drawing, no extra channels). |
| Between them | A socket for lines both ways (keys as X keysyms, pointer moves; status, errors), and the screen in a file in `/dev/shm` that the app maps read-only. A new size is a new file, so a resize can never pull memory out from under the app. Protocol details are at the top of `view/view.cpp`. |
| Certificates | RDP trusts a computer's certificate the first time (as `wlfreerdp /cert:tofu` does) and refuses to connect if it changes later. To accept a new one, remove the computer's file in `~/.config/freerdp/server/`. |
| Keys | VNC gets keysyms as they are. RDP gets scan codes for a US keyboard layout: set the remote computer's keyboard to US English for the symbols to come out right. |
| `/usr/lib/zeroos/zeroos-desktop-dock` | The monitor: `gvncviewer`, or `wlfreerdp3` in a `foot` window, in the Dock session. |
| Log | `~/.cache/zeroos/desktop-view.log`, the viewer's messages from the last connection. |

## Building

The viewer needs `libvncserver-dev` and `freerdp3-dev`. Without them the app
still builds and opens (the simulator and screenshots), but it can't connect
and the package can't be made. With them, the simulator connects for real.

To try it on a PC with a VNC server:

```sh
sudo apt install xvfb x11vnc x11-apps
Xvfb :7 -screen 0 1024x600x24 & DISPLAY=:7 xclock &
x11vnc -display :7 -rfbport 5917 -forever &
./build/sim/zeroshell   # Desktop > Add a computer > 127.0.0.1:5917, VNC
```

## Still to do

- Try on a real Cardputer: speed, memory, and how readable the screen is.
- The remote pointer's shape (now a plain arrow is drawn over RDP).
- Clipboard and sound on the built-in screen.
- Keyboard layouts other than US English for RDP.
- Encrypted saved passwords.

Third-party parts: libvncclient (GPL-2.0+), FreeRDP (Apache-2.0), gvncviewer
(LGPL-2.1+), from Debian.
