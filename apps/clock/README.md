# Clock

The time and date, and a stopwatch with laps.

The time comes from the Cardputer Zero's battery-backed clock chip (RX8130CE), so it is right even
with no network; when the device is online, the system keeps it in step with internet time. The
time zone is the system's (`sudo timedatectl set-timezone Europe/London`, or Raspberry Pi Imager's
settings before the first boot).

The keys are in the soft-key bar along the bottom (a Control screen: letters are shortcuts), and
in the menu on **Tab**:

| Key | Does |
| --- | --- |
| Space or Enter | Start / stop the stopwatch |
| L | Lap: shows the time since the last lap, with the one before it underneath |
| R | Reset the stopwatch and its laps |
| Esc | Home |

The stopwatch shows minutes, seconds and tenths. It runs only while the app is open: leaving the
app resets it.

## How it works

`clock.cpp`, about 120 lines: the time is read with `localtime_r()` ten times a second, the
stopwatch counts LVGL's millisecond tick. It needs nothing installed.
