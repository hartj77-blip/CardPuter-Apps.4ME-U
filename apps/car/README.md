# In the car

The **Car** app gives three ways to play music through a car stereo. None is
CarPlay or Android Auto: those only accept a phone certified by Apple or
Google, and the Cardputer cannot stand in for one.

| | USB stick mode | Bluetooth | Music from your phone |
| --- | --- | --- | --- |
| Works with | Any stereo that plays music from a USB stick | Stereos, speakers and headphones with Bluetooth music | A stereo with an aux input (or Bluetooth music) |
| What plays | Files in `~/Music` | Anything Media or Jellyfin plays | Anything the phone plays: Spotify, the Jellyfin app, podcasts |
| Who is the player | The car | The Cardputer | The phone |
| Car buttons | All of them (it is the car's own player) | Play, pause, next, previous | None over aux; Bluetooth: play, pause, next, previous |
| Track names on the car | Yes | No | No; on the Cardputer's screen |
| Cable | One USB-C cable, data and power | None for sound; any USB charger for power | Aux: a 3.5 mm cable from the headphone jack |

## USB stick mode

1. Set the **Host / Slave switch** on the left side of the Cardputer to
   **Slave**. In Slave the USB-A port, the other USB-C port and Ethernet are
   off; that is how the board is wired.
2. Open **Car** and press Enter on **USB stick mode**. The first time (and
   whenever `~/Music` has changed) it copies the music into a disk image:
   **Copying music 43%**. Then it says **on**.
3. Plug the **right-hand USB-C port** into the car's USB socket. When the car
   has taken it up the app says **in use**. Pick USB as the source on the
   stereo and use the car's buttons.

Enter on the row again turns it off. Set the switch back to **Host** to use
USB devices and Ethernet again.

What the car sees is a read-only FAT32 stick called `MUSIC` with the same
folders as `~/Music`, files in name order. It is a copy
(`/var/lib/zeroos/car/music.img`), so it needs as much free space again as
the music takes, and the car cannot change your files. Only music files are
copied: mp3, m4a, aac, wma, wav, flac, ogg, opus. An older stereo may only
play mp3 (and perhaps wma or aac) and skip the rest.

## Bluetooth

In **Car**, under **Bluetooth**:

- **Scan for devices**: put the car, speaker or headphones in pairing mode,
  then scan (8 seconds). Press Enter on it in the list to pair and connect.
- **Let the car find me**: for cars that search for a phone from their own
  screen. The Cardputer is visible as `cardputer` for 2 minutes; pick it on
  the car's screen. Only the first device that pairs is kept (and trusted to
  reconnect by itself); the Cardputer then hides again at once.

Pairing is only possible while you pair: when you pick a device to pair, the
Cardputer accepts pairing for that device alone until it has connected, and
unpairs anything else that pairs in that time. It is never visible to others
except during **Let the car find me**.
- Enter on a paired device connects or disconnects it. **Tab > D** (or **Del**) forgets it, after
  asking. The bar along the bottom says what Enter does on the row in focus; typing filters the list.

Once connected, sound from Media and Jellyfin goes to that device, and its play,
pause, next and previous buttons drive the play queue, also with the screen
off. Pressing play with nothing queued starts `~/Music` from the top.

A paired car reconnects by itself when it starts, as long as the Cardputer's
Bluetooth is on. **Settings > Bluetooth auto-off** turns Bluetooth off after
a while with nothing connected, so set it to **Never** if the Cardputer lives
in the car.

## Music from your phone

For a car with an aux input but no Bluetooth music: the phone sends its
music to the Cardputer over Bluetooth, and the Cardputer plays it out of
its headphone jack into the aux, like a Bluetooth aux adapter. Calls stay
on the phone: the Cardputer takes the phone's music but doesn't offer to
be its hands-free kit, so the phone rings and you talk on it as usual, and
the music pauses for the call.

**Pair the phone once.** In **Car**, press Enter on **Add a phone**. The
Cardputer can be found as `cardputer` for 2 minutes: on the phone, open
the Bluetooth settings, pick **cardputer** and accept. (Or open the phone's
Bluetooth settings so it can be found, then use **Scan for devices** here.)
The phone then shows in the list with a phone icon.

**In the car.** Plug a 3.5 mm cable from the Cardputer's headphone jack
into the car's aux and pick AUX on the stereo. Connect the phone (Enter on
its row in **Car** if it didn't connect by itself) and play music on it.
The row says **Playing** or **Paused**, and the panel beside it shows the
song and the artist.

| To | Press |
| --- | --- |
| Pause or play | Enter on the phone's row |
| Next or previous song | **Tab > N** or **Tab > B** (Ctrl + N, Ctrl + B) |
| Disconnect the phone | **Tab > X** |
| Pause, play and skip from anywhere, also with the screen off | Fn + Q, W, E (ZeroOS 0.8.0 or newer) |
| Change the volume | The phone's volume, the Cardputer's volume keys, or the car's knob |

The car's buttons can't reach the Cardputer over an aux cable: that carries
only sound. With ZeroOS 0.7.2 or older, Fn + Q, W, E start the Cardputer's own
music over the phone's; use the menu or the phone instead.

If a Bluetooth car, speaker or headphones is connected as well, the phone's
music goes there instead of the headphone jack, and on a newer ZeroOS that
device's play and skip buttons reach the phone too. While the Cardputer
plays something of its own (Media, Jellyfin), the keys are for that.

Keep **Settings > Bluetooth auto-off** at a setting that suits you: a
connected phone counts as connected, so Bluetooth stays on while it is.

## Power

A car's USB socket often gives only 0.5 A (2.5 W), about what the Cardputer
uses when idle, so it may hold its charge rather than gain any. M5 says to
charge through "USB-C2" and recommends 5 V at 2 A; a 12 V socket charger
gives that. Whether the right-hand (data) port also charges is one of the
things still to check on a device.

## How it works

| Piece | What it does |
| --- | --- |
| `zeroos-usb-stick.service` | Runs `/usr/lib/zeroos/usb-stick` as root: builds the image with `sfdisk`, `mkfs.vfat` and mtools, then sets up a USB mass-storage gadget (configfs) on the `dwc2` controller. Progress goes to `/run/zeroos/usb-stick.state`. |
| Bluetooth | `bluetoothctl` pairs, trusts and connects; PipeWire's Bluetooth module (`libspa-0.2-bluetooth`) carries the sound; `pipewire-alsa` routes sox's playback of local files through PipeWire as well. |
| Car buttons | BlueZ presents a connected device's buttons as a Linux input device (via `uinput`). ZeroShell reads any input device that has media keys but no letters, so a USB multimedia remote works too. |
| A phone's music | PipeWire receives it (the A2DP sink role) and plays it on the default output, as any other sound: the headphone jack, or a connected Bluetooth car. The phone's player (AVRCP) is an `org.bluez.MediaPlayer1` object under its device; Car reads the song and sends play, pause and skip with `busctl` (`phone_player.h`). A device counts as a phone when `bluetoothctl info` lists an Audio Source or the phone icon. On ZeroOS 0.8.0 and newer, ZeroShell sends Fn + Q, W, E and a Bluetooth car's buttons to that player too, while it plays nothing itself. |
| BlueALSA off | The base image also runs BlueALSA (`bluealsa`, `bluealsa-aplay`), a second Bluetooth audio stack. It took a phone's music before PipeWire could and then couldn't play it (`bluealsa-aplay` runs as root and can't reach the user's PipeWire: "Host is down"), so the phone was connected and its buttons worked, but nothing played. Installing Car turns both services off; ZeroOS 0.8.1 and newer do too. |
| `/etc/wireplumber/wireplumber.conf.d/60-zeroos-car.conf` | WirePlumber settings. Bluetooth audio without a login: ZeroShell runs as a service, and WirePlumber otherwise only starts Bluetooth audio for an active login (`monitor.bluez.seat-monitoring`). And the Bluetooth roles without `hfp_hf`, so a phone isn't offered a hands-free kit and calls stay on it. Car restarts WirePlumber once when it opens after an install or update, so the settings apply without a restart. |

## Checked on a device

USB stick mode has worked in a car: the stereo took the stick, showed the
track names, and its buttons played, paused and skipped.

Music from an iPhone: it paired, and the song controls worked. Its music
only played once BlueALSA was off (Car 1.2.2 does that).

If a phone is connected and its buttons work but nothing plays, in the
Terminal:

```
sudo systemctl disable --now bluealsa-aplay bluealsa
systemctl --user restart wireplumber
```

then disconnect and connect the phone again. `wpctl status` lists the phone
under Sources, and a stream while music plays.

## Not checked on a device yet

The rest hasn't run on a Cardputer or in a car yet. In particular:

- whether other cars accept the stick (old stereos can be picky about
  size, folder depth and file types);
- whether the data port charges the battery;
- pairing with a real car, and that sound moves to it when it connects;
- that the car's buttons arrive as media keys;
- that Bluetooth audio needed the seat setting in `60-zeroos-car.conf`, and
  that restarting WirePlumber when Car opens applies it;
- music from a phone: an Android phone; the song on the screen; that the
  phone keeps its calls (and whether it plays its ringtone over the aux
  too); that turning BlueALSA off at install is enough without a restart;
- how far behind the phone the sound runs (Bluetooth adds a fraction of a
  second; it doesn't matter for music).

## Limits

- The car's screen does not show the track name over Bluetooth.
- Music from a phone: one phone at a time, and no calls through the car.
- No iPod emulation, so a stereo that only knows "iPod" on its USB socket
  will not see the Cardputer.
- Pairing and scanning freeze the screen while they run (up to 40 seconds for
  a first pairing).
