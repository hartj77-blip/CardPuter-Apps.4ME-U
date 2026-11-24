# Clock

The time and date, and a stopwatch.

The time comes from the Cardputer Zero's battery-backed clock chip (RX8130CE), so it is right even
with no network; when the device is online, the system keeps it in step with internet time. The
time zone is the system's (`sudo timedatectl set-timezone Europe/London`, or Raspberry Pi Imager's
settings before the first boot).

| Key | Does |
| --- | --- |
| Space or Enter | Start / stop the stopwatch |
| R | Reset the stopwatch |
| Esc | Home |

The stopwatch shows minutes, seconds and tenths. It runs only while the app is open: leaving the
app resets it.

## How it works

`clock.cpp`, about 70 lines: the time is read with `localtime_r()` ten times a second, the
stopwatch counts LVGL's millisecond tick. It needs nothing installed.
