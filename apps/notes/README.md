# Notes

Plain-text notes that save as you type.

Open **Notes** to see your notes, the files in `~/Notes`, newest first. It's a list: typing filters
it, 1 to 9 open the first nine, Enter opens the one in focus. A note opens in the editor; it's saved
three seconds after you stop typing, and again when you leave, so a crash or a flat battery loses
at most a few seconds. The top line shows the file's name, the word count, and `*` while there are
changes not yet saved.

On the list, **Tab** opens the menu (Ctrl + the letter runs an action straight away):

| Menu | Does |
| --- | --- |
| N New note | Type a name; `.txt` is added if it has no extension of its own |
| E Rename | Rename the note in focus |
| S Share to a device | Send it to one of your devices with Taildrop (needs Remote) |
| D Delete | Delete it, after asking |

In a note:

| Key | Does |
| --- | --- |
| Arrow keys | Move the cursor |
| Enter | New line |
| Backspace / Del | Delete |
| Tab > F | Find in note: the next place the text is, after the cursor (capitals don't count) |
| Tab > S | Share the note to a device |
| Esc | Save and go back to the list |

**Files** opens any text file here: Enter on a text file under 64 KB, and Notes edits that file
(keeping its permissions); Esc then goes back to Files. Files only opens what reads as text, so an
image or archive is never saved back as text.

**Sync** shares the Notes folder with your PC and phone, so your notes are the same everywhere.

If saving fails (a full or read-only card), Notes says so once and keeps trying; your text stays
on screen.

## How it works

`notes.cpp` is a list of `~/Notes` (the OS's `ListView`) and an LVGL text area. Saving writes a
temporary file and renames it over the note (`write_file_atomic()` from the OS), so the file on the
card is always either the old note or the new one, never half of each. Names are typed on the
kit's prompt (`apps/kit/ui.h`). Older ZeroOS versions kept the note in `~/notes.txt`; the first
time Notes opens, it moves it into `~/Notes`.

## Limits

- Files over 64 KB don't open, to keep the editor quick on this screen.
- Find leaves the editor for a moment to ask; the note is saved first.
