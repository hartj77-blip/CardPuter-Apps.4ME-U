# Media

The **Media** app plays your own music and video files. On top it's a classic
desktop MP3 player: a green LCD with the time and a spectrum analyser, the
title scrolling past, transport buttons. Underneath, where the playlist used
to be, is a file explorer: the music and video folders on the SD card, USB
drives, and shares on a server.

Jellyfin used to be a tab here. It has its own app now,
[Jellyfin](../jellyfin/README.md), in Jellyfin's own look. Voice notes are in
[Recorder](../recorder/README.md).

## The player

```
 ZEROOS MEDIA  ====================  Music/Aphex Twin/SAW 85-92
 +-----------------+ +-----------------------------------+
 | >         1:23  | | 2. Aphex Twin - Tha (9:01) ***    |
 | ||| |||||  || | | [MP3] [2/2] [SHUF]  VOL ====        |
 +-----------------+ [|<][>][||][#][>|]  ----o--------   |
 +--------------------------------------------------------+
 | 1. 01 Xtal                                         MP3 |
 | 2. 02 Tha                                          MP3 |
 +--------------------------------------------------------+
```

The LCD shows play, pause or stop, and the time (it blinks while paused). The
strip on the right scrolls the track; under it are the format (MP3, FLAC,
OGG..., NET for a stream), where you are in the queue, whether shuffle is on,
and the volume. The title bar says where you are. The explorer marks the
track that's playing in white and the row you're on in blue; folders are gold.

## Places

The first screen lists where media can be:

| Place | What it is |
| --- | --- |
| **Music** | `~/Music` on the SD card |
| **Videos** | `~/Videos` on the SD card (made the first time you open it) |
| **Downloads** | `~/Downloads`, if there is one |
| **SD card** | your whole home folder |
| A USB drive | anything mounted under `/media`, `/run/media` or `/mnt` (DRIVE) |
| A server share | any SMB, NFS, SSHFS, WebDAV or rclone mount, wherever it is (SMB nas, NFS nas...) |
| **Open an address...** | an `http://` or `https://` address: a stream, internet radio, or a file on a web server |

Only folders and files Media can play are listed: music (mp3, flac, ogg,
wav, m4a, opus, aac, wma), video (mp4, mkv, webm, mov, avi, m4v, mpg, ts)
and playlists (m3u, m3u8, pls). Media opens where you left it last time.

### A share on your server

Mount the share once and Media lists it. For SMB (a Windows share, a NAS,
Samba), from a terminal:

```sh
sudo apt install cifs-utils
sudo mkdir -p /mnt/nas
echo '//nas/music /mnt/nas cifs credentials=/home/zero/.smb,uid=zero,nofail,x-systemd.automount 0 0' | sudo tee -a /etc/fstab
printf 'username=me\npassword=secret\n' > ~/.smb && chmod 600 ~/.smb
sudo mount /mnt/nas
```

NFS works the same way (`sudo apt install nfs-common`, then a `nas:/music
/mnt/nas nfs nofail,x-systemd.automount 0 0` line). With Tailscale on both
ends (see [Remote](../remote/README.md)), a share at home works from anywhere.

## Keys

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Open a folder; play a track (and the rest of the folder after it); play a video or a playlist |
| Right | Open the folder you're on |
| Left, Del or Esc | Up a folder (Esc first clears a search) |
| X | Play everything in the folder you're on, folders under it included (up to 3000 tracks) |
| / | Search the folder you're in by name |
| Z X C V B | Previous, play, pause, stop, next: the classic player's keys |
| Space | Pause / resume |
| N / P | Next / previous track |
| S | Shuffle on or off (from the next track you pick) |
| + / - | Volume |
| **Fn + Q** | Play / pause. With nothing playing, plays the row you're on |
| **Fn + W / Fn + E** | Previous / next track |
| Fn + S / Fn + D, Fn + A | Volume down / up, sound off (anywhere) |
| Tab | The menu: search, shuffle, stop, open an address, places, rescan |

The media keys (Fn + Q, W, E) work in every app, not only this one, and so do a
Bluetooth headset's or car's buttons. The queue belongs to ZeroShell, not the
Media app, so music carries on to the next track after you leave the app and
while the screen is off.

The marquee shows the artist as the folder two up from the track
(`Artist/Album/01 Track.mp3`), or the folder it's in.

## Video

A video file plays across the whole app area, with the sound on the speaker,
the headphone jack or Bluetooth.

| Key | Does |
| --- | --- |
| Space, Enter or Fn + Q | Pause / resume |
| Left / Right | Back / forward 10 seconds |
| Fn + W / Fn + E | Back 10 / forward 30 seconds |
| Down / Up | Back / forward a minute |
| 0 | From the start |
| + / - | Volume |
| M | Play it on the monitor instead (Dock mode must be running) |
| Esc, S or V | Stop |

The Cardputer decodes video on its CPU and scales it to the screen, so files
about the size of the screen play best (480p or smaller, H.264). A 1080p film
may stutter: press **M** to send it to the monitor in Dock mode, which decodes
in hardware. Video belongs to the app: leaving Media stops it. Turning the
Cardputer face down, or the screen turning off, pauses it.

## Limits

- The spectrum analyser is for the look: the player doesn't hand the sound to
  the app, so it dances to the beat of a random number generator.
- No tags: titles come from file names, artists from folder names. Track
  lengths show once a track plays.
- Media doesn't mount shares itself; mount them as above.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
