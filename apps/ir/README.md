# IR Remote: a universal remote

The Cardputer Zero has an IR LED and an IR receiver. The **IR Remote** app turns them into a
universal remote for AV rooms and homes: TVs, projectors, displays, projector screens, receivers and
amplifiers, Blu-ray and DVD players, cable and satellite boxes, video conferencing, switchers,
cameras, shades and lighting control, air conditioners, fans and LED lights. It has the codes of
about 4,500 real remotes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB).

## Using it

The app opens on three lists.

**My remotes** are yours to keep and change (they live in `~/IR`).

- **New remote** (a row, or **N** in the menu): name it, then press **N** (New button) for each
  button you want: give the button a name,
  point your old remote at the Cardputer and press that button. NEC and Samsung codes are
  recognised and stored as codes; anything else is kept as a raw recording, which replays the same.
- On a remote: **Enter** sends the button, **L** learns it again, **N** adds a button, **D** (or
  **Del**) deletes one, after asking. The bar at the bottom of the screen shows these keys, and
  **Tab** opens the menu with them.

**Universal: send every code** is for when you don't know which remote a device uses.

1. Pick the kind of device and the button:

   | Kind | Buttons |
   | --- | --- |
   | TVs | Power, Power on, Power off, Input, HDMI 1, HDMI 2, Vol +, Vol -, Mute, Ch +, Ch - |
   | Projectors | Power, Power on, Power off, Input, Blank (AV mute), Freeze, Vol +, Vol -, Mute |
   | Displays (monitors, signage, touch screens) | Power, Power on, Power off, Input, HDMI 1, Vol +, Vol -, Mute |
   | Screens (projector screens and lifts) | Up, Down, Stop |
   | Audio (receivers, amplifiers, sound bars, speakers) | Power, Power on, Power off, Input, Vol +, Vol -, Mute, Play |
   | Players (Blu-ray, DVD, CD, VCR, LaserDisc) | Power, Play, Pause, Stop, Eject, Menu |
   | Set top boxes (cable, satellite, streaming) | Power, Ch +, Ch -, Guide, Play |
   | Conferencing (codecs and cameras) | Power, Mute, Vol +, Vol - |
   | ACs, Fans, Lights | Power, Off, Oscillate, Speed, On |

   Power on and Power off are the discrete codes, for when you need a room in a known state.
2. Point the Cardputer at the device and press **Enter**. Every distinct code for that button
   goes out, one every 150 ms, the most common first (for TV power: LG, Samsung, Sony, Panasonic...).
3. Press any key as soon as the device reacts. The app stops on the code it just sent and shows
   whose it is. **Left** and **Right** try the codes either side of it (you might have pressed a
   moment late), **Enter** sends it again.
4. **O** (Open brand) opens that brand's remotes, where you can find the model with all its
   buttons and press **S** to save it to My remotes.

**Browse or search** lists every remote by kind, brand and model. Besides the kinds above there
are Switchers and Scalers (Extron, tvONE, ZeeVee, Faroudja, HDMI switches and matrices), Control
Systems (Crestron, Xantech, Niles), Lighting Control (Lutron, LiteTouch, Vantage), Shades (Somfy,
Hunter Douglas), Pro Audio (Biamp, TASCAM, Shure) and more. Type to search: on the first
page the search covers every brand and model (`sam`, `bravia`, `roku`); inside a kind it filters
brands, inside a brand it filters models. **Backspace** edits the search, **Esc** clears it.
**1** to **9** pick a row, so digits don't search. On a remote, **S** saves a copy to My remotes.

## Flipper Zero files

Remotes are stored in the Flipper Zero's `.ir` format, so files go both ways: copy a `.ir` file
from a Flipper (or from Flipper-IRDB) into `~/IR` and it appears under My remotes; copy one of
yours to a Flipper's `infrared/` folder and it works there. Sync (docs/SYNC.md) doesn't carry
`~/IR` yet; Files and Taildrop do.

Every protocol in Flipper-IRDB can be sent: NEC, NECext, NEC42, NEC42ext, Samsung32, RC5,
RC5X, RC6, SIRC (12, 15 and 20 bit), Kaseikyo (Panasonic, Denon...), RCA, Pioneer, and raw
recordings (most air conditioners).

## How it works

| Part | Where |
| --- | --- |
| `.ir` reader and writer; the encoders that turn a protocol, address and command into IR timings; recognising learned codes | `base/shell/src/ir/ir.cpp` |
| The database and `~/IR` | `base/shell/src/ir/db.cpp` |
| The app | `apps/ir/ir_remote.cpp` |
| Sending and receiving: `ir-ctl` on the rc-core devices (`gpio-ir-tx`, `gpio-ir-recv`) | `LinuxHal::ir_send`, `ir_learn` in `base/shell/src/hal/linux_hal.cpp` |
| Building the database | `apps/ir/irdb/build_irdb.py` |
| Tests (encoders checked against known frames, file round trips) | `base/shell/tests/ir_test.cpp`, run by CI |
| Tests of the database build (kinds, merging, decoding) | `apps/ir/tests/irdb-test.sh`, run by CI |

The encoders are our own, written from the protocols' published timings; they pack address and
command bits the way the Flipper does, so codes it recorded replay the same. Every one of the
140,000 codes in the database (universal lists included) encodes, and the longest (a fan) is 959
edges, under the kernel's 1,024 limit.

To send, ZeroShell writes the timings as `pulse`/`space` lines and runs
`ir-ctl -d <tx device> -c <carrier> -s <file>`. It finds which `/dev/lircN` sends and which
receives from `ir-ctl --features`. Learning runs `ir-ctl -r --one-shot` for up to 5 seconds.

## The database

It comes with the IR Remote app (docs/APPS.md), whose package installs it in
`/usr/share/zeroos/ir` (about 60 MB on disk, about 2 MB in the package), so the base image
doesn't carry it. `apps/ir/irdb/build_irdb.py --fetch` clones a pinned Flipper-IRDB
commit and takes:

- every remote filed by kind (about 1,960);
- the Pronto set from its `_Converted_` folder (about 2,500 remotes converted from Philips Pronto
  files, much of it AV gear). These are filed by brand only, so `classify()` works out the kind of
  each: from the brand when it makes one kind of thing (Draper: screens, Polycom: conferencing),
  from the model number (Sony VPL: projector, BDP: Blu-ray, KDL: TV), or from its buttons
  (keystone and lamp: projector; eject and subtitle: player; tuner and surround: receiver).
  What it can't place goes under Miscellaneous; search finds it either way.

It writes:

- the `.ir` files, unchanged, as `<Kind>/<Brand>/<Model>.ir` under the kind they are listed as.
  Projector screens and HDMI switches filed under Projectors or Miscellaneous get kinds of their
  own, and Pronto brands take the main set's spelling (Dalite: Da-Lite);
- `index.tsv`, one line per remote, so the app lists 4,500 remotes without opening them;
- `universal/<Kind>.ir`: for each kind of device, every distinct code for each common button,
  most-used first, each credited to the brand that uses it most. Pronto files hold every code as a
  raw recording; a clean NEC, Samsung32 or SIRC frame is read as that protocol (as learning does),
  so it counts as the same code as elsewhere and goes out as a clean frame;
- `SOURCE.txt` (commit) and Flipper-IRDB's `LICENSE`.

`apps/ir/prepare.sh` runs it when the app is packaged (into `build/work/ir/irdb`); it is kept if it is already there.
To move to a newer Flipper-IRDB, change `COMMIT` in the
script. The app reads `ZEROOS_IR_DB` instead of `/usr/share/zeroos/ir` if it is set, which is how
the simulator gets codes:

```sh
apps/ir/irdb/build_irdb.py --src apps/ir/irdb/sample --out build/irdb-sample   # 16 hand-written remotes
ZEROOS_IR_DB=$PWD/build/irdb-sample ./build/sim/zeroshell
```

(or `--fetch --out build/irdb` for the full set).

### Licence

Flipper-IRDB is CC0-1.0 (public domain dedication). Its README notes that commits before
[2319685](https://github.com/Lucaslhm/Flipper-IRDB/commit/2319685f2cbf0cd3f809609622cade14d24fb819)
are not covered by that dedication; IR codes are facts about devices, and we ship the files as
published with their `LICENSE` and the commit they came from. Of its `_Converted_` folder (bulk
conversions from other databases) we take the Pronto set, converted and tested by Flipper-IRDB's
maintainers from Pronto remote files; its codes are facts about devices too, but it has no clearer
licence than that. Its CSV set is left out: it is converted from
[probonopd/irdb](https://github.com/probonopd/irdb), whose licence asks every product that ships it
to register with the project and send units on request. Its IR Plus set is left out too.

## Still to do

- Try it on the hardware: whether `gpio-ir-tx` sends with the stock overlay (docs/HARDWARE.md),
  and the range of the LED.
- Hold-to-repeat for volume keys (today each press sends one frame).
- Recognise more protocols when learning (RC5, RC6, SIRC are stored raw for now, which works but
  can't be read).
- Search across My remotes too; rename a remote.
