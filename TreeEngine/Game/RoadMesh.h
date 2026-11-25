#pragma once

// Road surface meshes from a roads file (RoadNetwork.h): ribbons along each centerline, draped on the ground a few
// centimeters up, and lane markings on county paved roads. A pure function of the network and a height lookup, so
// tests can build one on flat ground.

#include "RoadNetwork.h"
#include "GeometryGenerator.h"
#include <functional>

namespace Roads
{
    enum class Part { Asphalt, Gravel, YellowLine, WhiteLine, Trail, Count };

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
    // paved roads, then gravel roads, then driveways, then trails; markings above them all.
    constexpr float RoadLift = 0.10f, GravelRoadLift = 0.08f, DrivewayLift = 0.06f, TrailLift = 0.04f, MarkingLift = 0.12f;
    constexpr float MarkingWidth = 0.10f;
    constexpr float CenterDash = 3.0f, CenterGap = 9.0f;   // A dashed center line's dashes and gaps, meters.
    constexpr int EdgeLineSpeed = 40;           // County paved roads this fast (mph) and up get white edge lines.

    // `height(x, z, y)` gives the ground's height at world (x, z), false off the ground; `offset` moves the network's
    // package coordinates into the world. Surfaces use world-planar texture coordinates (u = x, v = -z, in meters), so
    // ribbons overlapping at a junction shade alike. Every triangle faces up. A cross-section with no ground height
    // under it is left out, and no triangle spans it: the strip ends there and starts again past it.
    // Trails (Kind::Trail) are left out when `trails` is false: a terrain builds them with TrailBuilder instead.
    Mesh BuildMesh(const Network& network, DirectX::XMFLOAT3 offset, const std::function<bool(float, float, float&)>& height,
        bool trails = true);

    // A square grid of ground heights as a terrain draws one level of it: `spacing` apart from the world x, z of its
    // node 0, 0, each cell split along the diagonal from its +x corner to its +z corner.
    struct GroundGrid
    {
        float spacing = 0;
        float originX = 0, originZ = 0;
    };

    // Trails laid on a terrain's own triangles. A strip of flat triangles would cut under a ridge between its rows,
    // and a terrain draws coarser levels further off, so a terrain builds its trails for each of its tiles and levels:
    // the cells of a grid inside a window, each of their triangles clipped to the trails' outlines, `TrailLift` above
    // its plane, leaving out what any road or driveway covers (they show over trails everywhere).
    class TrailBuilder
    {
    public:
        TrailBuilder(const Network& network, DirectX::XMFLOAT3 offset);
        bool Empty() const { return m_trails.empty(); }
        // The trails' outlines' world bounds in x and z.
        void Bounds(float& minX, float& minZ, float& maxX, float& maxZ) const;
        // The trails on cells [i0, i1] x [j0, j1] of the grid (cell i, j spans origin + i, j times spacing);
        // `height` gives the grid's height at its nodes (false off the ground: the cell is left out).
        MeshPart Build(const GroundGrid& grid, int i0, int j0, int i1, int j1,
            const std::function<bool(float, float, float&)>& height) const;

    private:
        struct Outline
        {
            std::vector<DirectX::XMFLOAT2> points;      // Convex, counterclockwise in (x, z).
            float minX, minZ, maxX, maxZ;
        };
        std::vector<Outline> m_trails, m_covers;
    };
}
