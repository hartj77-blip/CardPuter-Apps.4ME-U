# Car

**Car** gets your music into a car stereo, with whatever the car has:

| Your car has | Use | What plays | Song names | Skip with |
| --- | --- | --- | --- | --- |
| An aux input (3.5 mm) | [Music from your phone](#music-from-your-phone) | Anything your phone plays: Spotify, Apple Music, the Jellyfin app, podcasts | On the Cardputer | Enter and the menu in Car, Fn + Q/W/E, or the phone |
| A USB socket | [USB stick mode](#usb-stick-mode) | Music files in `~/Music` on the Cardputer | On the car's screen | The car's own buttons |
| Bluetooth music | [Bluetooth](#bluetooth-to-a-car-speaker-or-headphones) | Anything Media or Jellyfin plays on the Cardputer, or your phone's music passed on | No | The car's buttons |

None of this is CarPlay or Android Auto: those only accept a phone certified
by Apple or Google, and the Cardputer can't stand in for one.

## Contents

1. [Quick start](#quick-start)
2. [The screen](#the-screen)
3. [Keys](#keys)
4. [Music from your phone](#music-from-your-phone)
5. [USB stick mode](#usb-stick-mode)
6. [Bluetooth to a car, speaker or headphones](#bluetooth-to-a-car-speaker-or-headphones)
7. [Power in the car](#power-in-the-car)
8. [Troubleshooting](#troubleshooting)
9. [How it works](#how-it-works)
10. [What has been checked on a device](#what-has-been-checked-on-a-device)
11. [Limits](#limits)

## Quick start

**Phone to aux** (the usual one):

1. Once: in **Car**, press Enter on **Add a phone**, then on the phone open
   Bluetooth, pick **cardputer** and accept.
2. In the car: a 3.5 mm cable from the Cardputer's headphone jack to the
   car's aux. Pick AUX on the stereo.
3. Play music on the phone. The song shows at the top of **Car**; Enter
   pauses, Fn + E skips.

**USB stick:**

1. Put your music in `~/Music` (Files, Sync, or over SSH).
2. Switch on the Cardputer's left side to **Slave**. In **Car**, Enter on
   **USB stick mode** and wait for **On**.
3. Cable from the right-hand USB-C port to the car's USB socket; pick USB
   on the stereo.

## The screen

With your phone connected and playing:

```
 Car                                               06:15   * wifi 78%
+------------------------------------------------------------------+
| >  Cafe del Mar (Energy 52 Remix)                     1:01 / 3:35 |
|    Energy 52  .  John's iPhone                                    |
|====================------------------------------------------------|
 +------------------------------+  +-------------------------+
 | 1 [] John's iPhone        >  |  | John's iPhone           |
 | 2 <> USB stick mode     Off  |  | Enter  play, pause      |
 | 3 *  CAR MULTIMEDIA          |  | Ctrl+N  next            |
 | 4 +  Add a phone             |  | Ctrl+B  back            |
 +------------------------------+  +-------------------------+
 Enter Pause                                             Tab Menu
```

- **The now playing card** runs across the top while a phone is connected:
  - left: a green play mark while the music plays, a grey pause mark when it
    doesn't;
  - the song (cut short with "..." when it doesn't fit), and on the right
    how far in it is and how long it is;
  - under it the artist, the phone's name, and an arrow with the Bluetooth
    car or speaker if the music goes there instead of the headphone jack;
  - a thin green line along the bottom for how far in the song is. The
    clock and the line keep running between the phone's updates (every 2
    seconds). Phones that don't say how long a song is get no clock or
    line.
  - Connected but nothing playing: the phone's name and "Connected. Play
    music on the phone."
- **The list:**
  - the connected phone comes first, with a play or pause mark on the right
    (short, so its name has the room);
  - **USB stick mode**, with Off, a percentage while it copies, On, or In
    use;
  - paired cars, speakers, headphones and phones (phones with a phone icon;
    connected ones say On in green), and devices the last scan found (new);
  - **Add a phone**, **Scan for devices** and **Let the car find me**.
- **The panel** on the right says what the row in focus is and what Enter
  does to it. The bar at the bottom names Enter's action too.

Without a phone the card is hidden and the list starts at the top.

## Keys

| Key | What it does |
| --- | --- |
| Up, Down | Move through the list |
| Enter | On the phone: play or pause. On USB stick mode: on or off. On a device: connect, disconnect, or pair a new one. On the last three rows: what they say |
| Tab | The menu for the row in focus |
| Tab > N, or Ctrl + N | Next song on the phone |
| Tab > B, or Ctrl + B | Previous song (or the start of this one; the phone decides) |
| Tab > X | Disconnect the phone |
| Tab > D, or Del | Forget a paired device (asks first) |
| Typing | Narrows the list to rows with those letters |
| Fn + Q, W, E | Play/pause, previous, next, from anywhere, with Car closed or the screen off (ZeroOS 0.8.0 or newer). They go to the phone while the Cardputer plays nothing of its own |
| Volume keys | The Cardputer's volume. The phone's volume and the car's knob work too |
| Esc | Back home. The music carries on |

## Music from your phone

For a car with an aux input but no Bluetooth music: the phone sends its
music to the Cardputer over Bluetooth, and the Cardputer plays it out of its
headphone jack into the aux, like a Bluetooth aux adapter, with the song on
its screen.

### Pair the phone once

1. In **Car**, press Enter on **Add a phone**. The Cardputer can be found as
   `cardputer` for 2 minutes.
2. On the phone, open the Bluetooth settings and pick **cardputer**. If both
   show a number, check it's the same and accept on the phone.
3. The phone appears in Car's list with a phone icon, and comes back by
   itself next time (the Cardputer trusts it).

If the phone doesn't list `cardputer` (some iPhones only list some kinds of
device): open the phone's Bluetooth settings so the phone can be found, and
use **Scan for devices** in Car instead, then Enter on the phone.

### In the car

1. Plug a 3.5 mm cable from the Cardputer's headphone jack into the car's
   aux, and pick AUX on the stereo.
2. Turn the Cardputer's Bluetooth on if it's off (Ctrl + Space). The phone
   usually connects by itself; if not, Enter on its row in Car connects it.
3. Play music on the phone, in any app. It comes out of the car's speakers.
4. Set the volume: the phone near full, then the car's knob. If it sounds
   quiet or distorted, try the Cardputer's volume keys.

### Calls

Calls stay on the phone. The Cardputer takes the phone's music but doesn't
offer to be its hands-free kit, so the phone rings and you talk on it as
usual (or on the car's own hands-free system, if it has one). The phone
pauses the music for the call and carries on after.

### Wi-Fi and Bluetooth

The Cardputer has one radio chip for both, and they take turns. When Wi-Fi
is on but not connected to anything, as it usually is in a car, it searches
for known networks every few seconds, and each search makes the music skip.
So while the phone's music plays and Wi-Fi isn't connected, Car turns Wi-Fi
off, and turns it on again about half a minute after the music stops or the
phone goes. This works with Car closed and the screen off.

- Connected Wi-Fi (your phone's hotspot, say) stays on. Light use, like SSH,
  messages or syncing, is fine alongside the music; a big download may still
  make it skip.
- Wi-Fi that is connecting at that moment is left alone.
- Turn Wi-Fi on yourself while the music plays and it stays on until the
  music stops.
- If the Cardputer restarts while Wi-Fi is off because of this, Wi-Fi comes
  back on.
- To switch this off: `sudo systemctl disable --now zeroos-car-wifi`.

### A buffer for the music

While the phone's music plays, the Cardputer keeps about four times as much
of it in hand as it does for other sound (about 43 ms instead of 10), so a
moment's stall of the radio or the CPU doesn't make it skip. It means the
music starts a fraction of a second after you press play on the phone, which
you won't notice. Other sound (key clicks, games) isn't affected.

### Also

- If a Bluetooth car, speaker or headphones is connected as well, the
  phone's music goes there instead of the headphone jack (the card shows
  where), and that device's play and skip buttons reach the phone too
  (ZeroOS 0.8.0 or newer).
- The phone's music and the Cardputer's own (Media, Jellyfin, a video)
  never play at once (ZeroOS 0.8.4 or newer): whichever you start last
  plays, and the other pauses. Start music on the Cardputer and the phone
  pauses; press play on the phone and the Cardputer's music pauses, with
  "Paused: your phone is playing".
- While the Cardputer plays something of its own, Fn + Q/W/E and a car's
  buttons are for that, not the phone.
- A connected phone counts as connected for **Settings > Bluetooth
  auto-off**, so Bluetooth stays on while it is.
- With ZeroOS 0.7.2 or older, Fn + Q, W, E start the Cardputer's own music
  over the phone's; use Enter and the menu in Car, or the phone.

## USB stick mode

The Cardputer shows up in the car as a USB stick holding a copy of
`~/Music`. The car is the player, so its screen shows the song names and all
its buttons work.

1. Set the **Host / Slave switch** on the left side of the Cardputer to
   **Slave**. In Slave the USB-A port, the other USB-C port and Ethernet are
   off; that is how the board is wired.
2. Open **Car** and press Enter on **USB stick mode**. The first time (and
   whenever `~/Music` has changed) it copies the music into a disk image:
   **Copying 43%**. Then it says **On**.
3. Plug the **right-hand USB-C port** into the car's USB socket. When the car
   has taken it up, the row says **In use**. Pick USB on the stereo and use
   the car's buttons.

Enter on the row again turns it off. Set the switch back to **Host** to use
USB devices and Ethernet again.

What the car sees is a read-only FAT32 stick called `MUSIC` with the same
folders as `~/Music`, files in name order. It is a copy
(`/var/lib/zeroos/car/music.img`), so it needs as much free space again as
the music takes, and the car can't change your files. Only music files are
copied: mp3, m4a, aac, wma, wav, flac, ogg, opus. An older stereo may only
play mp3 (and perhaps wma or aac) and skip the rest.

## Bluetooth to a car, speaker or headphones

For a car with Bluetooth music, or a speaker or headphones: the Cardputer is
the player and sends the sound to it.

- **Scan for devices**: put the car, speaker or headphones in pairing mode,
  then scan (8 seconds). Press Enter on it in the list to pair and connect.
- **Let the car find me**: for cars that search for a phone from their own
  screen. The Cardputer is visible as `cardputer` for 2 minutes; pick it on
  the car's screen. Only the first device that pairs is kept (and trusted to
  reconnect by itself); the Cardputer then hides again at once.
- Enter on a paired device connects or disconnects it. **Tab > D** (or
  **Del**) forgets it, after asking.

Pairing is only possible while you pair: when you pick a device to pair, the
Cardputer accepts pairing for that device alone until it has connected, and
unpairs anything else that pairs in that time. It is never visible to others
except during **Let the car find me** and **Add a phone**.

Once connected, sound from Media and Jellyfin goes to that device, and its
play, pause, next and previous buttons drive the play queue, also with the
screen off. Pressing play with nothing queued starts `~/Music` from the top.

A paired car reconnects by itself when it starts, as long as the Cardputer's
Bluetooth is on. **Settings > Bluetooth auto-off** turns Bluetooth off after
a while with nothing connected, so set it to **Never** if the Cardputer lives
in the car.

## Power in the car

A car's USB socket often gives only 0.5 A (2.5 W), about what the Cardputer
uses when idle, so it may hold its charge rather than gain any. M5 says to
charge through "USB-C2" and recommends 5 V at 2 A; a 12 V socket charger
gives that. Whether the right-hand (data) port also charges is one of the
things still to check on a device.

## Troubleshooting

| What happens | Try |
| --- | --- |
| The phone is connected and its song shows, but nothing plays | BlueALSA, a second Bluetooth audio program in the base system, may have taken the music. Car 1.2.2 and ZeroOS 0.8.1 turn it off; on an older one, in the Terminal: `sudo systemctl disable --now bluealsa-aplay bluealsa`, then `systemctl --user restart wireplumber`, then disconnect and connect the phone. |
| Still nothing | Restart the Cardputer once (it loads Car's sound settings), and check the phone's audio output is set to cardputer (iPhone: Control Center; Android: Bluetooth > cardputer > Media audio on). |
| The music skips | Turn Wi-Fi off (Ctrl + Space) and see if it stops; Car does this by itself while the music plays and Wi-Fi isn't connected. Keep the phone near the Cardputer. Big downloads over a hotspot make it skip too. |
| The phone doesn't list `cardputer` | Use **Scan for devices** in Car with the phone's Bluetooth settings open. |
| The phone doesn't reconnect | Enter on its row in Car. Check Bluetooth is on (Ctrl + Space). |
| No song name, or no clock | Some apps and phones don't send them. The music still plays and the keys still work. |
| Fn + Q, W, E start other music | Update ZeroOS to 0.8.0 or newer. Until then, use Enter and the menu in Car. |
| The car doesn't see the USB stick | The switch must be on **Slave** and the cable in the right-hand USB-C port. Older stereos may want mp3 only, or a smaller stick. |
| It hums or buzzes on aux | A ground loop from charging off the car: try a different charger or a ground loop isolator in the aux cable. |

To see what the Cardputer's sound system is doing, in the Terminal:
`wpctl status` lists the phone under **Sources** when it is connected for
music, and a stream while music plays.

## How it works

| Piece | What it does |
| --- | --- |
| The Car app (`car.cpp`) | The screen: a ZeroOS List with the panel on the right and, while a phone is connected, the now playing card. It polls the phone every 2 seconds while open, the USB stick every second, and the device list every 5 seconds. |
| A phone's music | PipeWire receives it (the A2DP sink role) and plays it on the default output, as any other sound: the headphone jack, or a connected Bluetooth car. The phone's player (AVRCP) is an `org.bluez.MediaPlayer1` object under its device; Car reads the song, its length and position, and sends play, pause and skip with `busctl` (`phone_player.h`, tested by `tests/phone_player_test.cpp`). A device counts as a phone when `bluetoothctl info` lists an Audio Source or the phone icon. On ZeroOS 0.8.0 and newer, ZeroShell sends Fn + Q, W, E and a Bluetooth car's buttons to that player too, while it plays nothing itself. |
| `60-zeroos-car.conf` (WirePlumber) | Bluetooth audio without a login: ZeroShell runs as a service, and WirePlumber otherwise only starts Bluetooth audio for an active login (`monitor.bluez.seat-monitoring`). The Bluetooth roles without `hfp_hf`, so a phone isn't offered a hands-free kit and calls stay on it. And the bigger buffer: while a phone's music plays, PipeWire runs in 2048-sample steps instead of 512 (`node.force-quantum` on `bluez_input.*` nodes). Car restarts WirePlumber once when it opens after an install or update, so the settings apply without a restart. |
| BlueALSA off | The base system also runs BlueALSA (`bluealsa`, `bluealsa-aplay`). It took a phone's music before PipeWire could and then couldn't play it (`bluealsa-aplay` runs as root and can't reach the user's PipeWire: "Host is down"). Installing Car turns both services off; ZeroOS 0.8.1 and newer do too. |
| `zeroos-car-wifi.service` | Runs `/usr/lib/zeroos/car-wifi` as root: every 5 seconds, while Bluetooth is on, it asks BlueZ whether a phone's player is playing (`busctl`) and NetworkManager whether Wi-Fi is connected (`nmcli`), and turns Wi-Fi off or on again as described above. With Bluetooth off it only reads the switch from `/sys` and starts no program: ZeroOS stops bluetoothd while Bluetooth is off, and a question would start it again. What it turned off is noted in `/var/lib/zeroos/car`, so it is put back after a restart and when the service stops. Tested by `tests/car-wifi-test.sh`. |
| `zeroos-usb-stick.service` | Runs `/usr/lib/zeroos/usb-stick` as root: builds the image with `sfdisk`, `mkfs.vfat` and mtools, then sets up a USB mass-storage gadget (configfs) on the `dwc2` controller. Progress goes to `/run/zeroos/usb-stick.state`. |
| Bluetooth out | `bluetoothctl` pairs, trusts and connects; PipeWire's Bluetooth module (`libspa-0.2-bluetooth`) carries the sound; `pipewire-alsa` routes sox's playback of local files through PipeWire as well. |
| Car buttons | BlueZ presents a connected device's buttons as a Linux input device (via `uinput`). ZeroShell reads any input device that has media keys but no letters, so a USB multimedia remote works too. |

## What has been checked on a device

- USB stick mode in a car: the stereo took the stick, showed the song names,
  and its buttons played, paused and skipped.
- Music from an iPhone to the aux: it paired with **Add a phone**, the song
  controls worked, and the music played once BlueALSA was off. With Wi-Fi
  on it skipped; with Wi-Fi off it was better.

Not yet:

- the now playing card's clock on a real phone, and that an iPhone reports
  "playing" for its player (the Wi-Fi helper goes by that);
- that the bigger buffer and Wi-Fi off while the music plays stop the
  skips;
- an Android phone; that the phone keeps its calls (and whether it plays its
  ringtone over the aux too); that turning BlueALSA off at install is enough
  without a restart;
- whether other cars accept the stick (old stereos can be picky about size,
  folder depth and file types), and whether the data port charges the
  battery;
- pairing with a real Bluetooth car, that sound moves to it when it
  connects, and that its buttons arrive as media keys;
- how far behind the phone the sound runs (Bluetooth adds a fraction of a
  second; it doesn't matter for music).

## Limits

- One phone at a time, and no calls through the Cardputer.
- The car's own screen shows no song names over Bluetooth or aux, only with
  USB stick mode.
- No iPod emulation, so a stereo that only knows "iPod" on its USB socket
  won't see the Cardputer.
- Song names with letters outside Western European ones (Japanese, Greek,
  emoji...) show as gaps: the Cardputer's font doesn't have them.
- Pairing and scanning freeze the screen while they run (up to 40 seconds for
  a first pairing).
