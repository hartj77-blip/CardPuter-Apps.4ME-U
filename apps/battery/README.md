# Battery

How much charge is left, how long it will last, what the Cardputer is drawing, and how healthy the
battery is. It's also where you switch the power profile.

| Shown | What it is |
| --- | --- |
| The ring | Charge in percent. Green while charging, red below 15%, amber below 30% |
| Charging / Plugged in / On battery | Where the power comes from |
| Time left | Until empty on battery, or until full while charging |
| Power draw | The average power the device is using, in watts |
| Voltage, Current | Straight from the fuel gauge |
| Temperature | The battery's |
| Health | Capacity now compared with new, with the full charge in mAh when the gauge reports it |
| Cycles | Charge cycles so far |

| Key | Does |
| --- | --- |
| P or Tab | Next power profile: Saver, Balanced, Performance |
| Esc | Home |

The profile sets how fast the CPU may run. When the battery is at 15% or less and not charging,
ZeroOS uses Saver whatever you picked, until it's charged, and the app shows "(low)" after the profile's name. What
each profile does, and measured figures as they come in, are in the OS's
[performance guide](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/blob/main/docs/PERFORMANCE.md).

The readings refresh every second. Values the gauge doesn't give show as `--`.

## How it works

The readings come from the BQ27220 fuel gauge through the OS's hardware layer (`hal().battery()`);
time left, the average draw and the profiles come from ZeroShell's power manager (`ui/power.h`).
The app only shows them. In the simulator the battery is simulated.

## To confirm on hardware

- That time left is close to the truth, on battery and while charging.
- That Health and Cycles read sensibly on a new battery.
