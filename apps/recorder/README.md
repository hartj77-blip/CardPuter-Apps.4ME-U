# Recorder

Voice notes from the Cardputer's microphone. They used to be a tab in Media;
they're an app of their own now, and the notes you already have are still in
`~/Recordings`.

Notes are 16 kHz mono WAV files, named by when you recorded them
(`note-20261123-101500.wav`) until you rename one. **Sync** can keep the
Recordings folder the same on your PC and phone.

| Key | Does |
| --- | --- |
| R | Record / stop. The red dot blinks while it records |
| Enter | Play the note; Enter again pauses it |
| Space or Fn + Q | Pause / resume |
| V | Stop playing |
| E | Rename the note |
| X or Del | Delete it (asks first) |
| + / - | Volume |
| Tab | The menu, with all of these |

Recording stops any music that is playing. Notes play through the same queue
as Media's music, so a note keeps playing after you leave the app.

The level meter is for the look: it moves while you record or play, but it
isn't the sound itself.

Needs sox (`rec` records, `play` plays), which the package installs.
