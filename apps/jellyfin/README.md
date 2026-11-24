# Jellyfin

The **Jellyfin** app plays music and video (movies, shows, home videos...)
from every library on your [Jellyfin](https://jellyfin.org) server, and looks
like Jellyfin's own apps: the near-black dark theme, the purple-to-blue logo,
a home screen of card rows with Jellyfin blue for the one selected, and a
now-playing bar along the bottom. Lists are ZeroOS lists, so you can type to
narrow them, as everywhere else.

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
can see.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** (in the menu, **Tab**) forgets it; you can also remove "Cardputer
Zero" under Devices in Jellyfin's dashboard.

## Home

```
 [=] jellyfin                                    Q (J)
 My Media
 +--------+ +--------+ +--------+ +--------+
 |   ##   | |   ##   | |   []   | |   []   |
 | Music  | | Vinyl  | | Movies | | Kids   |
 +--------+ +--------+ +--------+ +--------+
 Continue Watching
 +-------------------+ +-------------------+
 | Bench Talk        | | Sintel            |
 | S2E2 Reading      | | 2010  38m left    |
 |=======------------| |==============-----|
 +-------------------+ +-------------------+
 Music
   Latest   Albums   Artists   Playlists
 > Low Battery Blues - Bench Tapes           0:42 / 4:03
```

- **My Media**: every library on your server, by the names you gave them
  (two music libraries, Kids movies, Home videos, Music videos, Audiobooks,
  Collections...). On ZeroOS 0.4.0 and older you get fixed **Movies**,
  **Shows** and **Music** cards instead.
- **Continue Watching**: what you started on any device. Enter carries on.
- **Music**: Latest, Albums, Artists and Playlists across all your music.

| A library of | Opens as |
| --- | --- |
| Music | its albums |
| Movies | its movies |
| Shows | its shows, then seasons, then episodes |
| Anything else (home videos, music videos, books, mixed, collections) | its folders and files, folders first |

Live TV libraries aren't listed: there's no tuner support.

## Keys

The soft-key bar along the bottom shows the keys for the screen you're on;
**Tab** opens the menu with everything (Ctrl + a letter runs one straight
away), **0** the key sheet.

On the home screen, letters are shortcuts:

| Key | Does |
| --- | --- |
| Arrows | Move between cards and rows |
| Enter | Open, or play |
| F | Search the whole server: tracks, albums, artists, playlists, movies, shows, episodes, music videos, audiobooks |
| Space | Pause / resume the music |
| N / P | Next / previous track |
| + / - | Volume |

Lists (albums, tracks, episodes, search results) are ZeroOS lists, so letters
narrow them instead:

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Open, or play |
| 1 to 9 | Open or play the row with that number |
| Letters | Narrow the list to the rows that match (Backspace takes a letter off) |
| Space | Pause / resume the music (while nothing is typed) |
| Backspace or Esc | Back |
| Tab | The menu: Pause (U), Search (F), Next (N), Previous (P), Home (H), Stop music (T), Refresh (R), Sign out (O) |

Everywhere: Fn + Q pauses or resumes, Fn + E / Fn + W skip to the next or
previous track.

## Playing music

Playing a track queues the rest of the album or playlist after it. The playing
track is marked with a speaker, and the bar along the bottom shows it
with its time and a blue progress line. The queue belongs to ZeroShell, not
the app, so music carries on to the next track after you leave the app and
while the screen is off. Wi-Fi stays on while something is playing. The
media keys (Fn + Q, W, E) and a Bluetooth headset's or car's buttons work
everywhere.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3. Long lists come 200 at a time: **More...** at
the bottom fetches the next 200.

## Watching video

Pick a movie, or a show, then a season, then an episode, or a card under **Continue Watching**. It plays across the
app area above the soft-key bar (16:9 fills 242 x 136 pixels), with the sound on the speaker,
the headphone jack or Bluetooth, whichever music would use.

| Key | Does |
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

- **Continue Watching** on the home screen lists what you started on any
  device, with a blue bar for how far you got, and picks up where you
  stopped. **R** goes back to the start. In lists, movies and episodes you
  left part-way say how much is left, e.g. `38m left`.
- The Cardputer tells the server what it is playing: it shows in Jellyfin's
  dashboard, and the place you stopped is saved for every device.
- An episode that plays to the end goes on to the next one in the season.
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
the Authorization header of every request, never in an address: Jellyfin 12
refuses tokens in addresses unless "legacy authorization" is turned on. For
the monitor, the header goes to the Dock's player in a file only you can read,
`~/.config/zeroos/jellyfin-mpv.conf`.

## Away from home

With Tailscale on both the Cardputer and the server (see [Remote](../remote/README.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Limits

- No album art or posters (cards show an icon instead), and no subtitle or audio track choice (the
  server's defaults are used).
- No photos, e-books or Live TV.
- Video needs Wi-Fi that keeps up with about 1 Mbit/s.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Asking the server for a list freezes the screen for a moment, up to 12
  seconds if the server doesn't answer.
- Not yet tried against a real server or on the device. The requests follow
  Jellyfin's published API description (12.4). Video playback is tested
  with the real player (libmpv) against a stand-in server that serves an HLS
  stream and checks every request's sign-in header.
