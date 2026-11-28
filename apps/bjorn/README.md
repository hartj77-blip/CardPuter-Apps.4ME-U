# Bjorn

The original [Bjorn](https://github.com/infinition/Bjorn) on the Cardputer Zero:
an autonomous network reconnaissance tool. Turn it on and it scans the network
it is on, maps hosts and open ports, probes common services (SSH, FTP, SMB,
Telnet, RDP, SQL…) and collects what it finds, all shown in Bjorn's own web
dashboard — the Bjorn character, the network view, loot and credentials.

This app runs Bjorn **headless**: there is no e-paper HAT, so it swaps in a mock
display driver and the web dashboard is the whole UI. Bjorn still renders its
screen every frame, so the Bjorn face animates in the browser exactly as it
would on the HAT.

**Use it only on networks you own or are explicitly allowed to test.** Active
scanning and probing of networks you don't have permission for is likely against
the law where you are.

> **Early, and not yet run on hardware.** The ZeroOS integration (the mock
> display, the service, the firewall, the control panel) is written and builds,
> but Bjorn's Python dependency closure and its behaviour on a real Cardputer
> Zero have not been confirmed. See [To confirm on hardware](#to-confirm-on-hardware).

## Bjorn and Ragnar

This repository has two apps from this family:

- **Bjorn** (this one) — the original, with its original UI and features.
- **Ragnar** — a large fork with many more features (LoRa, SDR, extra scanners)
  and its own, different UI.

They both serve their dashboard on port 8000, so **only one runs at a time** —
starting one stops the other. Install whichever you want; install both if you
want to switch between them.

## Using it

Open **Bjorn** and press **Enter** on **Turn on Bjorn**. It asks first, because
turning it on starts active scanning. Then:

| Row | What it does |
| --- | --- |
| Turn on / off Bjorn | Start or stop the background service |
| Open dashboard | Open the dashboard in the **Browser** app, on this device |
| Open on monitor | Open it full size on an HDMI monitor (Dock mode) |
| Allow on this network / Block | Let other devices on this network reach the dashboard, or not |

While Bjorn is on, the dashboard is at **http://localhost:8000** on the device,
and on your **Tailscale** network if Remote is set up. It is **not** open to the
rest of the local network until you choose **Allow on this network** — see
[Who can reach it](#who-can-reach-it).

## How headless works

Bjorn is written for a Waveshare 2.13" e-Paper HAT, which the Cardputer Zero
does not have (ZeroShell owns the screen). So:

- `prepare.sh` adds a mock e-paper driver (`epd_mock.py`) and makes it the
  default (`epd_type`), so Bjorn imports no `RPi.GPIO` or `spidev` and needs no
  HAT.
- Bjorn's `Display` still renders its image and writes `web/screen.png` every
  frame, so the dashboard's Bjorn face and status are live. Only the physical
  panel push is a no-op.

Nothing else about Bjorn is changed: same orchestrator, same actions, same web
UI on port 8000.

## Where your files go

Bjorn keeps everything in its own working area, not in your home folder:

| Path | What's in it |
| --- | --- |
| `/var/lib/bjorn/config/` | `shared_config.json` and `actions.json` |
| `/var/lib/bjorn/data/` | Scan results, loot, cracked passwords, stolen files, logs |
| `/var/lib/bjorn/backup/` | Bjorn's backups and uploads |

The service runs from a writable copy there because Bjorn writes its data next
to its own program files. Removing the app stops the service; systemd keeps the
state directory, so your findings survive a reinstall. To wipe it, delete
`/var/lib/bjorn` by hand.

## Who can reach it

Bjorn's web UI binds every interface and has **no login**, so this package
firewalls it by default:

- **Locked (default):** only this device (loopback) and your Tailscale network
  can reach port 8000. Set every time Bjorn starts.
- **Allowed on the network:** **Allow on this network** removes the block so the
  LAN can reach it too. **Block**, turning Bjorn off, or removing the app closes
  it again.

Bjorn has no built-in authentication, so treat "Allow on this network" as making
it readable (and controllable) by anyone on that network.

## What it needs

Pulled from Debian by `DEPENDS` when you install the app: Python (the dashboard
is Python's own `http.server`, no Flask), Pillow, numpy, pandas, paramiko,
python-nmap and the `nmap` / `arp-scan` tools. No account or API key.

## How it works

- **The source** is vendored by `prepare.sh` from the Bjorn repository at a
  pinned commit, with the mock display added, installed read-only to
  `/usr/lib/zeroos/bjorn`.
- **The service** (`files/bjorn.service`) runs `Bjorn.py`. It is not started at
  boot; the app starts it. On each start it `rsync`s the code into
  `/var/lib/bjorn` (keeping your data) and runs from there.
- **Not as root.** Upstream runs Bjorn as root; here it runs as a locked-down
  systemd `DynamicUser` with only `CAP_NET_RAW`/`CAP_NET_ADMIN` — the same trade
  ZeroOS makes for Net Tools. Bjorn features that reconfigure the system (Wi-Fi,
  reboot/shutdown from the web UI) do not work here; the scan, probe and
  dashboard core does.
- **The control panel** (`bjorn.cpp`) is a ZeroShell module that reads the
  service state with `systemctl`. It has no hal support, so it talks to systemd
  directly and fakes its state in the simulator.

## Security

Installing this app installs and runs third-party code (see
[`THIRD_PARTY.md`](files/THIRD_PARTY.md)): Bjorn, MIT-licensed, at a pinned
commit. It is a capable offensive tool.

- It actively scans and probes. Only aim it at networks you are allowed to test.
- Its web UI has no login; this package keeps it off the LAN by default.
- It runs non-root with raw-socket capabilities, which is still privileged: it
  can see and inject traffic on the networks the Cardputer is on.

## To confirm on hardware

None of this has run on a Cardputer Zero yet. Known unknowns:

- **Python dependency closure.** `DEPENDS` covers the Debian-packaged core.
  Bjorn also imports `ping3`, `get-mac`, `smbprotocol` and `pysmb`, which are
  **not** in Debian and are **not** installed here. Bjorn's orchestrator loads
  its action modules at startup, so a missing one can crash the scanning thread
  (the web UI still runs). Check `journalctl -u bjorn` and vendor the missing
  wheels (a `requirements-extra.txt` + a pip step into the state dir) if the
  recon actions don't run.
- **The mock display.** Confirm Bjorn starts with `epd_type=epd_mock`, renders
  `screen.png`, and that nothing else imports the real Waveshare driver.
- **Capabilities.** Confirm `nmap` and `arp-scan` work with the ambient
  `CAP_NET_RAW`/`CAP_NET_ADMIN` and don't need more.
- **Wi-Fi gate.** Bjorn starts its orchestrator only when `nmcli` reports a
  Wi-Fi connection; confirm that is true on ZeroOS.
- **Memory.** The Zero has 512 MB. Confirm Bjorn and the dashboard fit alongside
  ZeroShell.
