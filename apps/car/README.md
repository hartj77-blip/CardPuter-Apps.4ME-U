# Car

**Car** gets your music into a car stereo, with whatever the car has:

| Your car has | Use | What plays | Song names | Skip with |
| --- | --- | --- | --- | --- |
| An aux input (3.5 mm) | [Music from your phone](#music-from-your-phone) | Anything your phone plays: Spotify, Apple Music, the Jellyfin app, podcasts | On the Cardputer | Enter and the menu in Car, Fn + Q/W/E, or the phone |
| A USB socket | [USB stick mode](#usb-stick-mode) | Music files in `~/Music` on the Cardputer | On the car's screen | The car's own buttons |
| A USB socket | [USB live sound](#usb-live-sound) | Whatever plays on the Cardputer now: your phone's music passed on, Media, Jellyfin. A few seconds late | On the Cardputer | The car's skip and pause buttons |
| A USB socket that takes an iPod | [iPod mode](#ipod-mode) | The same, without the delay. Not every stereo accepts it | On the Cardputer | The car's play, pause and skip buttons |
| Bluetooth music | [Bluetooth](#bluetooth-to-a-car-speaker-or-headphones) | Anything Media or Jellyfin plays on the Cardputer, or your phone's music passed on | No | The car's buttons |

The USB ways need **one cable**: it carries the music and charges the
Cardputer, with nothing in the aux socket, so there is no hum from the car's
wiring ([One cable](#one-cable-music-and-power)). Other apps carry on
meanwhile: open GPS and the music plays on.

None of this is CarPlay or Android Auto: those only accept a phone certified
by Apple or Google, and the Cardputer can't stand in for one.

## Contents

1. [Quick start](#quick-start)
2. [The screen](#the-screen)
3. [Keys](#keys)
4. [Music from your phone](#music-from-your-phone)
5. [One cable: music and power](#one-cable-music-and-power)
6. [USB stick mode](#usb-stick-mode)
7. [USB live sound](#usb-live-sound)
8. [iPod mode](#ipod-mode)
9. [Bluetooth to a car, speaker or headphones](#bluetooth-to-a-car-speaker-or-headphones)
10. [Power in the car](#power-in-the-car)
11. [Troubleshooting](#troubleshooting)
12. [How it works](#how-it-works)
13. [What has been checked on a device](#what-has-been-checked-on-a-device)
14. [Limits](#limits)

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
   **USB stick** and wait for **On**.
3. Cable from the right-hand USB-C port to the car's USB socket; pick USB
   on the stereo.

**USB live** (your phone's music, Media or Jellyfin, over the USB cable):

1. Switch on the left side to **Slave**. In **Car**, Enter on **USB live**
   and wait for **On**.
2. Cable from the right-hand USB-C port to the car's USB socket; pick USB
   on the stereo and play **Live sound** (LIVE1).
3. Play something on the Cardputer, or on a phone paired with **Add a
   phone**. It comes out of the car about three to five seconds later.

**USB iPod** (the same, without the delay, if the stereo takes an iPod):
as USB live, but Enter on **USB iPod**, and pick iPod (or USB) on the
stereo. If the stereo won't have it, use USB live.

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
 | 2 <> USB stick          Off  |  | Enter  play, pause      |
 | 3 <> USB live           Off  |  | Ctrl+N  next            |
 | 4 <> USB iPod           Off  |  | Ctrl+B  back            |
 +------------------------------+  +-------------------------+
 Enter Pause                                             Tab Menu
```

- **The now playing card** runs across the top while a phone is connected:
  - left: a green play mark while the music plays, a grey pause mark when it
    doesn't;
  - the song (cut short with "..." when it doesn't fit), and on the right
    how far in it is and how long it is;
  - under it the artist, the phone's name, and an arrow with where the
    music goes if that isn't the headphone jack: **USB** while USB live
    sound or iPod mode is on, or the Bluetooth car or speaker;
  - a thin green line along the bottom for how far in the song is. The
    clock and the line keep running between the phone's updates (every 2
    seconds). Phones that don't say how long a song is get no clock or
    line.
  - Connected but nothing playing: the phone's name and "Connected. Play
    music on the phone."
- **The list:**
  - the connected phone comes first, with a play or pause mark on the right
    (short, so its name has the room);
  - **USB stick**, **USB live** and **USB iPod**, each with Off, On, or In
    use (and USB stick a percentage while it copies). One is on at a time:
    turning one on turns the others off;
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
| Enter | On the phone: play or pause. On USB stick, USB live or USB iPod: on or off. On a device: connect, disconnect, or pair a new one. On the last three rows: what they say |
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

## One cable: music and power

Charging from the car's USB socket while the sound goes through the aux
lead joins the car's wiring to itself in two places, and the stereo plays
the difference: a hum, or a whine that follows the engine. With the sound
on the USB cable too there is only one connection, and nothing to hum.

- **The cable** goes from the car's USB socket (the one the stereo plays
  sticks from, not a charge-only one) to the Cardputer's **right-hand USB-C
  port** (USB-C2). M5 says charging is through that port, "with the device on or off and
  in either USB Switch position", so the one cable also charges. How much a
  car's socket gives is another matter: see [Power in the car](#power-in-the-car).
- **The switch** on the left side must be on **Slave**. In Slave the USB-A
  port, the other USB-C port and Ethernet are off; that is how the board is
  wired. Set it back to **Host** to use them again.
- **Pick what plays:** [USB stick mode](#usb-stick-mode) for the files in
  `~/Music`, with the car as the player; [USB live sound](#usb-live-sound)
  for anything the Cardputer plays, a few seconds late; or
  [iPod mode](#ipod-mode) for the same without the delay, on a stereo that
  takes it.
- **Other apps carry on.** The modes run outside the Car app: press Esc and
  open GPS (or anything else), and the music plays on, also with the screen
  off. The GPS on a Cap LoRa-1262 or the Grove port works in Slave; a USB
  GPS dongle doesn't, because the USB-A port is off.

Two other ways to be rid of the hum, with the aux lead kept: a ground loop
isolator in the lead (a small transformer, about the price of the lead), or
a car with Bluetooth music, which needs no lead at all.

## USB stick mode

The Cardputer shows up in the car as a USB stick holding a copy of
`~/Music`. The car is the player, so its screen shows the song names and all
its buttons work.

1. Set the **Host / Slave switch** on the left side of the Cardputer to
   **Slave**. In Slave the USB-A port, the other USB-C port and Ethernet are
   off; that is how the board is wired.
2. Open **Car** and press Enter on **USB stick**. The first time (and
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

## USB live sound

For anything that isn't a file in `~/Music`: your phone's music passed on
through the Cardputer, Jellyfin, a radio stream in Media. A car's USB socket
only plays files from a stick, so the Cardputer shows up as a stick with
three very long songs, **Live sound** (`LIVE1.MP3` to `LIVE3.MP3`), and
whichever of them the car plays is the sound the Cardputer is making now.

1. Set the switch on the left side to **Slave**.
2. Open **Car** and press Enter on **USB live**. It says **On**.
3. Plug the right-hand USB-C port into the car's USB socket and pick USB on
   the stereo. The row says **In use** when the car has taken it up. Play
   **Live sound** on the car.
4. Play music on the Cardputer (Media, Jellyfin), or on a phone paired with
   [Add a phone](#music-from-your-phone). While USB live sound is on,
   everything the Cardputer plays goes to the car instead of the speaker,
   the headphone jack or a Bluetooth speaker; key clicks and games too.

Enter on the row again turns it off, and the sound comes back out of the
Cardputer.

What to expect:

- **It runs three to five seconds late.** The car reads a few seconds of a
  song ahead before it plays them, as with any stick, and there is no way to
  tell it not to. For music that doesn't matter. It does mean a press of
  pause or skip takes that long to be heard, and it is no good for a video
  or a game.
- **The car's buttons work**, as far as a stick allows. Next and previous
  on the car skip the song on the phone (or in Media's queue); pause on the
  car pauses it, and play carries on. The Cardputer works these out from
  how the car reads the stick (it starts another song, or stops reading),
  and passes them on as the same play and skip keys a Bluetooth car sends.
  So does switching the car off: the music pauses, and carries on when the
  car is back. As with a Bluetooth car, play with nothing loaded starts
  `~/Music` from the top.
- **The car's screen** says "Live sound" by "Cardputer", not the song. The
  song is on the Cardputer, in Car's now playing card or in Media.
- **The clock on the car** counts up to 3 hours 43 minutes, then the car
  goes on to the next "song" by itself with a moment's gap. Winding
  forward or back does nothing useful.
- **Volume:** the Cardputer's volume keys set how loud the sound is sent.
  Leave it at 100% and use the car's knob.
- **Battery:** making the MP3 keeps one of the four processor cores partly
  busy while something plays.

It sends MP3 at 320 kbit/s, which every stereo that plays MP3 should take.
For one that doesn't, or to change the delay, put either or both of these
lines in `/etc/zeroos/car-live.conf` and turn the mode off and on:

```
KBPS=192
DELAY_MS=3000
```

`KBPS` is the MP3 bit rate (128, 160, 192, 224, 256 or 320). A lower rate
makes the delay longer on most cars, because they read ahead by size, not by
time. `DELAY_MS` is how far behind the sound the car starts (500 to 10000):
shorter is less delay, but a car that reads in big gulps may then stutter.

## iPod mode

Many stereos made before CarPlay took over have a USB socket that plays an
iPod: the iPod sends its sound down the cable as it plays, and the stereo's
buttons drive it. In iPod mode the Cardputer answers as an iPod would, so
whatever it plays reaches the car over the one cable **without the delay**
of USB live sound.

1. Set the switch on the left side to **Slave**.
2. Open **Car** and press Enter on **USB iPod**. It says **On**.
3. Plug the right-hand USB-C port into the car's USB socket and pick iPod
   (or USB) on the stereo. The row says **In use** when the car has taken
   the cable up.
4. Play music on the Cardputer, or on a phone paired with
   [Add a phone](#music-from-your-phone). As with USB live sound,
   everything the Cardputer plays goes to the car while the mode is on.

Enter on the row again turns it off.

**Not every stereo accepts it.** The Cardputer isn't an iPod and can't
prove it is one. Stereos that only check that an iPod answers play it;
stereos that ask the iPod to prove itself, and some that are simply
particular, say "unsupported" or show nothing. The project this mode is
built on keeps [reports of which stereos work](https://github.com/oandrew/ipod-gadget/issues).
If yours doesn't, [USB live sound](#usb-live-sound) is the way that works
on anything that plays a stick.

What to expect:

- **The car's buttons work:** play, pause, next and previous reach whatever
  is playing, like a Bluetooth car's buttons.
- **The car's screen** shows no song names; the stereo may show a blank
  song or "iPod". The song is on the Cardputer.
- **Volume:** the Cardputer's volume keys set how loud the sound is sent.
  Leave it at 100% and use the car's knob.
- **It needs the kernel ZeroOS shipped with** (6.18.39): the driver in the
  package is built for that one. ZeroOS keeps its kernel, so this only
  matters if you changed it; the row then says **Failed**, "iPod mode isn't
  built for this kernel".

For a stereo that sees the cable but doesn't start, there are three things
to try, as lines in `/etc/zeroos/car-ipod.conf` (turn the mode off and on
after a change):

```
SWAP_CONFIGS=1
ONLY_IPOD=1
PRODUCT_ID=0x1261
```

A real iPod offers the car two faces, a plain storage one first and the
iPod one second. `SWAP_CONFIGS=1` puts the iPod one first, for a stereo that
only looks at the first; `ONLY_IPOD=1` leaves the storage one out.
`PRODUCT_ID` says which iPod or iPhone to be (four hex digits; the driver's
[list](https://github.com/oandrew/ipod-gadget/blob/master/doc/apple-usb.ids)
has them), for a stereo that only knows some.

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
uses when idle, so on the car's own socket it may hold its charge rather
than gain any, and with the screen on and USB live sound running it may
slowly lose some. M5 recommends 5 V at 2 A, which a 12 V socket charger
gives; but a charger carries no music, so with one of those the sound has to
go by aux (and may hum) or Bluetooth.

M5 says charging is through USB-C2, the right-hand port, which is also the
port the USB modes use, and that it works in either switch position. That
the battery really gains charge with the switch on Slave and a car on the
other end is still to check on a device: watch **Settings > Battery &
power**, which shows the current going in or out.

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
| It hums or buzzes on aux | A ground loop from charging off the car. Use [one cable](#one-cable-music-and-power) for both (USB stick mode or USB live sound), or put a ground loop isolator in the aux lead. |
| USB live: the car plays "Live sound" but it is silent | Nothing is playing on the Cardputer, or its volume is at 0 or muted. Play something in Media to check. A phone must be connected (its row in Car) and playing. |
| USB live: the car says the file can't be played, or skips it | Try a lower rate: `KBPS=192` in `/etc/zeroos/car-live.conf` (see [USB live sound](#usb-live-sound)). Some stereos refuse very long songs; that can't be changed yet. |
| USB live: it stutters | Make the delay longer: `DELAY_MS=5000` in the same file. |
| USB live: songs skip or pause by themselves | The car is reading the stick in a way the Cardputer takes for a button. `journalctl -u zeroos-car-live` shows what it thought the car said ("the car says next"); please report it with the car's make. |
| USB live: **Failed** | The panel on the right says why. "no USB device port": the switch isn't on Slave. Otherwise `journalctl -u zeroos-car-live -u zeroos-usb-stick` has the details. |
| No sound from the Cardputer's speaker | USB live sound or iPod mode is on: everything goes to the car. Turn it off in Car. |
| USB iPod: the car says "unsupported", or nothing happens | Try the three settings in [iPod mode](#ipod-mode), one at a time. If none helps, this stereo wants a real iPod: use USB live. `journalctl -u zeroos-car-ipod` shows what the car asked and where it stopped; it helps a report. |
| USB iPod: **In use**, the car shows an iPod, but no sound | Check something is playing and the volume isn't at 0. Then turn the mode off and on with the cable in. |
| USB iPod: **Failed** | The panel says why. "isn't built for this kernel": the kernel was changed from the one ZeroOS shipped. "no USB device port": the switch isn't on Slave. |

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
| `zeroos-usb-stick.service` | Runs `/usr/lib/zeroos/usb-stick` as root, for whichever USB mode the Car app named in `~/.config/zeroos/car-usb` (`files`, `live` or `ipod`; the app writes it, then starts the service). For USB stick mode it builds the image with `sfdisk`, `mkfs.vfat` and mtools; for USB live sound it starts `zeroos-car-live.service`. For those two it then sets up a read-only USB mass-storage gadget (configfs) on the `dwc2` controller. Progress goes to `/run/zeroos/usb-stick.state`, the mode that is on to `/run/zeroos/usb-stick.mode`. Tested, with stand-ins for the kernel's side, by `tests/car-usb-test.sh`. |
| `zeroos-car-live.service` (USB live sound) | Runs `/usr/lib/zeroos/car-live` (`live/main.cpp`) as root. It serves one file, `/run/zeroos/car-live/stick.img`, through FUSE, and that file is what the gadget offers the car. Nothing of the stick is stored: the partition table, the FAT32 volume and the three 512 MiB songs are worked out as the car reads them (`live/live_stick.h`), and a read of any part of any song is answered with the MP3 stream from three seconds back, on a whole frame, then as fast as the sound is made. The file is opened without the kernel's cache, so every read the car makes arrives once, as it happens. |
| The sound for it | `pw-record` runs as the UI user with `media.class=Audio/Sink`, so PipeWire shows it as an output, "Car (USB)", with a higher priority than the speaker, the jack or Bluetooth: WirePlumber makes it the default while it runs, and everything goes back when it stops. Its samples go through `lame` (constant bit rate, no bit reservoir, flushed frame by frame) into the program. With nothing playing PipeWire sends nothing, and the program fills in silent frames, only ever between whole frames, so the car's song never runs dry or clicks. |
| iPod mode | `zeroos-usb-stick.service` loads three kernel modules from `/usr/lib/zeroos/car-ipod/<kernel>/`, built from [ipod-gadget](https://github.com/oandrew/ipod-gadget): they give the USB port an iPod's two interfaces, a sound interface (which appears here as a sound card, `iPodUSB`, taking 44.1 kHz stereo) and one that carries the iPod's protocol (which appears as `/dev/iap0`). It then starts `zeroos-car-ipod.service`, which runs three things: the [ipod](https://github.com/oandrew/ipod) client, which answers the car on `/dev/iap0` and lets the sound start; `zeroos-car-keys` (`live/keys.cpp`), which presses the buttons the client reports (our one change to it, `ipod/buttons.patch`) on the same made-up keyboard as USB live sound; and the sound, `pw-record` as the "Car (USB)" output again, piped into `aplay` on the driver's sound card. A WirePlumber rule keeps PipeWire itself off that card, which takes one player. Turning the mode off stops the client first, then takes the modules out. `apps/car/prepare.sh` builds the modules and the client when the package is made; `tests/car-keys-test.sh` checks the buttons. |
| The car's buttons over USB | A stick has no buttons, but how the car reads tells: a song read from its start after another was playing is next or previous (the three songs are in a ring, so which way is known; two quick presses pass through a song), the same song from its start is previous, and no reads for five seconds is pause. A jump only counts once the car has read on for a few seconds, so a car that looks at the other songs' names and comes back presses nothing. The program presses play, pause, next and previous on a keyboard it makes with `uinput`, which ZeroShell reads like a Bluetooth car's buttons. `tests/live_stick_test.cpp` checks the stick and the buttons; `tests/car-live-test.sh` runs the program and reads its stick with fsck, mtools and a stand-in car. |
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
- **USB live sound, at all.** It has run on a PC only: the sound system,
  the encoder and the program together, with the stick read back and played
  (clean, three seconds late). Not yet: that the Cardputer's kernel offers
  the FUSE file as a stick, that a car plays a 512 MiB MP3 that is still
  being made, how far behind it really runs, and whether a real car's reads
  are taken for the right buttons;
- **iPod mode, at all.** The driver builds for ZeroOS's kernel and its
  symbols match that kernel's; the client builds, and recorded button
  messages replayed through it come out as the right keys. Nothing has been
  loaded on a Cardputer or plugged into a car: that the modules load, that
  the sound card takes the sound, and which stereos accept it;
- that one cable charges the battery while it carries the music (M5 says
  the port charges in either switch position);
- an Android phone; that the phone keeps its calls (and whether it plays its
  ringtone over the aux too); that turning BlueALSA off at install is enough
  without a restart;
- whether other cars accept the stick (old stereos can be picky about size,
  folder depth and file types);
- pairing with a real Bluetooth car, that sound moves to it when it
  connects, and that its buttons arrive as media keys;
- how far behind the phone the sound runs (Bluetooth adds a fraction of a
  second; it doesn't matter for music).

## Limits

- One phone at a time, and no calls through the Cardputer.
- The car's own screen shows no song names over Bluetooth, aux or USB live
  sound, only with USB stick mode.
- USB live sound is a few seconds late, and stereos that don't play MP3
  from a stick can't use it.
- iPod mode can't prove it is an iPod, so stereos that check refuse it; and
  it is not CarPlay.
- Song names with letters outside Western European ones (Japanese, Greek,
  emoji...) show as gaps: the Cardputer's font doesn't have them.
- Pairing and scanning freeze the screen while they run (up to 40 seconds for
  a first pairing).
