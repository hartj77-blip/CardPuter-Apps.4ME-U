# Files

Browse the files on the Cardputer, starting in your home folder.

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Open a folder, or open a file (below) |
| Backspace | Up one folder |
| Esc | Up one folder; home when you're in your home folder |
| S | Send the file to one of your devices with Taildrop (needs Remote) |

Folders come first, then files, by name, with each file's size. Hidden files (names starting with
`.`) aren't shown. The top line shows where you are, with `~` for your home folder.

What Enter does with a file:

| File | What happens |
| --- | --- |
| Music: `.mp3 .flac .ogg .wav .m4a .opus` | Plays it |
| Video: `.mp4 .mkv .webm .mov .avi .m4v` | Plays it on the HDMI monitor, in Dock mode |
| Text under 64 KB | Opens it in Notes, to read or edit (needs Notes) |
| Anything else | Says why it can't open it: not text, too large, or not a file (a device or pipe) |

Files never opens a device, pipe or socket (`/dev/zero`, a FIFO): reading one could freeze the
screen.

## How it works

`files.cpp` lists folders with `opendir()` and opens files by handing them to another app:
`shell_open_app("notes", path)` for text, the OS's player for music, Dock for video, Remote for
Taildrop. A file counts as text if its first 64 KB are valid UTF-8 with no NUL bytes.

## Limits

- It browses and opens. Copying, moving, renaming and deleting are for the Terminal for now, or
  the file manager in Dock mode.
