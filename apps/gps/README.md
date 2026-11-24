# GPS

Where you are, how fast you're going, and a record of where you've been, with a GPS module.

It works with:

- the **Cap LoRa-1262** on the expansion port, which has a GPS receiver as well as the LoRa radio;
- a **Grove GPS unit** on the Grove port;
- a **USB GPS** dongle.

Power the Cardputer off before you plug a module in. The OS's
[expansion guide](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/EXPANSION.md)
has the details.

| Shown | |
| --- | --- |
| Top line | Starting, looking for satellites, 2D fix or 3D fix |
| Latitude, longitude | In degrees, to six places |
| Altitude | Only with a 3D fix |
| Speed, Heading | Heading once you're moving faster than about 2 km/h |
| Accuracy | The receiver's own estimate, in metres |
| Satellites | Used of seen |
| UTC | The time from the satellites |

The keys are in the soft-key bar along the bottom and in the menu on **Tab** (Ctrl + the letter
runs one straight away):

| Key | Does |
| --- | --- |
| Enter (menu: M) | Save this spot: a line in `~/Documents/waypoints.txt` with the time, position and altitude |
| R | Start or stop recording a track, as GPX in `~/Documents/Tracks/` |
| Esc | Home |

**Tracks keep recording** after you leave the app, with the screen off, until you press **R**
again. The top-right corner shows REC, the distance and the time while one records. A track that
never got a fix is saved empty, and the app says so. GPX opens in almost any map app, and
**Sync** copies Documents to your PC.

A first fix outdoors takes about 30 seconds; indoors, often never. The expansion port is powered
only while the app is open (or a track records, or Mesh is on), so each time you open GPS the
receiver starts afresh.

## How it works

The package installs [gpsd](https://gpsd.io) and points it at the UART, `/dev/serial0`, at
115200 baud, and at USB GPS dongles (`files/gpsd.default`, which replaces gpsd's
`/etc/default/gpsd` and is put back when you remove the app). gpsd starts only when something
connects to it (`gpsd.socket`), and only reads the receiver while someone is listening.

The app turns on the expansion port's 5 V when it opens and reads fixes through the OS's hardware
layer (`hal().gps()`). Track recording is part of ZeroShell (`ui/tracker.h`), which is why it
outlives the app.

The Mesh radio doesn't get the GPS position: meshtasticd can't read gpsd, only a serial port, and
two programs on one UART garble each other. Set a fixed position in Meshtastic's web client if
you want your node on the map.

## To confirm on hardware

- That the Cap LoRa-1262's GPS answers on `/dev/serial0` at 115200.
- A Grove GPS unit at its own speed (gpsd should find it).
- That the port's power switching doesn't upset a fix in progress.
