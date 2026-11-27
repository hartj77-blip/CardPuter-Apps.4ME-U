# Jellyfin

The **Jellyfin** app plays music and video (movies, shows, home videos...)
from every library on your [Jellyfin](https://jellyfin.org) server, and looks
and works like Jellyfin's own apps: the near-black dark theme, the
purple-to-blue logo, **cover art everywhere**, a home screen of card rows
(My Media, Continue Watching, Next Up, what is new in each library), poster
grids, a page for each album, film and episode, a search that finds as you
type, and a now-playing bar along the bottom.

Nothing waits on the server with the screen frozen: lists, searches and
covers are fetched in the background and the screen fills in as they arrive.

It used to be the Jellyfin tab of [Media](../media/README.md). Media is now
for your own files (the SD card, USB drives, server shares). If you signed in
there, you're still signed in here.

## Sign in

Open **Jellyfin** and press **Enter** on **Sign in**. Three questions, as in
Jellyfin's own apps:

- **Server address**: its name or address. For a bare name such as `nas` or
  `192.168.1.20`, the Cardputer looks for the server over **https** first
  (`https://nas`, then `https://nas:8920`, Jellyfin's https port) and only
  then over plain **http** on Jellyfin's usual port, `http://nas:8096`. Type
  the whole address, such as `https://music.example.com`, for anything else;
  an address you type with `http://` is used as it is. Before the password is
  sent, the Cardputer asks the server for its public info (nothing secret), so
  a server that moves http to https on the same host is signed in to over https.
- **User** and **Password**: the ones you use in Jellyfin's own apps.

**Unencrypted servers.** Over plain http, the password, the sign-in token and
what you play cross the network as they are, readable by anyone on it. The
password question says *sent unencrypted* when you typed `http://`, and
signing in says *not encrypted (http)* when the server only answered over
http. That is fine on your own home network or over Tailscale (which encrypts
everything, see below); on other networks, give the server https (a reverse
proxy, or Jellyfin's own https setting) and type its `https://` address. A
server whose https certificate isn't trusted (self-signed) is refused over
https: use its http address on a network you trust instead.

Requests don't follow redirects to other addresses, so the token and password
only ever go to the server you signed in to; a sign-in the server redirects
says *The server moved; type its full address*. The token is handed to curl
and mpv in files only you can read, never on a command line other programs
can see, and never in an address.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** (in the menu, **Tab**) forgets it, and with it the covers and the
searches the Cardputer kept, and stops music that is playing from the server;
you can also remove "Cardputer Zero" under Devices in Jellyfin's dashboard. If the server stops taking the token (you
removed the device there, or changed your password), the app says so and
shows the sign-in screen again.

## Home

```
 [=] jellyfin                                    Q (J)
 My Media
 +---------+ +---------+ +---------+ +----
 | (cover) | | (cover) | | (cover) | | (co
 +---------+ +---------+ +---------+ +----
    Music       Movies      Shows      Ho
 Continue Watching            38m left · S2E2 Reading
 +---------+ +---------+
 | (still) | | (still) |
 |====-----| |=======--|
 +---------+ +---------+
  Bench Talk    Sintel
 > Low Battery Blues - Bench Tapes           0:42 / 4:03
```

The rows, top to bottom, each scrolling sideways:

- **My Media**: every library on your server, by the names you gave them,
  each with its own picture.
- **Continue Watching**: what you started on any device, with a blue line
  for how far you got. Enter carries on from there.
- **Next Up**: the next episode of each show you are watching. Enter plays it.
- **Latest in ...**: the newest films, shows, albums or videos of a library,
  for the first six libraries of those kinds.
- **Browse**: Albums, Artists, Playlists, Songs and Favourites, across all
  your libraries.

The title line says what the card in focus is: a name, a year, how long is
left. After you stop a video, Continue Watching and Next Up are asked for
again, so they follow what you just watched.

| A library of | Opens as |
| --- | --- |
| Music | a grid of its albums |
| Movies | a grid of its posters |
| Shows | a grid of its shows; a show opens its seasons, a season its episodes (a show with one season goes straight to the episodes) |
| Anything else (home videos, music videos, books, mixed, collections) | its folders and files, folders first |

Live TV libraries aren't listed: there's no tuner support.

## Cover art

Every card shows the picture Jellyfin's own apps would: an album's cover, a
film's poster, an episode's still, a library's picture, an artist's photo. A
track shows its album's cover. In lists of mixed things the picture is
cropped to the card's shape, evenly from both sides.

- **Watched** films and episodes have a blue tick in the corner; a show or
  season with episodes left shows how many; something you stopped part-way
  has a blue line along the bottom.
- Something with **no picture** on the server gets Jellyfin's coloured card
  with its name on it.
- The server scales each picture down to the size of the card before sending
  it (a poster is 46 x 69 pixels, a few kilobytes), so covers cost little
  data, and they are fetched several at a time.
- Covers are **kept** in `~/.cache/zeroos/jellyfin` (readable only by you),
  so the second look at a screen shows them at once and asks the server for
  nothing. The folder is kept under 24 MB (the oldest go first), a cover that
  changes on the server is fetched again, and signing out empties it.

## Search

**F** on the home screen (or **Search** in any menu) opens the search page.
Type, and the results arrive as you type, a row for each kind of thing, in
the order of Jellyfin's own search page: Movies, Shows, Episodes, Videos,
Collections, Playlists, Artists, Albums, Songs, Audiobooks.

- The server is asked once for each kind, so 300 songs with "love" in the
  name can't crowd out the one film. It is asked when you pause typing, not
  for every letter.
- What matches is the server's decision, as in Jellyfin's own apps.
- If several words find nothing, the longest is tried on its own: `sintel
  2010` finds *Sintel*, and the row's title says what it searched for.
- With nothing typed, the page lists your **recent searches** (Enter on one
  types it again) and **suggestions** from the server.

| Key | Does |
| --- | --- |
| Letters, digits, Space | Type |
| Up / Down | Move between the rows of results |
| Left / Right | Move along a row (in the text, while there are no results) |
| Enter | Open the card in focus; with no results yet, search now |
| Backspace | Take a letter off; with the box empty, back |
| Esc | Clear the box; with the box empty, back |

In a library you don't need the search page: **type** and the grid narrows
to what matches (name, artist, album or year), as every ZeroOS list does.

## Keys

The soft-key bar along the bottom shows the keys for the screen you're on;
**Tab** opens the menu with everything (Ctrl + a letter runs one straight
away), **0** the key sheet.

On the **home screen**, letters are shortcuts:

| Key | Does |
| --- | --- |
| Arrows | Move between cards and rows |
| Enter | Open; play, on Continue Watching and Next Up |
| F | Search |
| Space | Pause / resume the music |
| N / P | Next / previous track |
| + / - | Volume |

A **library** (a grid of covers):

| Key | Does |
| --- | --- |
| Arrows | Move; the line above the grid names the card in focus |
| Enter | Open |
| Letters | Narrow the grid to what matches (Backspace takes a letter off, Esc all of them) |
| Space | Pause / resume the music (while nothing is typed) |
| Backspace or Esc | Back |
| Tab | The menu: Pause (U), Sort (S), Search (F), Next track (N), Previous track (P), Now playing (Y), Stop music (T), Home (H), Refresh (R), Sign out (O) |

**Sort** steps through by name, newest first, by year and shuffled. A long
library loads 100 at a time on its own, to the end (up to 5 000), while you
look at the start of it; the header says how many there are.

An **album or playlist** (its cover beside its tracks) is a ZeroOS list:

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Play from this track |
| 1 to 9 | Play the row with that number |
| Letters | Narrow the list to the rows that match |
| Space | Pause / resume (while nothing is typed) |
| Backspace or Esc | Back |
| Tab | The menu, with also: Play all (A), Shuffle (S), Favourite (V) |

Everywhere: Fn + Q pauses or resumes, Fn + E / Fn + W skip to the next or
previous track.

## Playing music

Playing a track queues the rest of the album or playlist after it. The playing
track is marked with a speaker, and the bar along the bottom shows it with
its cover, its time and a blue progress line. **Now playing** in the menu
(**Tab**, then **Y**) opens the player: the cover large, the track, artist
and album, and the time.

| Key, in the player | Does |
| --- | --- |
| Space or Enter | Pause / resume |
| Left / Right, or P / N | Previous / next track |
| Up / Down, or + / - | Volume |
| Tab | The menu: Open the album (A), Search (F), Stop music (T) |

The queue belongs to ZeroShell, not the app, so music carries on to the next
track after you leave the app and while the screen is off. Wi-Fi stays on
while something is playing. The media keys (Fn + Q, W, E) and a Bluetooth
headset's or car's buttons work everywhere.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3.

## Watching video

A film, an episode or a video opens its **page** first, as in Jellyfin's own
apps: the poster or still, the year, how long it is, its rating, whether you
have watched it, and its description.

```
 < Sintel
 +------+  Sintel
 |      |  2010 · 15m · PG · 7.4 / 10
 |poster|  A lonely young woman, Sintel, helps and
 |      |  befriends a dragon, whom she calls Scales...
 +------+
 [> Resume 5:00] [Restart] [Watched] [Favourite]
```

| Key, on the page | Does |
| --- | --- |
| Enter | Press the button in focus: Play (or Resume, where you stopped) |
| Left / Right | Choose a button |
| Space | Play, or resume |
| R | From the start |
| W | Mark as watched, or not |
| V | Add to favourites, or take off |
| Backspace or Esc | Back |

The video plays across the app area above the soft-key bar (16:9 fills
242 x 136 pixels), with the sound on the speaker, the headphone jack or
Bluetooth, whichever music would use.

| Key, while it plays | Does |
| --- | --- |
| Space, Enter or Fn + Q | Pause / resume |
| Left / Right | Back / forward 10 seconds |
| Fn + W / Fn + E | Back 10 / forward 30 seconds |
| Down / Up | Back / forward a minute |
| R | From the start |
| + / - | Volume |
| M | Play it on the monitor instead (Dock mode must be running) |
| Esc or S | Stop |

Any key shows the title, time and progress for three seconds; they stay up
while paused or loading.

- **Continue Watching** on the home screen picks up where you stopped, on
  whichever device that was.
- The Cardputer tells the server what it is playing: it shows in Jellyfin's
  dashboard, and the place you stopped is saved for every device.
- An episode that plays to the end goes on to the next one in the season.
- **Watched** and **Favourite** are the server's: they show in Jellyfin's
  other apps too, and **Favourites** on the home screen lists what you marked.
- A Bluetooth headset's or car's play and skip buttons pause and skip the
  video too (ZeroOS newer than 0.4.0), instead of starting your music over it.
- The screen stays on while a video plays (not while paused). Turning the
  Cardputer face down, or the screen turning off, pauses it.
- Video belongs to the Jellyfin app, unlike music: leaving the app stops it.
- **M** hands the video to the monitor's player in 720p, from the same place.

### What the server sends

The server converts each video to fit the Cardputer: H.264 and AAC, at most
427 x 240 and about 700 kbit/s (720p and 4 Mbit/s for the monitor), in short
HLS segments, so seeking works. A file that already fits is passed through.
The server needs transcoding enabled for the user (on by default), and a
server that has to convert a 4K film may need a moment before it starts.

On the Cardputer, mpv decodes the stream (libmpv, which comes with mpv) and
draws each picture into ZeroShell's own screen. The sign-in token travels in
the Authorization header of every request (lists, covers, music, video),
never in an address: Jellyfin 12 refuses tokens in addresses unless "legacy
authorization" is turned on. For the monitor, the header goes to the Dock's
player in a file only you can read, `~/.config/zeroos/jellyfin-mpv.conf`.

Covers come as JPEG, scaled by the server to the card they are for.

## Away from home

With Tailscale on both the Cardputer and the server (see [Remote](../remote/README.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Which servers

Jellyfin 10.9 and newer, up to 12.4, use the paths in Jellyfin's published
API description (12.4). A few lists moved in 10.9; for a server older than
that the app tries the old place when the new one isn't there (the
libraries, Continue Watching, what is new in a library).

## Limits

- No subtitle or audio track choice (the server's defaults are used).
- No photos, e-books or Live TV; people and genres aren't in the search.
- Video needs Wi-Fi that keeps up with about 1 Mbit/s.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Signing in still waits for the server with the screen still (up to about
  12 seconds if it doesn't answer). Everything after that is fetched in the
  background.
- Favourite artists aren't in **Favourites** (albums, tracks, films, shows,
  episodes, playlists and collections are).
- **Not yet tried against a real Jellyfin server or on the device.** The
  requests follow Jellyfin's published API description (12.4), and are tested
  end to end against a stand-in server that answers in those shapes (see
  below). Video playback is tested with the real player (libmpv) against a
  stand-in that serves an HLS stream and checks every request's sign-in
  header. If something is wrong with your server, please
  [open an issue](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/issues)
  with the server's version.

## How it is built, and tested

| File | Is |
| --- | --- |
| `jellyfin.cpp` | The screens: home, a library's grid, an album, a film's page, search, the player, video |
| `ui.cpp`, `ui.h` | Cards with their covers, rows of cards, the grid; the covers kept in memory and on the card |
| `api.cpp`, `api.h` | What is asked of the server and how its replies are read; the requests themselves (curl) |
| `art.cpp`, `art.h` | A JPEG or PNG made into a card's pixels ([stb_image](https://github.com/nothings/stb)); the cover files |
| `work.h` | The three background threads that requests and covers run on |

ZeroOS still signs in, builds the addresses music and video stream from, and
reports playback (`hal/jellyfin.h` in the OS); the lists, the search and the
pictures are the app's own, so they are the same on every ZeroOS the app
installs on.

Tests, all run in CI:

- `jellyfin_api_test`: the addresses asked for and the replies read, with
  replies that are cut short, not JSON, or carry ids and tags that could
  break an address.
- `jellyfin_art_test`: decoding, cropping to a card, the cover files.
- `tests/server-test.sh`: the app in the headless ZeroShell, with the
  device's own hardware layer, against `tests/fake_jellyfin.py`, a stand-in
  server. It signs in over real HTTP and checks what the app asked for and
  what it drew: the token in every request's header and in no address, the
  covers on the screen and kept for the next visit, a library of 230 in three
  pages, one search request for each kind of thing, a favourite reaching the
  server, signing out, a refused token, and a server from before 10.9.

To try the app on a PC without a server, run the stand-in and sign in to it
from the simulator (any user, password `secret`):

```sh
python3 apps/jellyfin/tests/fake_jellyfin.py --port 8096 &
build/sim/zeroshell --real-hal        # server address: http://127.0.0.1:8096
```

Without `--real-hal`, the simulator (and the browser one) has no network: it
signs in to a pretend server and draws its covers from each item's name.
