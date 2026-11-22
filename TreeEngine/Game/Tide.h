#pragma once

// Tide prediction from harmonic constituents, as NOAA CO-OPS publishes them for a station (amplitude, and phase in
// degrees on Greenwich time, "phase_GMT"): height = mean + sum of f * amplitude * cos(V + u - phase), where V is the
// constituent's astronomical argument at the time (from Doodson numbers and the mean longitudes of the moon, sun,
// lunar perigee and node) and f, u its nodal corrections over the 18.6-year lunar node cycle.
//
// Checked against NOAA's predictions for Budd Inlet (9446807) with its 37 published constituents: within 2.6 cm RMS
// in June 2026 and 5.2 cm in January 2028, on a 4.4 m tidal range. L2's perigee-dependent nodal correction is
// approximated by M2's, and M1 isn't supported.

#include <string>
#include <vector>

namespace Tide
{
    struct Constituent
    {
        std::string name;       // NOAA's name: M2, S2, N2, K1, O1, ...
        double amplitude = 0;   // Meters.
        double phase = 0;       // Degrees, Greenwich epoch (NOAA's phase_GMT).
    };

    // Whether a constituent name is supported.
    bool Supported(const std::string& name);
    // The tide's height above its mean at a UTC time, in seconds since 1970-01-01.
    double Height(const std::vector<Constituent>& constituents, double unixSeconds);
    // Seconds since 1970-01-01 for exactly "YYYY-MM-DDTHH:MM:SSZ" (UTC); false if it isn't that form or isn't a real
    // calendar date and time (February 29 only in leap years; no leap seconds).
    bool ParseUtc(const std::string& text, double& unixSeconds);
}
