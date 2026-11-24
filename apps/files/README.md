# Files

Browse the files on the Cardputer, starting in your home folder, and rename, copy, move and delete
them.

A folder is a list: typing filters it, 1 to 9 open the first nine rows, and the bar along the bottom
says what the keys do.

| Key | Does |
| --- | --- |
| Up / Down | Move |
| Enter | Open a folder, or open a file (below) |
| a-z | Filter the folder by name |
| Backspace | Take a letter off the filter; with none, up one folder |
| Esc | Clear the filter; else up one folder; home when you're in your home folder |
| Tab | The menu, below. Ctrl + a letter runs that action straight away |

| Menu | Does |
| --- | --- |
| E Rename | Rename the file or folder in focus |
| C Copy, M Move | Mark it; then open another folder and choose **V Paste** there |
| V Paste | Copy or move what you marked into this folder |
| S Share to a device | Send the file to one of your devices with Taildrop (needs Remote) |
| N New folder | Make a folder here |
| H Show hidden files | Show or hide names starting with `.` |
| D Delete | Delete it (a folder with everything in it), after asking |

Folders come first, then files, by name, with each file's size. Hidden files (names starting with
`.`) aren't shown unless you ask. The top line shows where you are, with `~` for your home folder.

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
Taildrop. Copying, moving and deleting use `std::filesystem`; a move to another card or drive
copies, then deletes the original. Names are typed on the kit's prompt (`apps/kit/ui.h`).
A file counts as text if its first 64 KB are valid UTF-8 with no NUL bytes.

## Limits

- Copying a large folder holds the screen until it's done; there's no progress bar yet.
