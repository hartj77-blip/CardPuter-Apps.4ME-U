# Mesh

Text messages over LoRa radio with [Meshtastic](https://meshtastic.org), with no phone signal and
no internet. Every Meshtastic node in range (often several kilometres, further from high ground)
passes messages on, so they can travel well beyond one radio's reach.

It needs M5Stack's **Cap LoRa-1262** on the expansion port. Power the Cardputer off before you
plug it in.

## Using it

1. Open **Mesh** and choose **Turn on mesh**. The radio service starts; it takes a few seconds.
2. The chat page opens. Type a message and press **Enter** to send it to everyone on the primary
   channel. You can type while the radio is still connecting; it's sent once it's ready.
3. **Left** or **Right** (while the message box is empty) switches to the info page and back,
   as do **Node info** and **Chat** in the menu (**Tab**, or **Ctrl + I** and **Ctrl + C**).
   **Esc** on the info page goes back to the chat.

The chat shows the last 30 messages: yours in green, ones sent directly to you highlighted. The
top line shows your node's name and how many nodes it has heard.

The info page has:

| | |
| --- | --- |
| Turn on / off mesh | Start or stop the radio. It stays on after you leave the app |
| Allow phones / Block phones | Let phones on this network reach the web client and API, until the mesh goes off. Without it, only the Cardputer and your tailnet can |
| QR code | Scan it with your phone, on the same Wi-Fi, to chat from Meshtastic's web client (after **Allow phones**) |
| Open on monitor | The same web client in Dock mode's browser |
| GPS | Opens the GPS app |

The mesh stays on, and the expansion port stays powered, until you turn it off: that's what lets
messages arrive while you do other things, and it costs battery.

To change the channel, the region or your node's name, use the web client (from the QR code).

## How it works

The package installs `meshtasticd`, Meshtastic's Linux node, from Meshtastic's own Debian
repository on openSUSE's Build Service; `prepare.sh` fetches that repository's key at build time
and checks it against a pinned fingerprint. It also installs:

- `files/meshtasticd-cap-lora-1262.yaml`: the radio (an SX1262 on SPI, `/dev/spidev0.1` at 1 MHz,
  with its reset, busy and interrupt pins, DIO2 as the RF switch and a 3.0 V TCXO on DIO3), the
  web client on port 9443, and at most 100 nodes. The radio settings are the ones in Meshtastic's
  own config for this board (`lora-cardputer-zero.yaml`) and in M5's LoRa app;
- `files/zeroos-mesh-cap-power` (as `/usr/lib/zeroos/mesh-cap-power`): run as root before every
  start of meshtasticd. It turns on the expansion port's 5 V and drives P0 of the cap's IO
  expander (a PI4IOE5V6408 at I2C 0x43 on `/dev/i2c-1`) high: that pin is the antenna switch.
  The expander forgets this whenever the 5 V goes off, which is why it runs every time. An older
  cap with no expander just gets the 5 V;
- `files/meshtasticd-zeroos.conf`: run that script first, never start at boot (the app starts
  it), run at a lower priority, and be the first thing stopped if memory runs out. It also runs
  `zeroos-mesh-firewall close` before every start and `clear` after it stops;
- `files/zeroos-mesh-firewall` (as `/usr/lib/zeroos/mesh-firewall`) and
  `files/zeroos-mesh-lan.service`: meshtasticd's web client and API have no login, so an
  nftables table (`inet zeroos_mesh`, from the `nftables` package) drops ports 9443 and 4403
  unless they arrive on `lo` or `tailscale0`. **Allow phones** starts `zeroos-mesh-lan.service`
  (polkit lets `pi` start exactly this unit, from ZeroOS with the matching rule), which empties
  the table. It is bound to meshtasticd, so turning the mesh off ends it.

Up to Mesh 1.0.1 the radio couldn't work: the config set the TCXO to 1.8 V (`DIO3_TCXO_VOLTAGE:
true`) instead of 3.0 V, and nothing switched the antenna in. Mesh 1.0.2 fixes both.

ZeroShell talks to meshtasticd through its API (`hal().mesh()`), so the chat is drawn by the app,
not the web client.

## To confirm on hardware

- The radio settings now match Meshtastic's and M5's own configs for this cap, but ZeroOS hasn't
  run them yet. If the app says **No radio** or **Radio error**, `journalctl -u meshtasticd -b`
  shows `zeroos-mesh:` lines from the power-up step, then meshtasticd's own errors.
- Range and battery use with the mesh on.
- **Allow phones**: a phone on the same Wi-Fi can't open the web client before it, and can after.
