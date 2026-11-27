# Maps

A map of New Jersey for the car that works with no internet, with spoken turn-by-turn directions.
Type a town or a street, and Maps plans the route, shows the next turn, and says it as you get
there. It also shows how fast you're going, the road you're on and its speed limit.

Everything comes with the app, made from [OpenStreetMap](https://www.openstreetmap.org) data
(© OpenStreetMap contributors, [ODbL](https://www.openstreetmap.org/copyright)). Nothing is downloaded
while you use it. The routes are planned by [Navit](https://www.navit-project.org), running in the
background; what you see is this app's own.

For your position it needs the same thing as the [GPS app](../gps/README.md): a Cap LoRa-1262 on the
expansion port, a Grove GPS unit, or a USB GPS dongle. **Install the GPS app too**: it sets up the
receiver. Without one, Maps is still a map you can scroll around, and it can show you a route from
the middle of the screen.

## Directions

1. Press **G** (Go to) and type. Every word you type must start a word of the place: `nassau`,
   `main st princeton`, `washington st hob`. Towns come first, then streets, the nearest first.
2. Pick one with the arrows and press **Enter**. The places you went to last are listed before you type.
3. Maps plans the route (a few seconds) and draws it in blue.

While you drive:

| Where | Shows |
| --- | --- |
| Along the top | The next turn: an arrow, how far, and what to do ("Turn right into Bridge Street") |
| Bottom middle | When you'll arrive, how long it will take, and how far is left |

You'll hear each turn twice: about half a minute before it ("In 500 feet, turn right into Bridge
Street") and as you get there. **V** turns the voice off and on. If you take another way, Navit
plans a new route from where you are, within a second or two. When you get there, Maps says so and
the directions end.

**X** stops the directions (it asks first). Directions that were going when you left Maps carry on
when you open it again.

The voice comes out wherever the sound goes: the speaker, headphones, or the car stereo through the
[Car app](../car/README.md)'s Bluetooth.

## On screen

| Where | Shows |
| --- | --- |
| Top left | What the GPS is doing, until it has a position: starting, looking for satellites, or that you're outside the map |
| Top right | A compass: the red end points north |
| Middle | You: an arrow pointing the way you're going, or a dot when you're standing still |
| Bottom left | Your speed, and the speed limit of the road you're on in a white sign, when OpenStreetMap knows it. Your speed turns red when it is more than 5 mph (8 km/h) over the limit |
| Bottom middle | The name (or number) of the road you're on; with directions, when you'll arrive |
| Bottom right | The scale |

The map turns so that the way you're going is up, and shows more of the road ahead than behind.
**N** keeps north up instead. The screen stays on while you're moving faster than a walk.

## Keys

| Key | Does |
| --- | --- |
| **G** | Go to: find a town or a street and get directions there |
| **X** | Stop the directions |
| **-** and **=** | Zoom out and in. The zoom you choose while following is kept for next time |
| Arrows (or **W A S D**) | Look around. The map stops following you |
| **Enter** | Follow me again |
| **N** | North up, or the way you're going up |
| **Tab** | The menu: also **V** voice on or off, **T** day or night colours, **U** miles or kilometres, **I** about this map |
| **Esc** | Home (in Go to: clear what you typed, then back to the map) |

Miles are the default on a US map. The last place you were is remembered, so the map opens there
before the GPS has a fix.

## Where things are

| Path | What |
| --- | --- |
| `/usr/share/zeroos/maps/new-jersey.zmap` | The map the app comes with (about 60 MB) |
| `/usr/share/zeroos/maps/new-jersey.search` | Go to's list of towns and streets (about 7 MB) |
| `/usr/share/zeroos/maps/new-jersey.navit` | The roads, for Navit to plan routes on (about 26 MB) |
| `~/Maps/` | Maps you add yourself (the same three files, with the same name before the dot): they come first |
| `~/.config/zeroos/settings.conf` | The places you went to last (`maps.recent`), where directions were going (`maps.dest`), and the app's settings |

Another app can open the map at a place: `shell_open_app("maps", "40.7357,-74.1724")`.

## How it works

### The map

The map is a `.zmap` file, a format of this app's own ([zmap.h](zmap.h)): the roads, railways, water,
woods, parks, the coastline, the state line and place names, at four levels of detail, cut into
tiles. The app maps the file into memory, so only the part on screen is read from the SD card, and
draws it itself ([render.cpp](render.cpp)): areas, then roads from the smallest to the largest
with their edges, then the route, then the names that fit. A screen takes a few milliseconds on a
PC; on the Cardputer the app redraws only when the picture would move by a pixel, and never spends
more than about a third of its time drawing.

The files are made when the package is built: [prepare.sh](prepare.sh) downloads Geofabrik's New
Jersey extract and its outline and runs [tools/zmap_build.cpp](tools/zmap_build.cpp). That reads the
OpenStreetMap data, builds the sea from the coastline (OpenStreetMap has no sea areas, only the
line where land meets it), simplifies everything for each level of detail and cuts it into tiles.
It also writes the search list (each place, and each named road once in each town it runs
through) and the roads a car may use, with their turn restrictions, which Navit's `maptool` turns
into Navit's routing map. It all takes under a minute and about half a gigabyte of memory, which is
why it isn't done on the Cardputer.

To make maps of somewhere else, build `zmap-build` (it comes with the simulator build, in
`build/<dir>/bin/`) and run it on any extract:

```sh
zmap-build --name "Delaware" --poly delaware.poly --imperial --search delaware.search --roads delaware.osm delaware-latest.osm.pbf delaware.zmap
```

```sh
maptool -i delaware.osm delaware.navit
```

Put `delaware.zmap`, `delaware.search` and `delaware.navit` in `~/Maps` on the Cardputer. Geofabrik has
extracts, and a `.poly` outline for each, at [download.geofabrik.de](https://download.geofabrik.de).

### Directions

[nav.cpp](nav.cpp) runs Navit for the app, on a thread of its own, only while there are directions:

- **A screen and a bus of its own.** Navit won't take requests without a screen, so the app starts
  Xvfb, a 64 by 64 screen in memory that nobody sees, a private `dbus-daemon`, and then `navit`
  with a settings file it writes for that run. Navit's own car settings come from Debian's
  `/etc/navit/navit.xml`. Together they take about 110 MB of memory; when the directions end,
  all three stop again.
- **Your position goes in.** Each GPS fix goes to Navit as NMEA sentences, through a pipe, as if
  from a GPS of its own.
- **The route comes out.** The app asks over D-Bus (`dbus-send`) how the route is doing and how far
  and how long is left. When Navit has a new route, the app has it write the route's roads to a
  file (`map.dump`) and the turns to another (`export_as_geojson`), and draws them
  ([nav_parse.cpp](nav_parse.cpp) reads both).
- **The app does the talking.** It works out the next turn from where you are on the route, shows
  it, and says it with eSpeak NG at the right moments. Navit's own words are only listened to for
  arriving.

In the simulator a stand-in plans an L-shaped route instead, so the screens can be tried with no
Navit. On a PC with Navit, `ZS_MAPS_NAVIT=1` uses the real one.

The position comes from gpsd through the OS's hardware layer, as in the GPS app. Between fixes (a
GPS reports once a second) the app carries the position forward at your speed, so the map moves
smoothly. The road you're on is the nearest one that runs the way you're heading.

## Tests

`tests/maps_check.sh` builds the files for a made-up town ([tests/make-town.py](tests/make-town.py),
whose `.osm.pbf` is committed) and checks:

- the map: what each level holds, what colour each place is drawn, names that don't overlap, the
  road and speed limit found at a point (and at a junction, by heading), and hundreds of damaged
  copies of the file, which must never crash the app;
- the search, and the roads written for Navit;
- reading Navit's answers: the route's line, the turns and their words, the position sent to it;
- a real route: where Navit is installed (CI installs it), Navit plans one across the town, and
  nothing of it is left running afterwards.

The simulated GPS walks through the town, so the simulator and CI's screenshots show it too.

## To confirm on hardware

- How long a screen takes to draw on the Cardputer, zoomed out over a city (the most work).
- How long Navit takes to start and to plan a long route (Princeton to Hoboken) on the Cardputer.
- That following a fix while driving is smooth, and the screen stays on.
- That the voice is heard, and in time for each turn, at highway speed.
- The speed limits and road names against real roads.
