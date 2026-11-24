# Compass

A compass and a spirit level, from the Cardputer Zero's motion sensors. The standard model has
them; the **Lite has none**, and the app says "No IMU".

- **The dial** turns so that N points to magnetic north; the needle points the way the top of the
  Cardputer faces. The heading is shown in degrees and as a compass point (N, NE, E...).
- **The level** is the small ring: the bubble moves as you tilt the device, and turns green when
  it's within 1° of level both ways. Pitch and roll are shown in degrees beside it.

There are no keys besides **Esc** (home); the bar along the bottom says so.

Hold it flat for the compass: the heading is magnetic, not true north, and it's thrown off by
magnets, speakers and steel nearby (a desk with a metal frame, a laptop).

## How it works

The heading and the tilt come from the BMI270 (accelerometer and gyroscope) and BMM150
(magnetometer) through the OS's hardware layer (`hal().imu()`), read ten times a second.

## To confirm on hardware

- That the sensors' axes are the right way round: north on the dial should match a real compass,
  and tilting the top of the device down should move the bubble the right way.
- Whether the magnetometer needs calibrating (a figure-of-eight) to be useful. There is no
  calibration step yet.
