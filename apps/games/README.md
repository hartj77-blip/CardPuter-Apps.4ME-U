# Games

**Games** plays your games for classic consoles on the built-in screen, or on a TV or monitor on
the HDMI port. It runs the emulators as **libretro cores**: the same emulator cores RetroArch uses,
loaded by an app built for the Cardputer's 320 x 170 screen and keyboard. Two players (up to four)
can play with gamepads. Install it from the **Apps** app.

Only play games you own. ZeroOS comes with no games and no console BIOS files.

## Why not RetroArch itself?

RetroArch draws with OpenGL, and the built-in screen is a small SPI panel with no GPU behind it.
RetroArch also expects a gamepad and a big screen for its menus. So Games is a small frontend of
its own that runs the same libretro cores, scales each picture to fit the panel, and uses the
keyboard. A core you download for Games is a normal libretro core, and the save files are the
usual `.srm` battery saves.

Games plays on a TV itself (see [On a TV](#on-a-tv)). In Dock mode ([Dock](../dock/README.md))
you can also run RetroArch (`sudo apt install retroarch`), with the cores Games installed.

## Consoles

| Console | Folder in `~/Games` | Files | Core |
| --- | --- | --- | --- |
| NES / Famicom | `nes` | `.nes .fds .unf` | Nestopia (comes with Games), FCEUmm |
| Game Boy | `gb` | `.gb` | Gambatte (comes with Games), mGBA |
| Game Boy Color | `gbc` | `.gbc` | Gambatte, mGBA |
| Game Boy Advance | `gba` | `.gba` | mGBA (comes with Games) |
| Super Nintendo | `snes` | `.sfc .smc` | Snes9x 2010 |
| Mega Drive / Genesis | `md` | `.md .gen .smd .bin` | Genesis Plus GX, PicoDrive |
| Master System / Game Gear | `sms` | `.sms .gg .sg` | Genesis Plus GX, PicoDrive |
| PC Engine / TurboGrafx-16 | `pce` | `.pce` | Beetle PCE Fast |
| Atari 2600 | `a26` | `.a26` | Stella 2014 |
| PlayStation | `psx` | `.cue .chd .pbp .m3u` | PCSX ReARMed |
| WonderSwan | `ws` | `.ws .wsc` | Beetle WonderSwan |

Unzip games first: cores here read the plain file, not a `.zip`. PlayStation games need the `.cue`
and its `.bin` files side by side; for a game on several discs, an `.m3u` that lists them is
shown instead. PCSX ReARMed has its own BIOS built in. A real one in `~/Games/system/` is used if
it's there, and some games need it.

## Getting games on

Games opens with a folder for each console in `~/Games`. Copy your files in with:

- **Files**, from a USB stick
- **Sync** (Syncthing), from a folder on your PC
- **Remote**'s Taildrop, then move them from `~/Downloads`
- `scp game.gba pi@cardputer.local:Games/gba/` from a PC

## Cores

NES, Game Boy, Game Boy Color and Game Boy Advance work as soon as Games is installed: their
cores come from Debian with it. For the other consoles, press **C** for the core list and
**Enter** on one to download it from libretro's nightly builds (buildbot.libretro.com, about
1 to 3 MB each). Downloaded cores go in `~/Games/cores`; **Del** removes one. Removing the Games
app keeps your games, saves and downloaded cores.

You can also copy any `<name>_libretro.so` built for arm64 into `~/Games/cores` yourself. Games
uses a core it knows for a console (see the table above). A copy in `~/Games/cores` is used
before the system one.

## Playing

| Key | |
| --- | --- |
| W A S D (or the arrow keys) | D-pad |
| K / J | A / B |
| I / U | X / Y |
| Q / E | L / R |
| Enter / Space | Start / Select |
| Backspace (Del), held | Fast forward (3x) |
| Esc | Menu: Resume, Save state, Load state, Swap players 1 and 2, Play on TV, Border, Picture, Carry on next time, Reset, Quit game |

On a full-size keyboard, X and Z are A and B too. Hold several keys at once: Games reads the
keyboard directly, so you can run and jump.

**Gamepads** work as well: USB, or Bluetooth. Pair a Bluetooth one once in the Terminal: turn on
Bluetooth in **Settings**, put the gamepad in pairing mode, then run `bluetoothctl`, and in it
`scan on`, then `pair`, `trust` and `connect` with the gamepad's address. Buttons follow the usual
layout, the d-pad or left stick moves, Home/Guide opens the menu, and holding the right stick in
fast forwards. A gamepad that connects
mid-game is picked up within a couple of seconds. In the menu, the d-pad moves, A (or Start)
chooses and B goes back to the game, so you don't need the keyboard on the sofa.

**Two players (or more).** Pair two gamepads the same way, and each one is a player of its own:
the first to connect is player 1, the next player 2, then 3 and 4. A note on the screen says which
player a gamepad that connects mid-game is. The keyboard is always player 1, so the keyboard and
one gamepad can play together: choose **Swap players 1 and 2** and the gamepad becomes player 2.
That menu row also fixes the wrong gamepad being player 1. A Bluetooth gamepad that drops out and
comes back gets its player back, if nobody has taken it meanwhile.

Games tells every core it has four players, and plugs a joypad into ports 1 and 2. Players 3 and 4
need a core that sets up a multitap itself (some SNES and PC Engine cores have it as a core
option, which Games leaves at the core's default), so most games are one or two players.

**Saves.** Games with a battery save (most RPGs) are saved as they are played and when you quit,
in `~/Games/saves/<game>.srm`. **Save state** stores the exact moment in
`~/Games/states/<game>.state` (one per game); **Load state** goes back to it. Turning the screen
off, or putting the Cardputer face down, pauses the game.

**Carry on where you left off.** Quitting a game keeps the moment you quit at, in
`~/Games/states/<game>.resume.state`, and starting it again carries on from there. **Reset** in
the menu starts the game over; **Carry on next time: Off** turns this off.

**Sound** plays through the speaker or headphones at the system volume.

## How well it runs

The Cardputer Zero has a quad-core 1 GHz Arm Cortex-A53. NES, Game Boy, Game Boy Color, Master
System, Game Gear, Atari 2600, PC Engine, WonderSwan and Mega Drive run at full speed. Game Boy
Advance and Super Nintendo (Snes9x 2010) run at full speed in most games. bsnes-mercury is too
slow here; use Snes9x 2010. PlayStation (PCSX ReARMed) runs many games at full speed. These are
estimates for this chip and haven't been measured on the device yet. Use the **Performance**
power profile in **Battery** for the demanding ones.

The screen redraws at 60 frames a second while a game is on (30 for the rest of ZeroOS).

## On a TV

Plug a TV or monitor into the HDMI port, start a game, and it plays on the TV. Plugging the TV in
mid-game works too: within a second the picture moves over, and unplugging brings it back to the
built-in screen. The built-in screen says "Playing on the TV"; the menu (**Esc**, or Home on a
gamepad) shows on both, and the notes ("State saved", "Gamepad connected: player 2") show on the
TV as well. **Play on TV: Off** in the menu keeps the game on the built-in screen.

The TV runs at 720p (or its preferred mode up to 1080p when it has no 720p). The picture is scaled
up by a whole number where that fills most of the screen, so the pixels stay sharp and square
(NES 3x, Game Boy 5x, GBA 4x on 720p), else to the TV's height, in the console's own shape with
black bars beside it. Border and Picture only change the built-in screen.

| | |
| --- | --- |
| Sound | Stays on the Cardputer's speaker or headphones, not the TV |
| Dock mode | Has the HDMI port while it runs: leave Dock mode first, or Games says so |
| Plugging in mid-game | Needs a ZeroOS newer than 0.5.1. Older ones open the Dock app when a monitor is plugged in, which closes the game (it carries on from there next time). With a TV plugged in before you start, any ZeroOS works |

How it works: Games draws to the HDMI output with kernel modesetting (DRM dumb buffers, no GPU and
no desktop), from the emulator thread, page-flipping between two buffers. The built-in LCD is a
separate DRM card, so ZeroShell keeps drawing there. While Games is open it writes its process id
to `$XDG_RUNTIME_DIR/zeroos-hdmi-app`, which tells ZeroShell not to open Dock over the game.

**To confirm on a device:** the picture on a real TV (720p and 1080p-only monitors), how much CPU
the scaling takes at 60 frames a second, and that the console comes back on the TV after a game.

## Picture and borders

The picture is scaled to fit the screen, keeping its shape. A Game Boy picture (160 x 144) is
shown at its real size, because that looks sharpest. Bigger pictures (NES, Super Nintendo, Mega
Drive, PlayStation) are scaled down. **Picture: Smooth** (the default) blends neighbouring pixels
as it shrinks them, so thin lines and small text stay readable; **Picture: Sharp** keeps hard
pixel edges but drops some rows and columns.

The space around the picture has a **Border**. Press **Esc** and Enter on **Border** to change it:

| Border | |
| --- | --- |
| Frame (the default) | A case in the console's colours around a recessed screen: a power light and speaker slots on the handhelds, trim lines on the others |
| Glow | The colours at the edges of the picture, spread out and dimmed into the bars, like a TV with ambient light. It follows the game |
| Image | A border picture of your own (below). Without one, the Frame is shown |
| Off | Black |

**Border pictures.** Put PNG pictures in `~/Games/borders`. For each game, Games uses the first
it finds of:

1. `<game>.png`, named like the game file without its extension (`Tetris (World).png`)
2. `<console>.png`, using the folder name from the table above (`gb.png`, `nes.png`, `snes.png`...)
3. `default.png`

A picture with a see-through window in the middle is scaled so that the window sits on the game's
picture. That's how ready-made bezels and borders work, so they can be used as they are: the
console bezels from libretro's
[overlay-borders](https://github.com/libretro/overlay-borders) collection (1920 x 1080, made for
a 4:3 picture), or a Super Game Boy border (256 x 224, with a 160 x 144 window). The screen is
wider and shorter than a TV, so the top and bottom of a Super Game Boy border are cut off. A
picture without a window fills the whole screen, behind the game. Any size up to 4096 x 4096
works, and see-through parts are drawn black. Pick a bezel made for the console's own picture
shape: a 4:3 TV bezel around a Game Boy picture leaves black gaps beside it.

Border, picture, carry-on and TV settings are kept in `~/Games/settings` for every game.
