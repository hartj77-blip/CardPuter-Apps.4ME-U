# Net Tools

Net Tools is one installable app, from the Apps app, with seven
keyboard-driven network tools inside, each a front-end for a standard
Linux tool. The tools are thin: the analysis is done by the
same programs you'd run on any Linux box, so the output is the real thing.

It works over whatever connection the device has — the built-in Wi-Fi, a
tethered phone, or the **Ethernet port**. Discover and Capture let you pick the
interface.

Install it from **Apps** (pick **Net Tools**, **Install**), then open **Net Tools**
and pick a tool from its menu: **1** to **7**, or Up/Down and **Enter**, or type to
filter. **Esc** in a tool comes back to the menu, and **Esc** on the menu leaves the
app. The menu remembers the tool you used last. Removing Net Tools removes it and the
programs it brought.

Every screen has the ZeroOS soft-key bar along the bottom, saying what the keys
do there; **Tab** opens the screen's menu, **Ctrl** + a menu letter runs that
action straight away, and **0** shows every key. Discover is a list, so typing
filters it and its actions are in the menu; the other tools' screens keep
their own letter keys, shown in the bar.

| Tool | Program | What it does |
| --- | --- | --- |
| **Discover** | `arp-scan` | Every device on your network: IP, vendor, MAC |
| **Ports** | `nmap` | The open ports and services on one host |
| **Trace** | `mtr` | The path to a host, with loss and latency per hop |
| **DNS** | `dig` | Look up a name (A, AAAA, MX, TXT, NS) |
| **Capture** | `tcpdump` | Watch live traffic and save a `.pcap` for Wireshark |
| **Netinfo** | `ip`, `ss` | This device's interfaces, routes, DNS and open ports |
| **Share** | NetworkManager | Turn the Ethernet port into a router: a laptop on the cable goes online through the Cardputer |

## Discover

Sweeps the local subnet and lists every device that answers, newest address
order, with the hardware vendor read from its MAC.

| Key | Does |
| --- | --- |
| Up/Down, 1 to 9 | Move through the devices (typing filters them) |
| Enter | Scan that device's ports (opens **Ports**) |
| Menu: R (Ctrl + R) | Scan again |
| Menu: F (Ctrl + F) | Interface: auto, `eth0` (Ethernet), `wlan0` (Wi-Fi) |

## Ports

Runs an nmap connect scan of the common ports on one host and lists the service
nmap names on each. Type an address, or arrive here with **Enter** from
Discover. **R** scans again; **Esc** goes back to type another host, and **Esc**
again goes back to Discover (if you came from there) or the menu.

## Trace

Shows the route to a host — one line per hop with packet loss and round-trip
time, so you can see where a connection slows down or stops. Type a host and
**Enter**.

## DNS

Looks up a name with dig. **Left**/**Right** change the record type (A, AAAA,
MX, TXT, NS; **T** in the menu does too); **R** looks it up again.

## Capture

Records the traffic on the wire. **Space** starts and stops; while it runs you
see a live one-line-per-packet summary and the packets are written to a `.pcap`
in `~/Recordings` at the same time. Open the file in Wireshark on a PC (Sync
copies `~/Recordings` off the device). **F** picks the interface before you
start; **R** clears the view.

## Netinfo

Two pages (**Left**/**Right**):

- **Links** — every interface (Ethernet, Wi-Fi, USB) with its state and address,
  the default gateway and the DNS servers in use.
- **Listening** — the TCP and UDP ports this device is accepting connections on.

**R** refreshes.

## Share

Makes the Cardputer a small router for a laptop (or any wired device) plugged
into its **Ethernet port**. The laptop gets an address by DHCP (10.42.0.x) and
goes online through whatever the Cardputer is online through: Wi-Fi or a phone
from **Tether**. With nothing to share, the laptop still gets an address, so
you can reach the Cardputer from it (`ssh pi@10.42.0.1`).

1. Get the Cardputer online (Wi-Fi or Tether).
2. Plug the laptop in. Its wired network should be on automatic (DHCP), which
   is the default on Windows, macOS and Linux.
3. Open **Share** and press **Enter** (Sharing on).

The screen shows the cable, where the internet is coming from, the
Cardputer's address on the cable and each device that has taken an address.

| Key | Does |
| --- | --- |
| Enter | Sharing on / off |
| R | Refresh (it also refreshes every few seconds) |

Sharing doesn't come back on by itself after a restart. If the Cardputer was
online *through* the Ethernet port, sharing takes the port over for the laptop
(the screen warns you first); turning it off gives the port back.

How it works: one NetworkManager connection, `zeroos-share`, on `eth0` with
`ipv4.method shared`, so NetworkManager turns on forwarding, adds the NAT rule
and runs dnsmasq for DHCP and DNS. The pack's helper `/usr/bin/zeroos-share`
(`status`, `on`, `off`) creates and starts it; you can run it from a terminal
too. It runs as you, since ZeroOS already lets the home-screen user manage
NetworkManager.

To confirm on hardware: that the image's NetworkManager uses nftables for the
NAT rule (the pack installs `nftables` and `dnsmasq-base`), and that a laptop
gets an address and reaches the internet through Wi-Fi and through a tethered
phone.

## Permissions

Nothing in Net Tools runs as root, and the pack adds no `sudo` rule.

Finding devices (arp-scan) and capturing packets (tcpdump) need raw access to
the network, which normally means root. Instead of root, the pack gives those
two programs just the Linux capabilities they need, set on the files
themselves:

| Program | Capabilities | Why |
|---|---|---|
| `arp-scan` | `cap_net_raw` | send and read ARP frames |
| `tcpdump` | `cap_net_raw`, `cap_net_admin` | capture, and put the interface in promiscuous mode |

Only the `netdev` group (the home-screen user and ZeroShell are in it) can run
them now: they become `root:netdev`, mode `0750`. They still run as you, so a
capture file belongs to you. A program tcpdump starts for you (its `-z`
option) gets none of the capabilities.

What this still allows: anyone in `netdev` can watch all the traffic on the
device's interfaces, and can see every device on the local network. That is
what the two tools are for. It is not a route to root.

How it is kept up: `zeroos-net-caps.service` sets the capabilities at boot and
when the pack is installed, and `zeroos-net-caps.path` sets them again after
an upgrade of arp-scan or tcpdump (an upgrade replaces the file, which loses
them; until they are back, the tool just doesn't work without root).
Removing the pack puts both files back the way their packages ship them
(`root:root`, `0755`, no capabilities). The script is
`/usr/lib/zeroos/net-caps`.

Everything else — nmap's connect scan, mtr, dig, ip, ss — runs as you, with
no extra privileges, and so does Share (through NetworkManager, which ZeroOS
already lets you manage).

Net Tools 2.0 used a passwordless `sudo` rule for arp-scan and tcpdump
(`/etc/sudoers.d/zeroos-net`). That was a mistake: tcpdump's `-z` option runs
any program, so the rule amounted to passwordless root for the home-screen user.
Updating to 2.1 removes the rule.

## A note on use

These are the ordinary tools a network admin uses to see what's on a network and
how it's reaching the internet. Scan and capture only on networks you own or are
allowed to test — port scanning and packet capture on networks you don't control
may be against the rules or the law where you are.

## On the simulator

In the PC simulator and the headless build there is no real network, so the tools
replay a small canned network (a router, a Pi, a few phones and laptops) so you
can see how each screen looks. On the device they run the real tools.
