# Ragnar

Autonomous network reconnaissance on the Cardputer Zero. Ragnar is a
[Bjorn](https://github.com/infinition/Bjorn)-family tool: once it is on, it
scans the network it is on, finds hosts and open ports, and probes common
services, showing what it finds in a web dashboard. This app packages the
[Ragnar](https://github.com/PierreGode/Ragnar) fork, which has a headless,
web-only mode, and runs it as a background service on ZeroOS.

**Use it only on networks you own or are explicitly allowed to test.** Active
scanning and probing of networks you don't have permission for is likely against
the law where you are.

> **Early, and not yet run on hardware.** The ZeroOS integration here (the
> service, the firewall, the control panel) is written and builds, but Ragnar's
> Python dependency closure and its behaviour on a real Cardputer Zero have not
> been confirmed. See [To confirm on hardware](#to-confirm-on-hardware).

## Bjorn and Ragnar

This repository has both: **Bjorn** (the original, its own UI and features) and
**Ragnar** (this one — a large fork with many more features and a different UI).
They both serve their dashboard on port 8000, so **only one runs at a time** —
starting one stops the other. If you just want the classic Bjorn, install Bjorn.

## What it is, and why the fork

The thing most people mean by "a Bjorn" is a small Linux box that hunts a
network on its own. Upstream Bjorn is tied to a Raspberry Pi with a 2.13-inch
e-Paper HAT and won't start without that display. Ragnar is a maintained fork
that adds a headless entrypoint (`headlessRagnar.py`) which drops the display
entirely and runs just the web UI — which is what makes it packageable here,
where ZeroShell owns the screen. If you want upstream Bjorn instead, point
`prepare.sh` at it and add a display shim; expect more work.

The on-device screen is a control panel, not the tool itself: everything Ragnar
does is driven from the web dashboard.

## Using it

Open **Ragnar** and press **Enter** on **Turn on Ragnar**. It asks first,
because turning it on starts active scanning. Then:

| Row | What it does |
| --- | --- |
| Turn on / off Ragnar | Start or stop the background service |
| Open dashboard | Open the dashboard in the **Browser** app, on this device |
| Open on monitor | Open it full size on an HDMI monitor (Dock mode) |
| Allow on this network / Block | Let other devices on this network reach the dashboard, or not |

While Ragnar is on, the dashboard is at **http://localhost:8000** on the device,
and on your **Tailscale** network if Remote is set up. It is **not** open to the
rest of the local network until you choose **Allow on this network** — see
[Who can reach it](#who-can-reach-it).

When you allow it on the network, the panel shows a QR code and the
`http://<device-ip>:8000` link, so you can open the dashboard from a phone or
laptop.

## Where your files go

Ragnar keeps everything — its database, config and logs — in its own working
area, not in your home folder:

| Path | What's in it |
| --- | --- |
| `/var/lib/ragnar/` | Ragnar's working copy: its database, `config/shared_config.json`, the `data/` tree and logs |

The service runs from a writable copy there because Ragnar writes its data next
to its own program files. Removing the app stops the service; systemd keeps the
state directory, so your findings and settings survive a reinstall. To wipe it,
delete `/var/lib/ragnar` by hand.

## Who can reach it

Ragnar's web UI binds every interface, and until you set a login inside the
dashboard it serves anyone who can reach it (one of its endpoints answers with
no login at all). So this package firewalls it by default:

- **Locked (default):** only this device (loopback) and your Tailscale network
  can reach port 8000. This is set every time Ragnar starts
  (`files/zeroos-ragnar-firewall lock`).
- **Allowed on the network:** **Allow on this network** starts
  `zeroos-ragnar-lan.service`, which removes the block so the LAN can reach it
  too. **Block**, turning Ragnar off, or removing the app closes it again.

Set a login in the dashboard before you expose it to a network you share with
anyone else.

## What it needs

Everything is pulled from Debian by `DEPENDS` when you install the app: Python
with Flask (the dashboard), scapy and python-nmap (scanning), paramiko (SSH
probes), and the command-line tools `nmap`, `arp-scan`, `tcpdump`, `iw` and
others. No internet account or API key is needed for the core.

## How it works

- **The source** is vendored by `prepare.sh` from the Ragnar repository at a
  pinned commit, pruned of firmware, demos and the e-paper driver, and installed
  read-only to `/usr/lib/zeroos/ragnar`.
- **The service** (`files/ragnar.service`) runs `headlessRagnar.py` with
  `RAGNAR_HEADLESS=1`. It is not started at boot; the app starts it. On each
  start it `rsync`s the code into `/var/lib/ragnar` (keeping your data) and runs
  from there.
- **Not as root.** Upstream runs Ragnar as root. Here it runs as a locked-down
  systemd `DynamicUser` with only `CAP_NET_RAW` and `CAP_NET_ADMIN` — the raw
  sockets its scanners need — the same trade ZeroOS makes for Net Tools. The
  cost is that Ragnar features which reconfigure the system (its own Wi-Fi
  access point, `nmcli`, `raspi-config`) do not work here. The scan, probe and
  dashboard core does.
- **The control panel** (`ragnar.cpp`) is a ZeroShell module that reads the
  service state with `systemctl` and offers the rows above. It has no hal
  support, so it talks to systemd directly and fakes its state in the simulator.

## Security

Installing this app installs and runs third-party code (see
[`THIRD_PARTY.md`](files/THIRD_PARTY.md)): Ragnar, MIT-licensed, fetched at a
pinned commit. It is a capable offensive tool. Points to weigh:

- It actively scans and probes. Only aim it at networks you are allowed to test.
- Its web UI is unauthenticated until you configure a login; this package keeps
  it off the LAN by default, but set a login before exposing it.
- It runs non-root with raw-socket capabilities, not as root, but raw-socket
  access is still privileged: it can see and inject traffic on the networks the
  Cardputer is on.

## To confirm on hardware

None of this has been run on a Cardputer Zero yet. Known unknowns:

- **Python dependency closure.** `DEPENDS` lists Debian's packages for the core.
  Ragnar pulls in a wide set of modules; a few upstream ones (for example
  `ping3`, `get-mac`, `smbprotocol`, `pysmb`) are not in Debian and are **not**
  installed here. If `headlessRagnar.py` hard-imports one at startup it will
  fail to boot — check `journalctl -u ragnar` and either add the missing Debian
  package or vendor the wheel.
- **Writable-tree assumption.** The service copies the code into
  `/var/lib/ragnar` so Ragnar's `__file__`-relative writes work. Confirm it
  actually writes there and nowhere read-only.
- **Capabilities.** Confirm `nmap`, `arp-scan` and `tcpdump` work with the
  ambient `CAP_NET_RAW`/`CAP_NET_ADMIN` and don't need more.
- **Memory.** The Zero has 512 MB. Confirm the service and dashboard fit
  alongside ZeroShell.
- **The firewall.** Confirm `nft` is present and the lock/open rules behave, and
  that they don't clash with other ZeroOS network rules (Net Tools, Router).
- **Naming.** This app's id is `ragnar`. If you'd rather it read "Bjorn" on the
  home screen, change `NAME` in `ragnar.pkg` and the strings in `app.cmake`.
