# Media

The **Media** app is a music player in the style of the classic desktop MP3
players: a green LCD with the time and a spectrum analyser, the title
scrolling past, transport buttons, and the playlist underneath. It plays music
from `~/Music` (the **Library** tab) and music and video (movies, TV shows,
home videos...) from every library on a Jellyfin server (the **Jellyfin** tab).

Voice notes used to be a tab here. They are in their own app now,
[Recorder](../recorder/README.md); your notes in `~/Recordings` are where they were.

## The player

```
 LIBRARY  JELLYFIN  ===========================  ~/Music
 +-----------------+ +-----------------------------------+
 | >         1:23  | | 3. Bench Tapes - Low Battery Blu… |
 | ||| |||||  || | | [OGG] [3/6] [SHUF]  VOL ====        |
 +-----------------+ [|<][>][||][#][>|]  ----o--------   |
 +--------------------------------------------------------+
 | 1. Synthwave Demos - Night Drive                  3:34 |
 | 2. Synthwave Demos - Solder Fumes                 3:07 |
 | 3. Bench Tapes - Low Battery Blues                4:03 |
 +--------------------------------------------------------+
```

The LCD shows play, pause or stop, and the time (it blinks while paused). The
strip on the right scrolls the track; under it are the format (MP3, FLAC,
OGG..., NET for a Jellyfin stream), where you are in the queue, whether
shuffle is on, and the volume. The playlist marks the track that's playing in
white and the one you're on in blue.

| Key | Does |
| --- | --- |
| Up / Down | Move in the playlist |
| Enter | Play the track (and the rest of the list after it), or open a folder |
| Left / Right | Library / Jellyfin tab |
| / | Search: the Library tab narrows its list to an artist or title; Jellyfin searches the server |
| Esc | Clear the search, or back one level in Jellyfin |
| Z X C V B | Previous, play, pause, stop, next: the classic player's keys |
| Space | Pause / resume |
| N / P | Next / previous track |
| S | Shuffle on or off (from the next track you pick) |
| + / - | Volume |
| **Fn + Q** | Play / pause. With nothing playing, plays the track you're on |
| **Fn + W / Fn + E** | Previous / next track |
| Fn + S / Fn + D, Fn + A | Volume down / up, sound off (anywhere) |
| Tab | The menu: search, shuffle, stop, and for Jellyfin refresh libraries, sign in or out |
| 0 | Every key at once |

The media keys (Fn + Q, W, E) work in every app, not only this one, and so do a
Bluetooth headset's or car's buttons. The queue belongs to ZeroShell, not the
Media app, so music carries on to the next track after you leave the app and
while the screen is off.

## Jellyfin: sign in

1. Open **Media** and press **Right** for the **Jellyfin** tab.
2. Press **Enter** on **Sign in...** and answer three questions:
   - **Server**: its name or address. `nas` or `192.168.1.20` means
     `http://nas:8096` (Jellyfin's usual port); type the whole address, such as
     `https://music.example.com`, for anything else.
   - **User name** and **password**: the ones you use in Jellyfin's own apps.

The password is used once and not kept. The server gives back a sign-in token,
which is saved in `~/.config/zeroos/settings.conf` (readable only by you).
**Sign out** forgets it; you can also remove "Cardputer Zero" under Devices in
Jellyfin's dashboard.

The Jellyfin tab's first list: **Search**, **Continue watching**, then every
library on your server, by the names you gave them (two music libraries, Kids
movies, Home videos, Music videos, Audiobooks, Collections...), then **Latest
albums**, **Artists** and **Playlists**. **Sign out** and **Refresh libraries**
are in the menu (**Tab**).

| A library of | Opens as |
| --- | --- |
| Music | its albums |
| Movies | its movies |
| Shows | its shows, then seasons, then episodes |
| Anything else (home videos, music videos, books, mixed, collections) | its folders and files, folders first |

Live TV libraries aren't listed: there's no tuner support. On ZeroOS 0.4.0 and older
the library rows aren't there; you get the fixed **Movies**,
**TV shows** and **Albums** rows instead, as before.

## Playing music

The keys are the player's (see [The player](#the-player)): **Enter** opens an
album, artist, playlist or folder and plays a track, **Esc** goes back one
level, and **/** searches the whole server: tracks, albums, artists, playlists,
movies, shows, episodes, music videos and audiobooks.

Playing a track queues the rest of the album or playlist after it. The queue
belongs to ZeroShell, not the Media app, so music carries on to the next track
after you leave the app and while the screen is off. Wi-Fi stays on while
something is playing.

Tracks in mp3, flac, ogg, opus, m4a or wav play as they are; the server
converts anything else to mp3. Long lists come 200 at a time: **More...** at
the bottom fetches the next 200.

## Watching video

Pick a movie, or a show, then a season, then an episode. It plays across the
whole app area (16:9 fills 267 x 150 pixels), with the sound on the speaker,
the headphone jack or Bluetooth, whichever music would use.

| Key | Does |
| --- | --- |
| Space, Enter or Fn + Q | Pause / resume |
| Left / Right | Back / forward 10 seconds |
| Fn + W / Fn + E | Back 10 / forward 30 seconds |
| Down / Up | Back / forward a minute |
| 0 | From the start |
| + / - | Volume |
| M | Play it on the monitor instead (Dock mode must be running) |
| Esc or S | Stop |

Any key shows the title, time and progress for three seconds; they stay up
while paused or loading.

- **Continue watching** lists what you started on any device (Jellyfin's own
  "Continue watching"), and picks up where you stopped. **0** goes back to the
  start. Movies and episodes you left part-way show the time, e.g. `@36:50`.
- The Cardputer tells the server what it is playing: it shows in Jellyfin's
  dashboard, and the place you stopped is saved for every device.
- An episode that plays to the end goes on to the next one in the season.
- A Bluetooth headset's or car's play and skip buttons pause and skip the
  video too (ZeroOS newer than 0.4.0), instead of starting your music over it.
- The screen stays on while a video plays (not while paused). Turning the
  Cardputer face down, or the screen turning off, pauses it.
- Video belongs to the Media app, unlike music: leaving the app stops it.
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

With Tailscale on both the Cardputer and the server (see [REMOTE.md](../remote/README.md)),
sign in with the server's Tailscale name, e.g. `homelab`, and it works from any
network. Set **Settings > Tailscale** to **Always on** so it stays connected.

## Limits

- No album art or posters, and no subtitle or audio track choice (the
  server's defaults are used).
- No photos, e-books or Live TV.
- The spectrum analyser is for the look: the player doesn't hand the sound to
  the app, so it dances to the beat of a random number generator.
- Video needs Wi-Fi that keeps up with about 1 Mbit/s.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
- Asking the server for a list freezes the screen for a moment, up to 12
  seconds if the server doesn't answer.
- Not yet tried against a real server or on the device. The requests follow
  Jellyfin's published API description (12.4). Video playback is tested
  with the real player (libmpv) against a stand-in server that serves an HLS
  stream and checks every request's sign-in header.
