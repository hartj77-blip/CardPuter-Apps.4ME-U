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
3. **Tab** switches to the info page and back. **Esc** on the info page goes back to the chat.

The chat shows the last 30 messages: yours in green, ones sent directly to you highlighted. The
top line shows your node's name and how many nodes it has heard.

The info page has:

| | |
| --- | --- |
| Turn on / off mesh | Start or stop the radio. It stays on after you leave the app |
| QR code | Scan it with your phone, on the same Wi-Fi, to chat from Meshtastic's web client |
| Open on monitor | The same web client in Dock mode's browser |
| GPS | Opens the GPS app |

The mesh stays on, and the expansion port stays powered, until you turn it off: that's what lets
messages arrive while you do other things, and it costs battery.

To change the channel, the region or your node's name, use the web client (from the QR code).

## How it works

The package installs `meshtasticd`, Meshtastic's Linux node, from Meshtastic's own Debian
repository on openSUSE's Build Service; `prepare.sh` fetches that repository's key at build time
and checks it against a pinned fingerprint. It also installs:

- `files/meshtasticd-cap-lora-1262.yaml`: the radio (an SX1262 on SPI, `/dev/spidev0.1`, with its
  reset, busy and interrupt pins), the web client on port 9443, and at most 100 nodes;
- `files/meshtasticd-zeroos.conf`: never start at boot (the app starts it), run at a lower
  priority, and be the first thing stopped if memory runs out.

ZeroShell talks to meshtasticd through its API (`hal().mesh()`), so the chat is drawn by the app,
not the web client.

## To confirm on hardware

- The Cap LoRa-1262's pins in the config file, taken from M5's pin map: they haven't been tried.
- Range and battery use with the mesh on.
