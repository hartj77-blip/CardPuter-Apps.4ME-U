# System

What the Cardputer is doing: CPU, memory, storage, temperature, and the versions it runs. Its id
is `sysmon`.

Two pages. **Left**, **Right** or **Tab** switch between them; **Esc** goes home. Both refresh
every second.

## Overview

| Left | Right |
| --- | --- |
| CPU busy, in percent, with a bar | Model: Standard or Lite |
| CPU clock, in MHz | ZeroOS version |
| Memory used of total, with a bar | Host name |
| Storage used of total (the SD card), with a bar | IP address, or "offline" |
| CPU temperature | Uptime, and the Linux kernel version |

## Memory

Where the 512 MB goes, which is the thing to watch on this device.

| Row | What it means |
| --- | --- |
| RAM used | Used of total, with a bar |
| Available | What programs can still get without anything being pushed out |
| File cache | Files Linux keeps in memory to read them faster; it gives this back when needed |
| zram | Compressed memory: "18 in 7 MB" holds 18 MB of data in 7 MB of RAM. The label shows the ratio |
| SD swap | Swap on the SD card, used only when zram is full |
| This UI | What ZeroShell itself, with the open app, is using |
| Biggest processes | The five programs using the most memory |

"simulated" in the corner means the numbers are made up: you're in the simulator.

## How it works

Everything comes from the OS's hardware layer (`hal().system()`), which reads `/proc` and `/sys`.
How ZeroOS manages memory (zram, swap, earlyoom) is in the OS README's
[Memory, CPU and battery](https://github.com/hartj77-blip/CardPuter-OS.4ME-U#memory-cpu-and-battery).
