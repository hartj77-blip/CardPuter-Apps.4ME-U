# Mesh

Messages over LoRa radio with [Meshtastic](https://meshtastic.org): no phone signal, no internet.
Every Meshtastic node in range (often several kilometres, further from high ground) passes
messages on, so they travel well beyond one radio's reach.

It needs M5Stack's **Cap LoRa-1262** on the expansion port. Power the Cardputer off before you
plug it in.

What it does, in short:

- **Chats**: every channel and every person you've written to, with unread counts. Messages arrive
  and are kept while the app is closed.
- **Private messages**, encrypted to the other person's key, with honest delivery marks (sending,
  passed on, delivered, or not sent and why) and **Send again**.
- **Replies, reactions, quick replies, the bell, sending where you are**, and **finding any
  message** you've ever had.
- **Nodes**: everyone heard, sorted four ways, filtered four ways, typed to find; each with its
  signal, hops, distance, battery and sensors, **traceroute**, and asking for its position or
  telemetry.
- **Map**: who is where around you, nearest first, with distance and bearing.
- **Channels**: make one with a fresh key and **share it as a QR code**; join one by **scanning a
  QR code with the camera** or pasting a link.
- **First-run setup**: where you are and your name, and you're on the air.
- **Phones too**: the Meshtastic app and web page can use the radio at the same time as Mesh.

## Using it

1. Open **Mesh** and choose **Turn on mesh**. The radio starts in a few seconds.
2. The first time, Mesh asks **where you are** (the radio's legal frequencies) and **your name**.
   Nothing is sent until you choose **Start**. **Esc** puts it off; the status line then says
   **No region: nothing is sent** until you choose one in **Radio**.
3. The app has four tabs, **Left** and **Right** between them: **Chats**, **Nodes**, **Map** and
   **Radio**. The right of the tab line says how the radio is ("ZERO | 7 online", "Connecting...",
   "No region").

The keys are the ones every ZeroOS app uses: **Enter** does the main thing, **Esc** goes back one
step, **Tab** opens the menu of everything the screen can do (and **Ctrl + its letter** does it
straight away), **1** to **9** pick a row, **0** shows every key. In a list, typing filters it.

### Chats

The channels come first, then the people you've written to, newest first. A row says what was said
last, or how many are new. **Tab** has **New message** (pick someone), **Find in messages**,
**Mark all read** and **Save all to Documents**.

In a chat, type and press **Enter** to send. What you type is kept if you leave the chat before
sending. Your messages carry a mark:

| Mark | Means |
| --- | --- |
| `...` | Sending: the radio has it |
| one tick | Passed on: another node was heard repeating it. On a channel that's all there ever is |
| two ticks | Delivered: the person it was for confirmed it (private messages) |
| `Not sent: ...` in red | Why: no one confirmed it, they don't have our key yet, sent too fast, no region... |

**Up** and **Down** (with nothing typed) pick a message. Then **Enter** shows its details (when,
how it came, the signal, how it was encrypted), and **Tab** has **Reply to it** (R), **React to
it** (E: thumbs up, heart, laughing...), **Send again** (A, for one that wasn't sent). **Esc**
lets the message go. **Up** past the first one shown brings older ones.

Also in a chat's **Tab** menu: **Quick reply** (Q: your own list of ready messages, 1 to 9 send
one; N, E and D there make, change and delete them), **Ring their bell** (B: the next message
sounds the alert on their radio), **Where I am** (W: a named waypoint with your position, shown on
their maps for 8 hours), **About this node** (I, private chats), **Save to Documents** (S) and
**Delete this chat's messages** (D, asks first).

The Cardputer's fonts have no emoji: common ones show as words or faces (`:thumbsup:`, `<3`, `:D`),
and the rest as `?`. Reactions you send are real emoji, as phones expect.

### Nodes

Everyone the radio has heard: short name, long name, then how (`+6 dB` heard directly, `2 hops`,
`MQTT`) and when. Nodes not heard for two hours are greyed. Favourites come first. **Tab, S**
sorts by last heard, name, how close on the mesh, or distance; **Tab, V** shows everyone, heard in
two hours, heard directly, or favourites. Both are remembered. The order stays put while you look
(values keep up); **Tab, R** sorts again.

**Enter** on a node opens its page: **Message**, **Trace the route**, **Ask where it is**, **Ask
how it's doing** (battery and the like), **Favourite**, **Ignore its messages**, then all that's
known about it (signal, distance and bearing, position, battery, channel use, uptime, temperature,
humidity and pressure if it has sensors, role, hardware, whether it has a key for private
messages). **Tab** has **Ask who it is** and **Remove from the list**.

**Trace the route** shows the nodes a message passes through each way, and how well each heard the
one before: `Out: You > HILL +6.5 dB > BASE +3.0 dB > MRK -0.5 dB`. The radio allows one traceroute
every 30 seconds; Mesh says how long to wait instead of failing.

### Map

Nodes that share a position, around yours: a circle with you in the middle (north up), the scale
bottom left, the nodes as dots (green: heard in two hours). **Up** and **Down** go through them,
nearest first, and the panel on the right names the one picked, how far and which way. **+** and
**-** zoom, **Tab, F** fits everyone, **Enter** opens the node's page. Without a position of your
own the middle is everyone's middle.

### Radio

| Row | What |
| --- | --- |
| Your name | The long name others see, and a four-letter one |
| Channels | Your channels: open one to share it as a QR code, open its chat, rename it, give it a new key, or send it to the internet (MQTT). **New channel** makes one with a new key. **Join with a QR code** reads one with the camera; **Tab, J** takes a link instead. **Tab, S** shares all of them at once |
| Share my position | Off, about 3 km, about 350 m, or exact. Mesh gives the radio the GPS fix (Maps' GPS module), the way a phone does, and the radio sends it on your first channel at that precision. It keeps doing so while the app is closed |
| Phones on this network | Blocked or Allowed: whether phones on the Wi-Fi you're on can use the radio (Tailscale always can). Back to Blocked when the mesh goes off |
| Use it from a phone | A QR code for Meshtastic's web page, and the address for the Meshtastic app (**+**, a network device) |
| Region, Range and speed, Hops, Role, Transmit | The radio's own settings. A change restarts the radio, which takes a few seconds; Mesh reconnects by itself |
| Open on monitor | Meshtastic's web page in Dock mode's browser |
| What it's hearing | Every packet in and out, as it happens |
| About this radio | Firmware, region, airtime, channel use, packets, noise floor, nodes |
| GPS | Maps, on its GPS screen |
| Turn off mesh | The radio and the expansion port off |

**Tab** on the Radio tab also has **Restart the radio**, **Forget every node** (favourites stay)
and **Delete every message**; the last two ask first.

The mesh stays on, and the expansion port stays powered, until you turn it off. That's what lets
messages arrive while you do other things, and it costs battery.

### Where it keeps things

| | |
| --- | --- |
| `~/.local/share/zeroos/mesh/messages.log` | Every message, and what became of yours. Yours only (mode 600). Up to 2,000 per chat are kept |
| `~/.local/share/zeroos/mesh/read.tsv` | What you've read |
| `~/.config/zeroos/mesh-quick.txt` | Your quick replies, one per line: edit it anywhere |
| `~/.config/zeroos/mesh.conf` | Whether to share your position (for the daemon) |
| `~/Documents/Mesh/` | Chats you saved, as text |

Removing Mesh leaves these. Mesh itself sends nothing anywhere but over the radio.

## How it compares

Before this version we looked at every Meshtastic app for the Cardputer Zero in M5Stack's store
(MeshZero, MeshTandem, Camillia, Plai, CardMesh) and at Meshtastic's own apps, web page,
on-device screens and the `contact` terminal client. Mesh takes what they do best and adds what
none of them has on this device:

- **The radio is shared, not taken.** Camillia, Plai and CardMesh drive the radio chip
  themselves: while one runs, nothing else can use the radio, and it stops when you leave the app.
  Mesh uses Meshtastic's own Linux node, and its daemon lets the app, ZeroShell's message notes
  and a phone all use it at once, app open or not (MeshZero does this too).
- **Find in every message**, up to 2,000 per chat (MeshZero shows the last 60, and none of them
  can search).
- **Join a channel by scanning its QR code** with the Cardputer's camera, as well as sharing one.
- **Delivery you can believe**: "passed on" and "delivered" are different marks, every failure says
  why, and a message the radio never answers for is marked as not sent after five minutes instead of
  waiting forever.
- **A first-run setup that sends nothing until it's done**, and a status line that says when the
  radio can't send (no region, transmit off) before you try.
- **The keys every other ZeroOS app uses**, with every action in the Tab menu, instead of a
  vocabulary of letters to learn.

What it doesn't do yet: map tiles (the Map is a radar of who is where), charts of telemetry over
time, remote administration of other nodes, and the less common radio settings (custom modem,
frequency slot, MQTT server, modules). Meshtastic's web page has those: **Radio > Use it from a
phone**, or **Open on monitor**.

## How it works

```
 Mesh app ─┐
 ZeroShell ─┼─ port 4403 ─ zeroos-meshd ─ port 4404 ─ meshtasticd ─ SX1262 on the Cap LoRa-1262
 phones   ──┘   (anyone)    (history,      (this device                  (SPI, /dev/spidev0.1)
                             GPS)            only)
```

- **meshtasticd** is Meshtastic's Linux node, from Meshtastic's own Debian repository on openSUSE's
  Build Service (`prepare.sh` fetches that repository's key and checks it against a pinned
  fingerprint). Its client API takes one connection at a time: a second one cuts the first off. So
  Mesh moves it to port 4404 (`files/meshtasticd-cap-lora-1262.yaml`, `General: APIPort`).
- **zeroos-meshd** (`core/meshd.cpp`, `core/hub.cpp`) holds that one connection and answers port
  4403 for everyone else. Each client's request for the radio's settings and node list is passed to
  the radio one at a time and the answer goes back to whoever asked; everything else the radio says
  goes to every client; a message one client sends is shown to the others. It writes every message,
  and what became of each one you sent, to the history file. With **Share my position** on it reads
  gpsd and gives the radio your position every two minutes (sooner after moving 100 m). It runs as
  you (`files/zeroos-meshd.service`, `User=1000`), starts with meshtasticd and stops with it.
- **The app** (`mesh.cpp`, `chats.cpp`, `nodes.cpp`, `radio.cpp`) is a client like any other: it
  connects to port 4403, reads the history as it grows, and never blocks the screen.
- **The protocol** (`core/proto.cpp`, `core/pb.h`) is hand-written: Meshtastic's protobuf messages
  without a protobuf library. Settings are changed field by field, keeping whatever a newer
  meshtasticd sends that Mesh doesn't know about. `tests/proto_test.cpp` checks it byte for byte
  against messages made by Google's protobuf library from Meshtastic's own `.proto` files
  (`tests/make_vectors.py` makes `tests/vectors.h`).

The rest of the package is as before 2.0:

- `files/meshtasticd-cap-lora-1262.yaml`: the radio (an SX1262 on SPI, `/dev/spidev0.1` at 1 MHz,
  with its reset, busy and interrupt pins, DIO2 as the RF switch and a 3.0 V TCXO on DIO3), the
  web client on port 9443, and at most 100 nodes. The radio settings are the ones in Meshtastic's
  own config for this board (`lora-cardputer-zero.yaml`) and in M5's LoRa app;
- `files/zeroos-mesh-cap-power` (as `/usr/lib/zeroos/mesh-cap-power`): run as root before every
  start of meshtasticd. It turns on the expansion port's 5 V and drives P0 of the cap's IO
  expander (a PI4IOE5V6408 at I2C 0x43 on `/dev/i2c-1`) high: that pin is the antenna switch;
- `files/meshtasticd-zeroos.conf`: power the cap first, never start at boot (the app starts it),
  start zeroos-meshd with it, run at a lower priority, and be among the first stopped if memory
  runs out;
- `files/zeroos-mesh-firewall` and `files/zeroos-mesh-lan.service`: nothing on these ports asks
  for a login, so an nftables table (`inet zeroos_mesh`) keeps port 4404 to this device always, and
  ports 4403 and 9443 to this device and your tailnet until you allow phones.

### In the simulator

With no radio, Mesh runs against `core/fake_radio.cpp`: a stand-in for meshtasticd that answers
the same bytes the real one does (the settings download, queue status, acks, traceroutes,
positions, telemetry, settings changes and the restart they cause, its two-second limit between
texts and thirty between traceroutes), with ten made-up nodes in an Alpine valley who talk now and
then and answer you. The tests drive the client and the daemon with it.

## How it is tested

| Test | What it checks |
| --- | --- |
| `tests/proto_test.cpp` | The codec, both ways, against Google's protobuf library's bytes for 58 Meshtastic messages; settings changed without losing unknown fields; framing; channel links; malformed input |
| `tests/client_test.cpp` | The client against the stand-in radio: the settings download; channel and private messages and each delivery mark; a node out of reach; the radio's two-second limit (three messages typed at once all go, in order); no region; first-run setup with its restart; traceroute, position, telemetry and "who are you"; every radio setting; favourites, ignoring, removing; channels made, shared, joined, renamed, deleted and replaced; unread marks; history export and deletion |
| `tests/hub_test.cpp` | The daemon over real sockets: the app, ZeroShell and a phone at once, with one connection to the radio; a message from each seen by the others and in the history; messages kept while the app is closed; the radio restarting under everyone; no radio at all; the GPS given to the radio, and let go |

## To confirm on hardware

- Everything above has run against the stand-in radio, and the daemon against a stand-in
  meshtasticd over real sockets, but not yet against the real meshtasticd on a Cardputer.
  In particular:
  - meshtasticd 2.7 taking `APIPort: 4404` from `config.d` (its source says it does);
  - ZeroShell's own message notes through zeroos-meshd;
  - the Meshtastic phone app through zeroos-meshd while Mesh is open;
  - the QR reader on a channel code shown by a phone;
  - Share my position from Maps' GPS module, and the precision the channel sends.
- Updating from Mesh 1.x with the mesh on: the app notices that the daemon isn't running and
  restarts the radio once, which puts both on their new ports.
- If the app says **No radio** or **Radio error**, `journalctl -u meshtasticd -b` shows
  `zeroos-mesh:` lines from the power-up step, then meshtasticd's own errors;
  `journalctl -u zeroos-meshd -b` shows the daemon's.
