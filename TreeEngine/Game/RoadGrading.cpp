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
            // Foreslope down to the ditch's bottom (on a fill, an embankment), backslope up to the ground, which the
            // backslope never raises.
            const float half = p.ditchWidth / 2;
            if (q > p.ditchWidth)
                return false;
            const float bottom = outer - p.ditchDepth;
            const float shaped = q <= half ? outer - p.ditchDepth * (q / half) : bottom + (ground - bottom) * ((q - half) / half);
            height = q <= half ? shaped : std::min(shaped, ground);
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

            const float reach = p.halfWidth + p.shoulder + (p.ditchDepth > 0 && p.ditchWidth > 0 ? p.ditchWidth : BlendWidth);
            for (size_t k = 0; k < centerline.size(); ++k)
            {
                const CenterlineSample& c = centerline[k];
                const XMFLOAT2 forward(c.side.y, -c.side.x);
                const int i0 = std::max(0, static_cast<int>(std::floor((c.point.x - reach + half) / spacing)));
                const int i1 = std::min(samples - 1, static_cast<int>(std::ceil((c.point.x + reach + half) / spacing)));
                const int j0 = std::max(0, static_cast<int>(std::floor((c.point.y - reach + half) / spacing)));
                const int j1 = std::min(samples - 1, static_cast<int>(std::ceil((c.point.y + reach + half) / spacing)));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const float dx = -half + i * spacing - c.point.x, dz = -half + j * spacing - c.point.y;
                        // This sample's stretch of the line: within half a sample spacing along it (a little more,
                        // so bends leave no gaps on their outsides).
                        const float along = dx * forward.x + dz * forward.y;
                        if (std::abs(along) > 0.75f * SampleSpacing)
                            continue;
                        const float lateral = dx * c.side.x + dz * c.side.y;
                        const size_t index = size_t(j) * samples + i;
                        float height = 0;
                        bool ditch = false;
                        if (!CrossSection(p, beds[k], lateral, ground[index], height, ditch))
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
