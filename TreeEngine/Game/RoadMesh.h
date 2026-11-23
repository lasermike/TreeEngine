#pragma once

// Road surface meshes from a roads file (RoadNetwork.h): ribbons along each centerline, draped on the ground a few
// centimeters up, and lane markings on county paved roads. A pure function of the network and a height lookup, so
// tests can build one on flat ground.

#include "RoadNetwork.h"
#include "GeometryGenerator.h"
#include <functional>

namespace Roads
{
    enum class Part { Asphalt, Gravel, YellowLine, WhiteLine, Count };

    struct MeshPart
    {
        std::vector<SimpleVertex> vertices;
        std::vector<UINT> indices;
    };

    struct Mesh
    {
        MeshPart parts[static_cast<int>(Part::Count)];
        MeshPart& operator[](Part part) { return parts[static_cast<int>(part)]; }
        const MeshPart& operator[](Part part) const { return parts[static_cast<int>(part)]; }
    };

    // Heights above the ground (meters), so that where surfaces overlap at a junction the more important one shows:
    // paved roads, then gravel roads, then driveways; markings above them all.
    constexpr float RoadLift = 0.10f, GravelRoadLift = 0.08f, DrivewayLift = 0.06f, MarkingLift = 0.12f;
    constexpr int SmoothingPasses = 2;          // Corner cutting on each centerline before sampling it.
    constexpr float SampleSpacing = 1.0f;       // Meters between cross-sections along a line.
    constexpr float MarkingWidth = 0.10f;
    constexpr float CenterDash = 3.0f, CenterGap = 9.0f;   // A dashed center line's dashes and gaps, meters.
    constexpr int EdgeLineSpeed = 40;           // County paved roads this fast (mph) and up get white edge lines.

    // `height(x, z, y)` gives the ground's height at world (x, z), false off the ground; `offset` moves the network's
    // package coordinates into the world. Surfaces use world-planar texture coordinates (u = x, v = -z, in meters), so
    // ribbons overlapping at a junction shade alike. Every triangle faces up.
    Mesh BuildMesh(const Network& network, DirectX::XMFLOAT3 offset, const std::function<bool(float, float, float&)>& height);
}
