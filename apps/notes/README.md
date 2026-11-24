# Notes

A plain-text notepad that saves as you type.

Open **Notes** and type. Your note is `~/Notes/notes.txt`. It's saved three seconds after you
stop typing, and again when you leave, so a crash or a flat battery loses at most a few seconds.
The top line shows the file's name, the word count, and `*` while there are changes not yet
saved.

| Key | Does |
| --- | --- |
| Arrow keys | Move the cursor |
| Enter | New line |
| Backspace / Del | Delete |
| Esc | Save and go home |

**Files** opens any text file here: Enter on a text file under 64 KB, and Notes edits that file
instead of your note (keeping its permissions). Files only opens what reads as text, so an image
or archive is never saved back as text.

**Sync** shares the Notes folder with your PC and phone, so `notes.txt` is the same everywhere.

If saving fails (a full or read-only card), Notes says so once and keeps trying; your text stays
on screen.

## How it works

`notes.cpp` is an LVGL text area. Saving writes a temporary file and renames it over the note
(`write_file_atomic()` from the OS), so the file on the card is always either the old note or the
new one, never half of each. Older ZeroOS versions kept the note in `~/notes.txt`; the first time
Notes opens, it moves it into `~/Notes`.

## Limits

- One note, plus whatever Files opens. More notes are more files in `~/Notes`, opened from Files.
- Files over 64 KB don't open, to keep the editor quick on this screen.
