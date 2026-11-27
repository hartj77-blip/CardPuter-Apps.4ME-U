# Maps

A map of New Jersey that works with no internet, and follows you as you drive when a GPS module is
plugged in. It shows how fast you're going, the road you're on and its speed limit.

The map comes with the app, made from [OpenStreetMap](https://www.openstreetmap.org) data
(© OpenStreetMap contributors, [ODbL](https://www.openstreetmap.org/copyright)). Nothing is downloaded
while you use it.

For your position it needs the same thing as the [GPS app](../gps/README.md): a Cap LoRa-1262 on the
expansion port, a Grove GPS unit, or a USB GPS dongle. **Install the GPS app too**: it sets up the
receiver. Without one, Maps is still a map you can scroll around.

## On screen

| Where | Shows |
| --- | --- |
| Top left | What the GPS is doing, until it has a position: starting, looking for satellites, or that you're outside the map |
| Top right | A compass: the red end points north |
| Middle | You: an arrow pointing the way you're going, or a dot when you're standing still |
| Bottom left | Your speed, and the speed limit of the road you're on in a white sign, when OpenStreetMap knows it. Your speed turns red when it is more than 5 mph (8 km/h) over the limit |
| Bottom middle | The name (or number) of the road you're on |
| Bottom right | The scale |

The map turns so that the way you're going is up, and shows more of the road ahead than behind.
**N** keeps north up instead. The screen stays on while you're moving faster than a walk.

## Keys

| Key | Does |
| --- | --- |
| **-** and **=** | Zoom out and in. The zoom you choose while following is kept for next time |
| Arrows (or **W A S D**) | Look around. The map stops following you |
| **Enter** | Follow me again |
| **N** | North up, or the way you're going up |
| **Tab** | The menu: also **T** day or night colours, **U** miles or kilometres, **I** about this map |
| **Esc** | Home |

Miles are the default on a US map. The last place you were is remembered, so the map opens there
before the GPS has a fix.

## Where the map is

| Path | What |
| --- | --- |
| `/usr/share/zeroos/maps/new-jersey.zmap` | The map the app comes with (about 60 MB) |
| `~/Maps/*.zmap` | Maps you add yourself: they come first |

Another app can open the map at a place: `shell_open_app("maps", "40.7357,-74.1724")`.

## How it works

The map is a `.zmap` file, a format of this app's own ([zmap.h](zmap.h)): the roads, railways, water,
woods, parks, the coastline, the state line and place names, at four levels of detail, cut into
tiles. The app maps the file into memory, so only the part on screen is read from the SD card, and
draws it itself ([render.cpp](render.cpp)): areas, then roads from the smallest to the largest
with their edges, then the names that fit. A screen takes a few milliseconds on a PC; on the
Cardputer the app redraws only when the picture would move by a pixel, and never spends more than
about a third of its time drawing.

The file is made when the package is built: [prepare.sh](prepare.sh) downloads Geofabrik's New
Jersey extract and its outline and runs [tools/zmap_build.cpp](tools/zmap_build.cpp). That reads the
OpenStreetMap data, builds the sea from the coastline (OpenStreetMap has no sea areas, only the
line where land meets it), simplifies everything for each level of detail and cuts it into tiles.
It takes about 15 seconds and half a gigabyte of memory, which is why it isn't done on the Cardputer.

To make a map of somewhere else, build `zmap-build` (it comes with the simulator build, in
`build/<dir>/bin/`) and run it on any extract:

```sh
zmap-build --name "Delaware" --poly delaware.poly --imperial delaware-latest.osm.pbf delaware.zmap
```

Put the result in `~/Maps` on the Cardputer. Geofabrik has extracts, and a `.poly` outline for each,
at [download.geofabrik.de](https://download.geofabrik.de).

The position comes from gpsd through the OS's hardware layer, as in the GPS app. Between fixes (a
GPS reports once a second) the app carries the position forward at your speed, so the map moves
smoothly. The road you're on is the nearest one that runs the way you're heading.

## Tests

`tests/maps_check.sh` builds a map of a made-up town ([tests/make-town.py](tests/make-town.py), whose
`.osm.pbf` is committed) and checks it: what each level holds, what colour each place is drawn,
names that don't overlap, the road and speed limit found at a point (and at a junction, by
heading), and hundreds of damaged copies of the file, which must never crash the app. The
simulated GPS walks through the town, so the simulator and CI's screenshots show it too.

## To confirm on hardware

- How long a screen takes to draw on the Cardputer, zoomed out over a city (the most work).
- That following a fix while driving is smooth, and the screen stays on.
- The speed limits and road names against real roads.
