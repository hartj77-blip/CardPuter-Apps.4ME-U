// Is it light out? For Maps' automatic day and night colours: the sun's
// height above the horizon at a place and time (NOAA's formulas, good to a
// few minutes, which is plenty for this).
#pragma once

#include <cmath>
#include <ctime>

namespace zmap {

// Degrees above the horizon (negative: below), at lat/lon at UTC time t.
inline double sun_elevation(double lat, double lon, std::time_t t) {
    constexpr double kPi = 3.14159265358979323846, d2r = kPi / 180;
    double jd = double(t) / 86400.0 + 2440587.5;        // Julian day
    double jc = (jd - 2451545.0) / 36525.0;             // Julian century
    double l0 = std::fmod(280.46646 + jc * (36000.76983 + jc * 0.0003032), 360.0);
    double m = 357.52911 + jc * (35999.05029 - 0.0001537 * jc);
    double e = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);
    double c = std::sin(m * d2r) * (1.914602 - jc * (0.004817 + 0.000014 * jc)) + std::sin(2 * m * d2r) * (0.019993 - 0.000101 * jc) +
               std::sin(3 * m * d2r) * 0.000289;
    double app = l0 + c - 0.00569 - 0.00478 * std::sin((125.04 - 1934.136 * jc) * d2r);
    double eps = 23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0 +
                 0.00256 * std::cos((125.04 - 1934.136 * jc) * d2r);
    double decl = std::asin(std::sin(eps * d2r) * std::sin(app * d2r));
    double y = std::tan(eps * d2r / 2) * std::tan(eps * d2r / 2);
    double eqt = 4 / d2r *
                 (y * std::sin(2 * l0 * d2r) - 2 * e * std::sin(m * d2r) + 4 * e * y * std::sin(m * d2r) * std::cos(2 * l0 * d2r) -
                  0.5 * y * y * std::sin(4 * l0 * d2r) - 1.25 * e * e * std::sin(2 * m * d2r));   // minutes
    double minutes = std::fmod(double(t), 86400.0) / 60.0;                                          // of the UTC day
    double solar = std::fmod(minutes + eqt + 4 * lon + 1440.0, 1440.0);
    double hour_angle = solar / 4 - 180;
    double zen = std::acos(std::sin(lat * d2r) * std::sin(decl) + std::cos(lat * d2r) * std::cos(decl) * std::cos(hour_angle * d2r));
    return 90 - zen / d2r;
}

// Day colours from a little before sunrise to a little after sunset (the
// sun more than 3 degrees below the horizon is night).
inline bool sun_up(double lat, double lon, std::time_t t) { return sun_elevation(lat, lon, t) > -3.0; }

}  // namespace zmap
