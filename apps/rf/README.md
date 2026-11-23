# RF Tools

One app for a USB software-defined radio dongle (RTL-SDR Blog V4 first), with nine tools inside:
**Spectrum**, **Scanner**, **Sensors**, **Aircraft**, **APRS**, **FT8**, **Logbook**, **SDR Server**
and **Station**. Installing RF Tools adds one icon to the home screen, with the radio programs the
tools drive; removing it takes them away again.

Open **RF Tools** and pick a tool from its menu: **1** to **9**, or Up/Down and **Enter**, or type
to filter. **Esc** in a tool comes back to the menu, and **Esc** on the menu leaves the app. The
menu remembers the tool you used last.

It hasn't been tried with a real dongle yet. So far it's been tested against a simulated radio and
recorded files.

| Part | What |
| --- | --- |
| `rf.cpp` | The app: its menu of tools (the menu itself is `apps/kit/suite.h`, shared with Net Tools). |
| `*.cpp`, `*.h` | The nine tools. One module, `rf.so`, since they share one connection to the radio daemon. |
| `core/` | The radio side: receiver backends, spectrum, FM and AM demodulation, the daemon `zrfd` and its protocol ([core/README.md](core/README.md)), and stand-ins for a simulated radio. |
| `files/` | What the package installs besides the programs: the first-run check, the dongle rules, the licences of code built in. |

## Spectrum

Left/Right tune by one step, Up/Down change the step size, **Z**/**X** narrow or widen the view,
**G** cycles gain, **P** toggles peak hold, **C** centres on the strongest signal. Type a frequency
in MHz and press **Enter** to jump to it, or press **B** for a list of bands.

**M** cycles listening: off, broadcast FM, narrow FM, AM, upper and lower sideband, CW. The first
press picks the mode that suits the frequency: the right sideband on an amateur band, CW at the
bottom of one, AM on the airband. The bottom line names the band you are in and, while listening,
shows signal strength above the noise. **S** toggles squelch. **R** records raw IQ
to `~/Recordings` (about 2 MB a second at the default width; it stops by itself before the card
fills, and when you retune). Audio and recording stop when you leave Spectrum.

**A** saves the channel you are on (type a name, **Enter**); **L** lists your saved channels, where
**Enter** tunes to one and **D** deletes it. They live in `~/.config/zeroos/rf-memories.tsv`, one
line each, and the Scanner can scan them.

**K** makes the radio keep playing after you leave Spectrum, so you can listen while writing notes
or browsing files. Opening Spectrum again picks the audio back up; opening another RF tool stops it,
because that tool needs the receiver. Spectrum reopens where you left it (frequency, width, step,
gain and squelch). **H** shows all the keys.

On broadcast FM the bottom line shows the station's name and text (from RDS) and whether it is in
stereo; headphones get both channels. **W** records what you are hearing to a WAV file in
`~/Recordings`, which the Media app plays and Sync copies off the device. With squelch on, only the
moments with a signal are recorded.

**F** follows a transceiver's dial through Hamlib, so the display becomes a panadapter for the
rig, and **T** sends the displayed frequency to the rig. Set the rig up in Station.

## Station

Your callsign and grid locator, the dongle's frequency correction (ppm), antenna power (bias tee),
the APRS frequency, and the rig for Hamlib (model number, serial port, speed). See
[HAM.md](HAM.md).

## APRS

Amateur packet stations on 144.390 MHz (**F** steps through other regions' frequencies and the
ISS): one row per station with distance and direction from your grid, and what it last sent.
**Enter** shows the packet. Uses `rtl_fm` and `multimon-ng`.

## FT8

The weak-signal digital mode. Every 15 seconds the stations heard on the band's FT8 frequency are
listed with signal-to-noise ratio, audio frequency and message. Stations calling CQ are
highlighted, a message that mentions your callsign more so, and a grid in a message becomes a
distance from yours. **Left**/**Right** change band (160 m to 2 m), **Up**/**Down** scroll back.
Receive only. The device's clock must be right to within about a second. Decoding is by
[ft8_lib](https://github.com/kgoba/ft8_lib); see [THIRD_PARTY.md](files/THIRD_PARTY.md).

## Logbook

Amateur radio contacts, kept as ADIF in `~/Documents/logbook.adi`, which every logging program,
Logbook of the World, QRZ and eQSL can import (and which Sync copies off the device). **N** starts
a contact with the frequency and mode Spectrum is on, the customary report (59, or 599 for CW) and
the time in UTC; typing a callsign you have logged before says so. **Enter** on a contact shows
it, and **D** there deletes it.

## Scanner

Sweeps a band, stops on the first channel with a signal, plays it until it goes quiet, then carries
on, keeping a log of what it heard. **Left**/**Right** change band (airband, weather radio, marine,
amateur, FRS/GMRS, PMR446, CB, broadcast FM) and, last in the list, **Saved channels**, which
checks each channel you saved in Spectrum in turn, **Space** skips the current channel, **H** holds on
it, **L** locks it out, **C** clears the log and lockouts.

## Aircraft

ADS-B reports from aircraft overhead on 1090 MHz: flight, altitude, speed, heading with a climb
or descent arrow, and position. Decoding is our own, from the raw frames `rtl_adsb` prints, or
`dump1090-mutability` when that is installed (it hears more).

## SDR Server

Lends the dongle to an SDR program on another computer or phone (SDR++, GQRX, SDR#) over Wi-Fi or
Tailscale, using `rtl_tcp` on port 1234. The screen shows the address to enter. While it is on,
anyone on the same network can tune the radio; it stops when you leave the app.

## Sensors

Wireless weather stations, tyre sensors, doorbells and remotes, decoded by
[rtl_433](https://github.com/merbanan/rtl_433). One row per device with its latest readings.
**Enter** shows everything the selected device sent, **F** changes band (433.92, 315, 868.3,
915 MHz), **C** clears the list. Readings are metric for now.

## On the device

Open **Apps**, pick **RF Tools** and press **Install**. Then plug in the dongle and run
`zeroos-rf-bringup` over SSH: it checks the dongle, the driver and the radio daemon, and says what
to send back if something fails.

The package has to be built on **Debian trixie**, the system the device runs. Debian and Ubuntu
ship the RTL-SDR library under different names (`librtlsdr.so.0` and `librtlsdr.so.2`), so a radio
program built on Ubuntu won't start on the device. The release builds on arm64 Debian trixie, and
`rf.pkg` refuses a `zrfd` linked to the wrong one.

## On a PC

The simulator and headless builds of this repository include RF Tools. Without a dongle, the apps
use a simulated radio:

```sh
sudo apt install librtlsdr-dev   # optional: lets zrfd use a real dongle
cmake -S . -B build/sim -DZS_BACKEND=sim && cmake --build build/sim -j
ZRFD_BIN=$PWD/build/sim/bin/zrfd ZRF_SOURCE=sim ./build/sim/zeroshell
```

`ZRF_SOURCE=rtlsdr` uses a real dongle, `ZRF_SOURCE=file:capture.u8` replays a recording. The
radio tests (`dsp`, `integration`) run in ctest.

## More

- [HAM.md](HAM.md): for amateur radio operators: bands, modes, rig control, what's missing.
- [files/THIRD_PARTY.md](files/THIRD_PARTY.md): other people's code built in, and its licences
  (FT8 decoding uses ft8_lib, MIT).
