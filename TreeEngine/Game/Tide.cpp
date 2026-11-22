#include "pch.h"
#include "Tide.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace
{
    constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;

    // Doodson numbers (tau, s, h, p, N', p1) and a phase offset in degrees.
    struct Doodson { int tau, s, h, p, n, p1; double offset; };

    const std::map<std::string, Doodson>& Basic()
    {
        static const std::map<std::string, Doodson> table = {
            { "M2", { 2, 0, 0, 0, 0, 0, 0 } }, { "S2", { 2, 2, -2, 0, 0, 0, 0 } }, { "N2", { 2, -1, 0, 1, 0, 0, 0 } },
            { "K2", { 2, 2, 0, 0, 0, 0, 0 } }, { "K1", { 1, 1, 0, 0, 0, 0, 90 } }, { "O1", { 1, -1, 0, 0, 0, 0, -90 } },
            { "P1", { 1, 1, -2, 0, 0, 0, -90 } }, { "Q1", { 1, -2, 0, 1, 0, 0, -90 } }, { "2N2", { 2, -2, 0, 2, 0, 0, 0 } },
            { "NU2", { 2, -1, 2, -1, 0, 0, 0 } }, { "MU2", { 2, -2, 2, 0, 0, 0, 0 } }, { "L2", { 2, 1, 0, -1, 0, 0, 180 } },
            { "LAM2", { 2, 1, -2, 1, 0, 0, 180 } }, { "T2", { 2, 2, -3, 0, 0, 1, 0 } }, { "R2", { 2, 2, -1, 0, 0, -1, 180 } },
            { "J1", { 1, 2, 0, -1, 0, 0, 90 } }, { "OO1", { 1, 3, 0, 0, 0, 0, 90 } }, { "2Q1", { 1, -3, 0, 2, 0, 0, -90 } },
            { "RHO", { 1, -3, 2, 1, 0, 0, -90 } }, { "S1", { 1, 1, -1, 0, 0, 0, 180 } }, { "SA", { 0, 0, 1, 0, 0, 0, 0 } },
            { "SSA", { 0, 0, 2, 0, 0, 0, 0 } }, { "MM", { 0, 1, 0, -1, 0, 0, 0 } }, { "MF", { 0, 2, 0, 0, 0, 0, 0 } },
            { "M3", { 3, 0, 0, 0, 0, 0, 180 } }, { "S4", { 4, 4, -4, 0, 0, 0, 0 } }, { "S6", { 6, 6, -6, 0, 0, 0, 0 } },
        };
        return table;
    }

    // Shallow-water and compound constituents: sums of basic ones.
    const std::map<std::string, std::map<std::string, int>>& Compound()
    {
        static const std::map<std::string, std::map<std::string, int>> table = {
            { "M4", { { "M2", 2 } } }, { "M6", { { "M2", 3 } } }, { "M8", { { "M2", 4 } } },
            { "MS4", { { "M2", 1 }, { "S2", 1 } } }, { "MN4", { { "M2", 1 }, { "N2", 1 } } },
            { "MK3", { { "M2", 1 }, { "K1", 1 } } }, { "2MK3", { { "M2", 2 }, { "K1", -1 } } },
            { "2SM2", { { "S2", 2 }, { "M2", -1 } } },
        };
        return table;
    }

    struct Astronomy { double tau, s, h, p, n, p1; };

    // Mean longitudes in degrees (Meeus), and tau, the mean lunar time, at a UTC time.
    Astronomy At(double unixSeconds)
    {
        const double julianDay = unixSeconds / 86400.0 + 2440587.5;
        const double t = (julianDay - 2451545.0) / 36525.0;
        Astronomy a;
        a.s = 218.3165 + 481267.8813 * t;
        a.h = 280.4661 + 36000.7698 * t;
        a.p = 83.3535 + 4069.0137 * t;
        a.n = 125.0445 - 1934.1363 * t;
        a.p1 = 282.9384 + 1.7195 * t;
        const double secondsOfDay = unixSeconds - 86400.0 * std::floor(unixSeconds / 86400.0);
        a.tau = 15.0 * secondsOfDay / 3600.0 + a.h - a.s;
        return a;
    }

    // Nodal corrections f (amplitude factor) and u (degrees), from the lunar node's longitude N (Schureman's
    // approximations).
    void Nodal(const std::string& name, double nodeDegrees, double& f, double& u)
    {
        const double n = nodeDegrees * DegreesToRadians;
        const double c1 = std::cos(n), c2 = std::cos(2 * n), c3 = std::cos(3 * n);
        const double s1 = std::sin(n), s2 = std::sin(2 * n), s3 = std::sin(3 * n);
        f = 1;
        u = 0;
        if (name == "M2" || name == "N2" || name == "2N2" || name == "NU2" || name == "MU2" || name == "L2" || name == "LAM2"
            || name == "M3")
        {
            f = 1.0004 - 0.0373 * c1 + 0.0002 * c2;
            u = -2.14 * s1;
            if (name == "M3") { f = std::pow(f, 1.5); u *= 1.5; }
        }
        else if (name == "K1") { f = 1.0060 + 0.1150 * c1 - 0.0088 * c2 + 0.0006 * c3; u = -8.86 * s1 + 0.68 * s2 - 0.07 * s3; }
        else if (name == "O1" || name == "Q1" || name == "2Q1" || name == "RHO")
        { f = 1.0089 + 0.1871 * c1 - 0.0147 * c2 + 0.0014 * c3; u = 10.80 * s1 - 1.34 * s2 + 0.19 * s3; }
        else if (name == "K2") { f = 1.0241 + 0.2863 * c1 + 0.0083 * c2 - 0.0015 * c3; u = -17.74 * s1 + 0.68 * s2 - 0.04 * s3; }
        else if (name == "J1") { f = 1.1029 + 0.1676 * c1 - 0.0170 * c2 + 0.0016 * c3; u = -12.94 * s1 + 1.34 * s2 - 0.19 * s3; }
        else if (name == "OO1") { f = 1.1027 + 0.6504 * c1 + 0.0317 * c2 - 0.0014 * c3; u = -36.68 * s1 + 4.02 * s2 - 0.57 * s3; }
        else if (name == "MF") { f = 1.043 + 0.414 * c1; u = -23.74 * s1 + 2.68 * s2 - 0.38 * s3; }
        else if (name == "MM") { f = 1.000 - 0.130 * c1; }
        else
        {
            const auto compound = Compound().find(name);
            if (compound == Compound().end())
                return;
            for (const auto& part : compound->second)
            {
                double fk, uk;
                Nodal(part.first, nodeDegrees, fk, uk);
                f *= std::pow(fk, std::abs(part.second));
                u += part.second * uk;
            }
        }
    }

    double Argument(const std::string& name, const Astronomy& a)
    {
        const auto basic = Basic().find(name);
        if (basic != Basic().end())
        {
            const Doodson& d = basic->second;
            return d.tau * a.tau + d.s * a.s + d.h * a.h + d.p * a.p - d.n * a.n + d.p1 * a.p1 + d.offset;
        }
        double sum = 0;
        for (const auto& part : Compound().at(name))
            sum += part.second * Argument(part.first, a);
        return sum;
    }
}

namespace Tide
{
    bool Supported(const std::string& name)
    {
        return Basic().count(name) || Compound().count(name);
    }

    double Height(const std::vector<Constituent>& constituents, double unixSeconds)
    {
        const Astronomy a = At(unixSeconds);
        double height = 0;
        for (const Constituent& c : constituents)
        {
            if (!Supported(c.name))
                continue;
            double f, u;
            Nodal(c.name, a.n, f, u);
            height += f * c.amplitude * std::cos((Argument(c.name, a) + u - c.phase) * DegreesToRadians);
        }
        return height;
    }

    bool ParseUtc(const std::string& text, double& unixSeconds)
    {
        int year, month, day, hour, minute, second;
        char t, z;
        if (sscanf_s(text.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d%c", &year, &month, &day, &t, 1, &hour, &minute, &second, &z, 1) != 8
            || t != 'T' || z != 'Z' || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
            return false;
        // Days since 1970-01-01 in the proleptic Gregorian calendar (Howard Hinnant's days_from_civil).
        const int y = year - (month <= 2 ? 1 : 0);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const double days = era * 146097.0 + doe - 719468.0;
        unixSeconds = days * 86400.0 + hour * 3600.0 + minute * 60.0 + second;
        return true;
    }
}
