// Words and numbers for the screen: text the fonts can draw, ages,
// distances, bearings.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zs::mesh {

// The Cardputer's fonts have Latin letters and a few symbols, no emoji. Text
// from the mesh is full of them, so the common ones become words in colons
// (":thumbsup:") or the usual keyboard faces (":)"), and what's left that
// the font can't draw becomes "?". Control characters go.
std::string display_text(std::string_view utf8);
// A reaction's emoji, short enough to sit under a message: "+1", "<3", ":D".
std::string reaction_text(std::string_view utf8);

// At most max_bytes, never ending inside a UTF-8 character.
std::string utf8_cut(std::string_view s, size_t max_bytes);
// One line: newlines become spaces, and it's cut to max_bytes with "...".
std::string one_line(std::string_view s, size_t max_bytes);

// "now", "5m", "3h", "2d", "4w"; "" if then is 0 or in the future by more than a minute.
std::string age_text(long now, long then);
// "14:05" today, "Mon 14:05" this week, "3 Nov" before.
std::string clock_text(long now, long then);
// "850 m", "3.2 km", "120 km"; with miles, "0.5 mi".
std::string distance_text(double metres, bool miles = false);
// "1h 5m", "3d 2h".
std::string duration_text(long seconds);

double distance_m(double lat1, double lon1, double lat2, double lon2);
// Degrees clockwise from north, 0 to 360.
double bearing_deg(double lat1, double lon1, double lat2, double lon2);
// "N", "NE", ...
const char* compass_point(double degrees);

// SNR in words for a LoRa link: "Good", "Fair", "Weak".
const char* snr_quality(float snr_db);
// 0 to 4 bars.
int snr_bars(float snr_db);

}  // namespace zs::mesh
