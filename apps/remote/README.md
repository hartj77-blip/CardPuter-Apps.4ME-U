# Remote mode and Tailscale

Remote mode makes the Cardputer Zero a screen and keyboard for your bigger
computers: SSH into a home PC, server or cloud machine from anywhere, or open
a full remote desktop on a monitor in Dock mode. Tailscale connects them
without port forwarding, from any Wi-Fi or phone hotspot.

## First time

1. Open **Remote**. Tailscale starts and shows a QR code.
2. Scan it with your phone, sign in to Tailscale (or create a free account),
   and approve the device.
3. A few seconds later Remote shows **Connected** and lists your devices.

Install Tailscale on the computers you want to reach
([tailscale.com/download](https://tailscale.com/download)). For SSH, the
computer needs an SSH server (built into Linux and macOS; on Windows, turn on
"OpenSSH Server" in Optional features). For remote desktop, Windows Pro's
Remote Desktop or any RDP server (xrdp on Linux).

## Using it

In **Remote**, your devices are a list (typing filters it), with the actions for the one in focus
in the menu on **Tab** (Ctrl + the letter runs one straight away):

| Key | Does |
| --- | --- |
| Enter on a device | SSH to it in the Terminal (or mosh, if turned on) |
| Tab > S | The same: SSH to it |
| Tab > D | Remote desktop to it: opens it in [Desktop](../desktop/README.md) (VNC or RDP, on this screen or the monitor). Without Desktop installed, RDP on the monitor (needs Dock mode) |
| Tab > F | Send a file to it: the one Files handed over, or else Files opens to pick one (Tab > S there) |
| Tab > I | Its name, address and system |
| Connect to... | SSH to any host or address, e.g. `john@example.com` |
| SSH as | The user name for SSH (default `pi`) |
| Use mosh | mosh instead of ssh (Left / Right switch it): survives Wi-Fi changes and sleep; the other end needs `mosh` installed |
| Use auth key | Sign in with an auth key instead of the QR code (shown until you're signed in) |
| Server | Tailscale's own, or your [Headscale](#headscale) server |
| Turn off Tailscale / Sign out | |

**Terminal** (its own app, from **Apps**; SSH and mosh from Remote need it) is a real Linux console on the built-in
screen: 53 x 14 characters, running tmux. Ctrl+Q goes back to ZeroShell and
leaves everything running (SSH included); open Terminal again to pick up
where you were. Alt+Left/Right switch tmux windows, Alt+N opens a new one,
and each SSH connection opens in its own window.

**Taildrop**: files sent to the Cardputer from your other devices ("Send
with Tailscale" / share sheet) land in `~/Downloads`. To send a file, choose
**Share to a device** on it in Files (**Tab > S**) and pick a device.

The status bar shows **TS** while Tailscale is connected.

## When Tailscale runs

Settings → **Tailscale**:

- **When needed** (default): starts when you open Remote; stops after 15
  minutes with no Remote app, terminal or Dock session. Saves memory (about
  30-40 MB) and battery **(measure)** when you aren't using it.
- **Always on**: starts at boot; the Cardputer is reachable from your other
  devices all the time (SSH in to it, send files).
- **Off**: only when you turn it on in Remote.

## Headscale

[Headscale](https://headscale.net) is a control server you run yourself in
place of Tailscale's. The Cardputer still uses the normal Tailscale client;
only the server it signs in to changes.

1. In **Remote**, pick **Server** and type your server's address, e.g.
   `hs.example.com` or `http://192.168.1.10:8080` (`https://` is added if you
   leave it out; leave it empty to go back to Tailscale).
2. Sign in, one of two ways:
   - **QR code**: Remote shows **Approve on Headscale**. The link opens your
     server's registration page, which shows the command to run on the server:
     `headscale nodes register --user NAME --key ...`.
   - **Auth key** (easier): make one on the server
     (`headscale preauthkeys create --user NAME`), pick **Use auth key** and
     type it. It is used once and not kept.

Switching servers signs out of the old one (Tailscale does that with
`--force-reauth`). Your devices, SSH, mosh and remote desktop work the same.
Taildrop and MagicDNS names depend on your Headscale version and settings:
if devices show up by IP only, turn on MagicDNS in Headscale's config.

The shell passes the server to Tailscale, so this needs a ZeroOS with
Headscale support. On an older ZeroOS the **Server** setting is saved but
Tailscale's own server is still used.

## How it works

| Piece | What |
| --- | --- |
| `tailscale` package | From Tailscale's Debian repository. `tailscaled.service` is disabled at boot; ZeroShell starts it (polkit allows `pi` exactly this unit). Log uploads are off (`--no-logs-no-support`). |
| `tailscaled.service.d/zeroos.conf` | Makes `pi` the Tailscale operator, so ZeroShell runs `tailscale up`, `status`, `file cp` without root. |
| Sign-in | `tailscale up --login-server=...` in the background; ZeroShell reads the sign-in link from `tailscale status --json` and draws it as a QR code. The server comes from the setting `tailscale.server` (empty: Tailscale's); `--force-reauth` when it changed since last time; `--authkey` once when you gave one. |
| `zeroos-taildrop.service` | `tailscale file get --loop` into `~/Downloads`, started with tailscaled. |
| `zeroos-terminal.service` | A login session on tty3 running `/usr/lib/zeroos/terminal-session` (tmux, config in `/etc/zeroos/tmux.conf`). It switches the screen to tty3 and back to tty1 when tmux detaches or exits. ZeroShell stops drawing and ignores keys meanwhile, but still dims and wakes the screen. |
| Console | Terminus 6x12 font. ZeroShell keeps tty1 in graphics mode with its keyboard off, so the console never draws over the UI. |
| Remote desktop | The Desktop app when it is installed (`shell_open_app("desktop", "peer:NAME:OS")`). Otherwise `zeroos-dock open rdp HOST`: FreeRDP (Wayland) in a terminal window on the monitor, which asks for the user and password. |

## Still to do

- Saved hosts outside Tailscale, with their own user names.
- Tailscale SSH (signing in without keys or passwords).
