#include "pch.h"
#include "RoadGrading.h"

#include <algorithm>
#include <cmath>
#include <limits>

using DirectX::XMFLOAT2;

namespace
{
    // What one line wants of one grid sample.
    struct Claim
    {
        float score = std::numeric_limits<float>::max();    // Smaller wins.
        float height = 0;
        bool ditch = false;
    };

    struct Profile
    {
        float halfWidth, shoulder, crown, ditchDepth, ditchWidth;
        int rank;       // 0 paved road, 1 gravel road, 2 driveway: lower shows where traveled ways overlap.
    };

    Profile ProfileOf(const Roads::Line& line)
    {
        const bool road = line.kind == Roads::Kind::Road;
        const bool gravel = line.surface == "gravel";
        Profile p;
        p.halfWidth = line.width / 2;
        p.shoulder = road ? line.shoulder : 0;
        p.crown = !road ? Roads::DrivewayCrown : gravel ? Roads::GravelCrown : Roads::PavedCrown;
        p.ditchDepth = road ? line.ditchDepth : 0;
        p.ditchWidth = road ? line.ditchWidth : 0;
        p.rank = !road ? 2 : gravel ? 1 : 0;
        return p;
    }

    // The graded height `lateral` meters from the centerline, for a bed at `bed` over `ground`; false past the
    // line's reach (the ground stays).
    bool CrossSection(const Profile& p, float bed, float lateral, float ground, float& height, bool& ditch)
    {
        const float d = std::abs(lateral);
        ditch = false;
        if (d <= p.halfWidth)
        {
            height = bed - p.crown * d;
            return true;
        }
        const float edge = bed - p.crown * p.halfWidth;
        if (d <= p.halfWidth + p.shoulder)
        {
            height = edge - Roads::ShoulderSlope * (d - p.halfWidth);
            return true;
        }
        const float outer = edge - Roads::ShoulderSlope * p.shoulder;
        const float q = d - p.halfWidth - p.shoulder;
        if (p.ditchDepth > 0 && p.ditchWidth > 0)
        {
            // Foreslope down to the ditch's bottom. Then, where the ground is above the bottom (a cut), a backslope
            // rising to it at the ditch's outer edge; where it is below (a fill on a side slope), an embankment
            // falling at EmbankmentSlope until it meets the ground. Where the ground crosses the bottom both give
            // the bottom, so the section is continuous, and the embankment is never steeper than its slope.
            const float half = p.ditchWidth / 2;
            const float bottom = outer - p.ditchDepth;
            if (q <= half)
                height = outer - p.ditchDepth * (q / half);
            else if (ground >= bottom)
            {
                if (q > p.ditchWidth)
                    return false;
                height = bottom + (ground - bottom) * ((q - half) / half);
            }
            else
            {
                const float out = q - half;
                const float embankment = bottom - Roads::EmbankmentSlope * out;
                if (out > Roads::MaxEmbankment || embankment <= ground)
                    return false;
                // Ground falling faster than the embankment never meets it: over the cap's outer half the fill
                // tapers to nothing, so it reaches the ground by MaxEmbankment.
                const float taperStart = Roads::MaxEmbankment / 2;
                const float taper = out <= taperStart ? 0 : (out - taperStart) / (Roads::MaxEmbankment - taperStart);
                height = embankment + (ground - embankment) * taper;
            }
            ditch = height < ground;
            return true;
        }
        if (q > Roads::BlendWidth)
            return false;
        const float t = q / Roads::BlendWidth, smooth = t * t * (3 - 2 * t);
        height = outer + (ground - outer) * smooth;
        return true;
    }

    // Running median then running mean, windows shrinking at the ends.
    std::vector<float> SmoothAlong(const std::vector<float>& values)
    {
        const int n = static_cast<int>(values.size());
        std::vector<float> median(n), mean(n);
        for (int i = 0; i < n; ++i)
        {
            const int r = std::min({ Roads::MedianWindow / 2, i, n - 1 - i });
            std::vector<float> window(values.begin() + i - r, values.begin() + i + r + 1);
            std::nth_element(window.begin(), window.begin() + r, window.end());
            median[i] = window[r];
        }
        for (int i = 0; i < n; ++i)
        {
            const int r = std::min({ Roads::SmoothingWindow / 2, i, n - 1 - i });
            float sum = 0;
            for (int k = i - r; k <= i + r; ++k)
                sum += median[k];
            mean[i] = sum / (2 * r + 1);
        }
        return mean;
    }
}

namespace Roads
{
    GradeReport Grade(const Network& network, std::vector<float>& heights, int samples, float spacing)
    {
        GradeReport report;
        if (samples < 2 || heights.size() != size_t(samples) * samples || spacing <= 0)
            return report;
        const std::vector<float> ground = heights;
        const float half = (samples - 1) * spacing / 2;
        const auto groundAt = [&](float x, float z)
        {
            const float u = std::min(std::max((x + half) / spacing, 0.0f), float(samples - 1));
            const float v = std::min(std::max((z + half) / spacing, 0.0f), float(samples - 1));
            const int i = std::min(static_cast<int>(u), samples - 2), j = std::min(static_cast<int>(v), samples - 2);
            const float fu = u - i, fv = v - j;
            const auto h = [&](int a, int b) { return ground[size_t(j + b) * samples + i + a]; };
            return (h(0, 0) * (1 - fu) + h(1, 0) * fu) * (1 - fv) + (h(0, 1) * (1 - fu) + h(1, 1) * fu) * fv;
        };

        std::vector<Claim> claims(heights.size());
        for (const Line& line : network.lines)
        {
            const std::vector<CenterlineSample> centerline = SampleCenterline(line.points);
            if (centerline.size() < 2)
                continue;
            const Profile p = ProfileOf(line);
            std::vector<float> beds(centerline.size());
            for (size_t k = 0; k < centerline.size(); ++k)
                beds[k] = groundAt(centerline[k].point.x, centerline[k].point.y);
            beds = SmoothAlong(beds);
            const bool ditched = p.ditchDepth > 0 && p.ditchWidth > 0;

            // Each segment between consecutive samples grades the grid points that project onto it, with the bed
            // interpolated along it, so a grid finer than the samples follows a grade smoothly. A point past a
            // segment's start, at a joint, measures from the joint itself (a round join), which covers the outsides
            // of bends; past the first sample or the last, nothing is graded, so a road stops at its ends.
            // As far as either ditch shape goes: the backslope to the ditch's outer edge, or the embankment's cap.
            const float reach = p.halfWidth + p.shoulder
                + (ditched ? std::max(p.ditchWidth, p.ditchWidth / 2 + MaxEmbankment) : BlendWidth);
            for (size_t k = 0; k + 1 < centerline.size(); ++k)
            {
                const XMFLOAT2 a = centerline[k].point, b = centerline[k + 1].point;
                const float sx = b.x - a.x, sz = b.y - a.y, length2 = sx * sx + sz * sz;
                if (length2 < 1e-8f)
                    continue;
                const float length = std::sqrt(length2);
                const XMFLOAT2 side(-sz / length, sx / length);
                const int i0 = std::max(0, static_cast<int>(std::floor((std::min(a.x, b.x) - reach + half) / spacing)));
                const int i1 = std::min(samples - 1, static_cast<int>(std::ceil((std::max(a.x, b.x) + reach + half) / spacing)));
                const int j0 = std::max(0, static_cast<int>(std::floor((std::min(a.y, b.y) - reach + half) / spacing)));
                const int j1 = std::min(samples - 1, static_cast<int>(std::ceil((std::max(a.y, b.y) + reach + half) / spacing)));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const float dx = -half + i * spacing - a.x, dz = -half + j * spacing - a.y;
                        const float t = (dx * sx + dz * sz) / length2;
                        float lateral, bed;
                        if (t > 1)
                            continue;                   // The next segment's (or past the end).
                        if (t < 0)
                        {
                            if (k == 0)
                                continue;               // Before the start.
                            // Only the wedge on a bend's outside, which neither segment reaches: past the previous
                            // segment's end too. (Behind the line, that's still before the start.)
                            const XMFLOAT2 previous = centerline[k - 1].point;
                            if (dx * (a.x - previous.x) + dz * (a.y - previous.y) <= 0)
                                continue;
                            // The joint's wedge: distance from the joint, on this segment's side of the line.
                            const float sideways = dx * side.x + dz * side.y;
                            lateral = std::copysign(std::sqrt(dx * dx + dz * dz), sideways);
                            bed = beds[k];
                        }
                        else
                        {
                            lateral = dx * side.x + dz * side.y;
                            bed = beds[k] + (beds[k + 1] - beds[k]) * t;
                        }
                        const size_t index = size_t(j) * samples + i;
                        float height = 0;
                        bool ditch = false;
                        if (!CrossSection(p, bed, lateral, ground[index], height, ditch))
                            continue;
                        // On a traveled way: rank first, then nearness. Elsewhere: nearness, after every traveled way.
                        const bool traveled = std::abs(lateral) <= p.halfWidth;
                        const float score = traveled ? p.rank * 100.0f + std::abs(lateral) : 1000.0f + std::abs(lateral);
                        Claim& claim = claims[index];
                        if (score < claim.score)
                            claim = { score, height, ditch };
                    }
            }
        }

        for (size_t index = 0; index < heights.size(); ++index)
        {
            const Claim& claim = claims[index];
            if (claim.score == std::numeric_limits<float>::max() || std::abs(claim.height - heights[index]) < 1e-4f)
                continue;
            const float change = claim.height - heights[index];
            report.deepestCut = std::max(report.deepestCut, -change);
            report.highestFill = std::max(report.highestFill, change);
            heights[index] = claim.height;
            ++report.graded;
            report.ditched += claim.ditch ? 1 : 0;
        }
        return report;
    }
}
