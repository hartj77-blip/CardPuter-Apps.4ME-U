# Media

The **Media** app plays your own music. On top it's a classic desktop MP3
player: an LCD with the time and a spectrum analyser, the title scrolling
past, transport buttons. Underneath, where the playlist used to be, is a file
explorer: the music on the SD card, USB drives, and shares on a server. It
comes in three skins: the classic look, a 1970s hi-fi receiver and a
liquid-chrome player from 2001. It also wears classic Winamp skins (`.wsz`)
that you copy to `~/Skins`.

Media is for music only. Up to 3.0 it also played video files; that's gone,
so video files aren't listed any more.

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
track that's playing and the row you're on, and numbers the first nine rows;
folders have their own colour. On the Player tab, the explorer's place shows
the track in big letters, where it is in the queue and what comes next.

## Skins

Press **T** on the Player tab, or pick **Skin** in the Tab menu (Ctrl + T
anywhere in Media), to go to the next skin. Media remembers the one you
leave it on.

| Skin | Look |
| --- | --- |
| **Classic** | The player above: gunmetal case, black LCD with green digits, the explorer underneath |
| **Hi-Fi Receiver** | Walnut and brushed aluminium. The amber tuning dial is the seek bar: its red needle sweeps across the track's time, and a second scale shows your place in the queue. Two VU meters, the track name in a big amber window, latching piano keys, Shuffle and Mute switches, a volume knob |
| **Liquid Chrome** | A molten-metal body whose reflections ripple, an oval screen with big digits, the title in large scrolling letters and an analyser, jelly buttons, and a glass tube that fills as the track plays |

The two skins are pixel art at the screen's own size, and each has two
screens, which are Media's two tabs: the player is the Player tab, and the
playlist (the explorer) the Library. Left / Right on the playlist go to
the player; on the player, Esc, **L**, Up or Down go to the playlist. On a
skin's playlist the preset keys **1** to **6** open that row.

Up to 3.1 Media had eight colour themes instead; they're gone.

## Winamp skins

Media wears classic Winamp skins, the `.wsz` files of Winamp 2 (thousands
are free to download, from the Winamp Skin Museum for one). Copy them to
`~/Skins` on the SD card. After the three built-in skins, **T** steps
through them by name. A skin unpacked into a folder in `~/Skins` works too.

Winamp's main window is 275 x 116 pixels, so it fits on the screen at its own
size, every pixel as the skin's artist drew it:

- The **Player** tab is the main window: play, pause or stop and the time in
  the skin's digits, the title scrolling in its letters, the analyser in its
  colours (`viscolor.txt`), the seek bar's thumb where the track is, the
  volume slider, Shuffle lit when it's on, and the transport button you just
  pressed held down for a moment.
- The **Library** is the playlist window, stretched across the screen. The
  explorer's rows are in the system font, since Winamp's 5 x 6 letters are too
  small for file names, in the skin's own colours (`pledit.txt`). Where you
  are scrolls along the box at the bottom, and the time goes next to it.

What only a mouse could do is drawn but does nothing: the balance, the EQ
and playlist buttons, repeat, the window buttons. Media plays from the
keyboard as in any other skin.

Only classic skins work. Winamp 3 and 5's modern skins (`.wal`, and `.wsz`
files without a `main.bmp`) are layouts with scripts, not pictures, and are
passed by. So is any skin that can't be read: **T** goes to the next one
and says why. When a part is missing from a skin, the main window's own
picture shows in its place. Winamp would draw its own skin's part there, and
that skin isn't ours to ship.

## Places

The first screen lists where media can be:

| Place | What it is |
| --- | --- |
| **Music** | `~/Music` on the SD card |
| **Downloads** | `~/Downloads`, if there is one |
| **SD card** | your whole home folder |
| A USB drive | anything mounted under `/media`, `/run/media` or `/mnt` (DRIVE) |
| A server share | any SMB, NFS, SSHFS, WebDAV or rclone mount, wherever it is (SMB nas, NFS nas...) |
| **Open an address...** | an `http://` or `https://` address: a stream, internet radio, or a file on a web server |

Only folders and files Media can play are listed: music (mp3, flac, ogg,
wav, m4a, opus, aac, wma) and playlists (m3u, m3u8, pls). Media opens where you left it last time.

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

Media has two tabs: the **Library** (the explorer) and the **Player** (what's
playing, where the explorer was). **Left / Right** go from the Library to the
Player, **Esc** comes back. The soft-key bar along the bottom shows the keys
for the tab you're on; **Tab** opens the menu with everything, **0** the key
sheet.

The Library is a list, so typing searches it:

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Open a folder; play a track (and the rest of the folder after it) or a playlist |
| 1 to 9 | Open or play the row with that number |
| Letters | Search the folder you're in by name (Backspace takes a letter off, Esc clears it) |
| Space | Pause / resume (with nothing playing, plays the row you're on). Once you've typed, a space in the search |
| Backspace or Esc | Up a folder |
| Left / Right | The Player tab |
| Tab | The menu: Play all (A), Pause (U), Next track (N), Previous track (P), Stop (S), Shuffle (H), Skin (T), Open an address (O), Go to places (G), Rescan (R). Ctrl + the letter runs one straight away |

**Play all** plays everything in the folder you're on, folders under it
included (up to 3000 tracks).

Tracks are changed with the keyboard's media keys (below), not letters. The Player:

| Key | Does |
| --- | --- |
| Enter or Space | Pause / resume |
| Fn + W / Fn + E, or Left / Right | Previous / next track |
| S | Stop |
| H | Shuffle on or off (from the next track you pick) |
| T | The next skin |
| + / - | Volume |
| Esc | Back to the Library |

Anywhere in Media, and in every other app:

| Key | Does |
| --- | --- |
| **Fn + Q** | Play / pause. With nothing playing, plays the Library row you're on |
| **Fn + W / Fn + E** | Previous / next track |
| Fn + S / Fn + D, Fn + A | Volume down / up, sound off |

A Bluetooth headset's or car's buttons work as the media keys do. The queue belongs to ZeroShell, not the
Media app, so music carries on to the next track after you leave the app and
while the screen is off.

The marquee shows the artist as the folder two up from the track
(`Artist/Album/01 Track.mp3`), or the folder it's in.

## Limits

- The spectrum analysers and the VU meters are for the look: the
  player doesn't hand the sound to the app, so they dance to a made-up beat.
- No seeking within a track: ZeroOS's music player can't do it yet, so Left /
  Right on the Player skip tracks.
- No tags: titles come from file names, artists from folder names. Track
  lengths show once a track plays.
- Media doesn't mount shares itself; mount them as above.
- Names in scripts the built-in font lacks (Chinese, Japanese, Cyrillic...)
  show as boxes.
