# IR Remote: a universal remote

The Cardputer Zero has an IR LED and an IR receiver. The **IR Remote** app turns them into a
universal remote for AV rooms and homes: TVs, projectors, displays, projector screens, receivers and
amplifiers, Blu-ray and DVD players, cable and satellite boxes, video conferencing, switchers,
cameras, shades and lighting control, air conditioners, fans and LED lights. It has the codes of
about 4,500 real remotes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB).

## Using it

The app opens on **My remotes**, then the kinds of device (TVs, Projectors, Monitors, Projector
Screens, receivers, players, cable boxes, video conferencing, switchers, control and lighting
systems, ACs, fans, lights and more: 52 in all, the AV ones first), then **Search all remotes**.

**My remotes** are yours to keep and change (they live in `~/IR`).

- **New remote** (a row, or **N** in the menu): name it, then press **N** (New button) for each
  button you want: give the button a name,
  point your old remote at the Cardputer and press that button. NEC and Samsung codes are
  recognised and stored as codes; anything else is kept as a raw recording, which replays the same.
- On a remote: **Enter** sends the button, **L** learns it again, **N** adds a button, **D** (or
  **Del**) deletes one, after asking. The bar at the bottom of the screen shows these keys, and
  **Tab** opens the menu with them.

**A kind of device** lists **Universal: every brand** at the top, then its brands A to Z. Universal
and each brand are one remote with every common button the brand (or, for Universal, every brand)
uses: 48 for Sony TVs, 51 for Universal TVs. Power, discrete Power on and Power off, Input,
HDMI 1 to 4, volume, channels, arrows, OK, Menu, Back, Home, Info, Guide, Blank and Freeze for
projectors, Aspect, Picture, transport keys, Eject, digits, and Oscillate, Speed, Mode,
temperature and brightness for fans, ACs and lights. A brand with one remote of a kind opens that
remote.

1. Point at the device and press **Enter** on a button (Power is a good start). Every code for
   that button goes out, one every 150 ms, most common first: 15 Power codes for Sony TVs, about
   4 seconds; 209 for Universal TVs, about 50 seconds.
2. Press any key as soon as the device reacts. The remote **locks** to that code's set: its
   protocol and device address, which a maker keeps across a range of models (Sony TVs: SIRC,
   address 01). **Left** and **Right** try the codes either side if you pressed late. On
   Universal the screen names the brand and remote the code came from.
3. From then on each button sends one code, from the locked set, at once. When a set has more
   than one code for a button (some remotes label discrete on/off or a second input as Power),
   **Left** and **Right** after sending try the others, and the remote keeps the one you pick.
   A button the set doesn't have sweeps every code for it, and locks again on the one that works.
4. **S** saves the locked remote to My remotes (Sony TVs: 36 buttons); **U** unlocks it to find
   the code again. The lock lasts until you leave the app. **M** lists the brand's models (on
   Universal, the models of the brand it locked to), each with all its buttons.

Why lock instead of sending every code on each press: a sweep of Sony TV codes takes 1 to 4
seconds per button, and buttons that toggle (Power, Input, Mute) can act twice when two codes in a
sweep both work. Most of a brand's devices share one set: 46 of 75 Sony TV remotes have the same
Power code.

**Search all remotes** lists every remote by kind, brand and model. Type to search: on the first
page the search covers every brand and model (`sam`, `bravia`, `roku`); inside a kind it filters
brands, inside a brand it filters models. **Backspace** edits the search, **Esc** clears it.
**1** to **9** pick a row, so digits don't search. On a remote, **S** saves a copy to My remotes.
Typing filters every list the same way (`son` on TVs finds Sony).

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
| Tests of the database build (kinds, merging, decoding, brand remotes) | `apps/ir/tests/irdb-test.sh`, run by CI |

The encoders are our own, written from the protocols' published timings; they pack address and
command bits the way the Flipper does, so codes it recorded replay the same. Every one of the
140,000 codes in the database encodes, and the longest (a fan) is 959
edges, under the kernel's 1,024 limit.

To send, ZeroShell writes the timings as `pulse`/`space` lines and runs
`ir-ctl -d <tx device> -c <carrier> -s <file>`. It finds which `/dev/lircN` sends and which
receives from `ir-ctl --features`. Learning runs `ir-ctl -r --one-shot` for up to 5 seconds.

## The database

It comes with the IR Remote app (docs/APPS.md), whose package installs it in
`/usr/share/zeroos/ir` (about 73 MB on disk, about 2.8 MB in the package), so the base image
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
- `brands/<Kind>/<Brand>.ir` for each brand with two or more remotes of a kind (471 of them):
  every distinct code the brand uses for each of 55 common buttons, most-used first (up to 60 a
  button), each credited to a remote it came from;
- `brands/<Kind>/_all.ir`, the Universal remote of each kind (47 of them): every brand's codes the
  same way, up to 400 a button, each credited to a remote of the brand that uses it most;
- `brands/index.tsv`: kind, brand (empty for Universal), number of remotes, path, buttons.

  Pronto files hold every code as a raw recording; a clean NEC, Samsung32 or SIRC frame is read as
  that protocol (as learning does), so it counts as the same code as elsewhere and goes out as a
  clean frame;
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
