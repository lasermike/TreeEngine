#pragma once

#include <cmath>
#include <vector>

// Shared level-of-detail selection. An object's level comes from its distance to the camera against its own
// thresholds, all scaled by one global setting (RenderData::lodDistanceScale, the debug UI's "LOD distance scale").
// Level 0 is full detail; each threshold passed adds one. Objects that remember their last level can use hysteresis,
// so one hovering near a threshold doesn't switch back and forth every frame.
namespace Lod
{
    // How a tree drops detail at levels 2 and up (beyond its second distance): about half its leaves per level.
    // Merge replaces groups of sibling leaves (and, from level 3, pairs of sibling twigs) with one at their average
    // position; prune keeps a fixed subset of leaves (and drops the finest twigs). Kept leaves grow to keep leaf area.
    enum class Reduction { Prune, Merge };

    struct Thresholds
    {
        std::vector<float> distances = { 10.0f };  // Ascending, positive: level i while closer than distances[i].
        float hysteresis = 0;                       // Fraction of a threshold to pass before leaving a level (0 to 0.5).
        Reduction reduction = Reduction::Prune;     // Trees only.

        bool operator==(const Thresholds& other) const
        {
            return distances == other.distances && hysteresis == other.hysteresis && reduction == other.reduction;
        }
        bool operator!=(const Thresholds& other) const { return !(*this == other); }
    };

    // The level at this distance: how many (scaled) thresholds it has passed. With a previous level (>= 0), the
    // object keeps it while within that level's band widened by the hysteresis.
    inline int Select(float distance, const Thresholds& thresholds, float scale = 1.0f, int previousLevel = -1)
    {
        const int levels = static_cast<int>(thresholds.distances.size());
        int level = 0;
        while (level < levels && distance >= thresholds.distances[level] * scale)
            ++level;
        if (previousLevel < 0 || previousLevel > levels || previousLevel == level || thresholds.hysteresis <= 0)
            return level;
        const float nearEdge = previousLevel > 0 ? thresholds.distances[previousLevel - 1] * scale * (1.0f - thresholds.hysteresis) : 0.0f;
        const float farEdge = previousLevel < levels ? thresholds.distances[previousLevel] * scale * (1.0f + thresholds.hysteresis)
                                                     : distance + 1.0f;
        return distance >= nearEdge && distance < farEdge ? previousLevel : level;
    }

    // Whether thresholds are usable: 1 to 8 ascending, positive, finite distances; hysteresis in [0, 0.5].
    inline bool Valid(const Thresholds& thresholds)
    {
        if (thresholds.distances.empty() || thresholds.distances.size() > 8)
            return false;
        float previous = 0;
        for (const float distance : thresholds.distances)
        {
            if (!std::isfinite(distance) || !(distance > previous))
                return false;
            previous = distance;
        }
        return thresholds.hysteresis >= 0 && thresholds.hysteresis <= 0.5f;
    }
}
