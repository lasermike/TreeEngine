#include "pch.h"
#include "RoadMesh.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;
using Sample = Roads::CenterlineSample;

namespace
{
    // A strip `columns` wide along samples[first, last]: across-positions given as offsets from the centerline
    // (meters, + to the left), the height `lift` over the ground. Triangles face up.
    void AddStrip(Roads::MeshPart& part, const std::vector<Sample>& samples, size_t first, size_t last,
        const std::vector<float>& across, float lift, bool planarUv, float dashStart,
        const std::function<bool(float, float, float&)>& height)
    {
        if (last <= first || across.size() < 2)
            return;
        const UINT columns = static_cast<UINT>(across.size());
        // Each row's vertices; a row with no ground under its centerline point is missing, and so is a column whose
        // own point has none (it takes the centerline's height).
        std::vector<SimpleVertex> rows;
        std::vector<bool> present;
        for (size_t i = first; i <= last; ++i)
        {
            const Sample& s = samples[i];
            float center = 0;
            const bool ground = height(s.point.x, s.point.y, center);
            present.push_back(ground);
            for (UINT c = 0; c < columns; ++c)
            {
                const float x = s.point.x + s.side.x * across[c], z = s.point.y + s.side.y * across[c];
                float y = center;
                if (ground && !height(x, z, y))
                    y = center;
                SimpleVertex v = {};
                v.Pos = XMFLOAT3(x, y + lift, z);
                v.Normal = XMFLOAT3(0, 1, 0);
                v.Tex = planarUv ? XMFLOAT2(x, -z) : XMFLOAT2(static_cast<float>(c) / (columns - 1), s.along - dashStart);
                v.TangentU = XMFLOAT4(1, 0, 0, 1);
                rows.push_back(v);
            }
        }
        const auto up = [&](UINT a, UINT b, UINT c)
        {
            const XMFLOAT3& pa = part.vertices[a].Pos;
            const XMFLOAT3& pb = part.vertices[b].Pos;
            const XMFLOAT3& pc = part.vertices[c].Pos;
            const float ux = pb.x - pa.x, uz = pb.z - pa.z, vx = pc.x - pa.x, vz = pc.z - pa.z;
            // cross(b - a, c - a).y > 0 faces up (as GeometryGenerator's grid does).
            if (uz * vx - ux * vz >= 0)
                part.indices.insert(part.indices.end(), { a, b, c });
            else
                part.indices.insert(part.indices.end(), { a, c, b });
        };
        // A strip over each run of two or more rows with ground.
        const size_t count = last - first + 1;
        for (size_t start = 0; start < count;)
        {
            if (!present[start])
            {
                ++start;
                continue;
            }
            size_t end = start;
            while (end + 1 < count && present[end + 1])
                ++end;
            if (end > start)
            {
                const UINT base = static_cast<UINT>(part.vertices.size());
                part.vertices.insert(part.vertices.end(), rows.begin() + start * columns, rows.begin() + (end + 1) * columns);
                for (size_t i = 0; i < end - start; ++i)
                    for (UINT c = 0; c + 1 < columns; ++c)
                    {
                        const UINT a = base + static_cast<UINT>(i) * columns + c, b = a + 1, d = a + columns, e = d + 1;
                        up(a, b, d);
                        up(d, b, e);
                    }
            }
            start = end + 1;
        }
    }

    // Smooth normals from the strip's triangles; the tangent follows +x projected onto each normal's plane.
    void ComputeNormals(Roads::MeshPart& part, size_t firstVertex, size_t firstIndex)
    {
        std::vector<XMFLOAT3> sums(part.vertices.size() - firstVertex, XMFLOAT3(0, 0, 0));
        for (size_t i = firstIndex; i + 2 < part.indices.size(); i += 3)
        {
            const UINT a = part.indices[i], b = part.indices[i + 1], c = part.indices[i + 2];
            const XMVECTOR pa = XMLoadFloat3(&part.vertices[a].Pos);
            const XMVECTOR n = XMVector3Cross(XMLoadFloat3(&part.vertices[b].Pos) - pa, XMLoadFloat3(&part.vertices[c].Pos) - pa);
            for (const UINT v : { a, b, c })
            {
                XMFLOAT3& sum = sums[v - firstVertex];
                XMStoreFloat3(&sum, XMLoadFloat3(&sum) + n);
            }
        }
        for (size_t v = firstVertex; v < part.vertices.size(); ++v)
        {
            XMVECTOR n = XMLoadFloat3(&sums[v - firstVertex]);
            if (XMVectorGetX(XMVector3LengthSq(n)) < 1e-12f)
                n = XMVectorSet(0, 1, 0, 0);
            n = XMVector3Normalize(n);
            if (XMVectorGetY(n) < 0)
                n = -n;
            XMStoreFloat3(&part.vertices[v].Normal, n);
            XMVECTOR t = XMVectorSet(1, 0, 0, 0);
            t = XMVector3Normalize(t - n * XMVector3Dot(t, n));
            XMFLOAT3 tangent;
            XMStoreFloat3(&tangent, t);
            part.vertices[v].TangentU = XMFLOAT4(tangent.x, tangent.y, tangent.z, 1);
        }
    }

    // A trail as the ground's own triangles clipped to its outline: for each quad between consecutive cross-sections
    // (left and right edges), each grid cell under it, and each of the cell's two triangles, the part of the triangle
    // inside the quad, at the triangle's plane plus `lift`. Pieces carry their plane's normal; texture coordinates are
    // world-planar like the other surfaces'.
    void AddTrail(Roads::MeshPart& part, const std::vector<Sample>& samples, float halfWidth, float lift,
        const Roads::GroundGrid& grid, const std::function<bool(float, float, float&)>& height)
    {
        typedef XMFLOAT2 P;
        const auto cross = [](const P& o, const P& a, const P& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
        // Keeps the part of a convex polygon on the inner side of edge a-b of a counterclockwise polygon.
        const auto clip = [&](const std::vector<P>& polygon, const P& a, const P& b)
        {
            std::vector<P> out;
            for (size_t i = 0; i < polygon.size(); ++i)
            {
                const P& p = polygon[i];
                const P& q = polygon[(i + 1) % polygon.size()];
                const float dp = cross(a, b, p), dq = cross(a, b, q);
                if (dp >= 0)
                    out.push_back(p);
                if ((dp >= 0) != (dq >= 0))
                {
                    const float t = dp / (dp - dq);
                    out.push_back(P(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t));
                }
            }
            return out;
        };
        const auto emit = [&](std::vector<P> polygon, const P& o, const P& u, const P& v, float h0, float hu, float hv)
        {
            // The terrain triangle's plane: h0 at o, hu along u, hv along v (u and v its two edges from o).
            const float det = u.x * v.y - u.y * v.x;
            const auto planeHeight = [&](const P& p)
            {
                const float dx = p.x - o.x, dz = p.y - o.y;
                const float a = (dx * v.y - dz * v.x) / det, b = (u.x * dz - u.y * dx) / det;
                return h0 + (hu - h0) * a + (hv - h0) * b + lift;
            };
            const UINT base = static_cast<UINT>(part.vertices.size());
            for (const P& p : polygon)
            {
                SimpleVertex vertex = {};
                vertex.Pos = XMFLOAT3(p.x, planeHeight(p), p.y);
                vertex.Normal = XMFLOAT3(0, 1, 0);
                vertex.Tex = XMFLOAT2(p.x, -p.y);
                vertex.TangentU = XMFLOAT4(1, 0, 0, 1);
                part.vertices.push_back(vertex);
            }
            for (UINT i = 1; i + 1 < polygon.size(); ++i)
            {
                // Counterclockwise in (x, z) is clockwise seen from above (+y): wind each so it faces up.
                const P& a = polygon[0];
                const P& b = polygon[i];
                const P& c = polygon[i + 1];
                if (cross(a, b, c) <= 0)
                    part.indices.insert(part.indices.end(), { base, base + i, base + i + 1 });
                else
                    part.indices.insert(part.indices.end(), { base, base + i + 1, base + i });
            }
        };
        for (size_t k = 0; k + 1 < samples.size(); ++k)
        {
            const Sample& s0 = samples[k];
            const Sample& s1 = samples[k + 1];
            std::vector<P> quad = {
                P(s0.point.x - s0.side.x * halfWidth, s0.point.y - s0.side.y * halfWidth),
                P(s1.point.x - s1.side.x * halfWidth, s1.point.y - s1.side.y * halfWidth),
                P(s1.point.x + s1.side.x * halfWidth, s1.point.y + s1.side.y * halfWidth),
                P(s0.point.x + s0.side.x * halfWidth, s0.point.y + s0.side.y * halfWidth) };
            // Counterclockwise in (x, z), and convex (a tight bend's quad can fold: then it's two triangles).
            float area = 0;
            for (size_t i = 0; i < 4; ++i)
                area += quad[i].x * quad[(i + 1) % 4].y - quad[(i + 1) % 4].x * quad[i].y;
            if (area < 0)
                std::reverse(quad.begin(), quad.end());
            bool convex = true;
            for (size_t i = 0; i < 4; ++i)
                convex = convex && cross(quad[i], quad[(i + 1) % 4], quad[(i + 2) % 4]) >= 0;
            std::vector<std::vector<P>> pieces;
            if (convex)
                pieces.push_back(quad);
            else
            {
                pieces.push_back({ quad[0], quad[1], quad[2] });
                pieces.push_back({ quad[0], quad[2], quad[3] });
                for (auto& piece : pieces)
                    if (cross(piece[0], piece[1], piece[2]) < 0)
                        std::reverse(piece.begin(), piece.end());
            }
            for (const auto& outline : pieces)
            {
                float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
                for (const P& p : outline)
                {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minZ = std::min(minZ, p.y); maxZ = std::max(maxZ, p.y);
                }
                const int i0 = static_cast<int>(std::floor((minX - grid.originX) / grid.spacing));
                const int i1 = static_cast<int>(std::floor((maxX - grid.originX) / grid.spacing));
                const int j0 = static_cast<int>(std::floor((minZ - grid.originZ) / grid.spacing));
                const int j1 = static_cast<int>(std::floor((maxZ - grid.originZ) / grid.spacing));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const float x0 = grid.originX + i * grid.spacing, z0 = grid.originZ + j * grid.spacing;
                        const float x1 = x0 + grid.spacing, z1 = z0 + grid.spacing;
                        float h00, h10, h01, h11;
                        if (!height(x0, z0, h00) || !height(x1, z0, h10) || !height(x0, z1, h01) || !height(x1, z1, h11))
                            continue;       // Off the ground.
                        // The cell's two triangles (split from its +x corner to its +z corner), counterclockwise.
                        const P c00(x0, z0), c10(x1, z0), c01(x0, z1), c11(x1, z1);
                        const std::vector<P> lower = { c00, c10, c01 }, upper = { c10, c11, c01 };
                        for (int t = 0; t < 2; ++t)
                        {
                            std::vector<P> polygon = t ? upper : lower;
                            for (size_t e = 0; e < outline.size() && polygon.size() >= 3; ++e)
                                polygon = clip(polygon, outline[e], outline[(e + 1) % outline.size()]);
                            if (polygon.size() < 3)
                                continue;
                            if (t == 0)
                                emit(polygon, c00, P(grid.spacing, 0), P(0, grid.spacing), h00, h10, h01);
                            else
                                emit(polygon, c11, P(-grid.spacing, 0), P(0, -grid.spacing), h11, h01, h10);
                        }
                    }
            }
        }
    }

    // Columns across a surface: edges, center and enough between that no two are more than about 1.5 m apart.
    std::vector<float> Across(float halfWidth)
    {
        const int segments = std::max(2, 2 * static_cast<int>(std::ceil(halfWidth / 1.5f)));
        std::vector<float> across;
        for (int c = 0; c <= segments; ++c)
            across.push_back(halfWidth - 2 * halfWidth * c / segments);
        return across;
    }
}

namespace Roads
{
    Mesh BuildMesh(const Network& network, XMFLOAT3 offset, const std::function<bool(float, float, float&)>& height,
        const GroundGrid* grid)
    {
        Mesh mesh;
        for (const Line& line : network.lines)
        {
            std::vector<Sample> samples = SampleCenterline(line.points);
            for (Sample& sample : samples)
                sample.point = XMFLOAT2(sample.point.x + offset.x, sample.point.y + offset.z);
            if (samples.size() < 2)
                continue;
            const bool road = line.kind == Kind::Road, trail = line.kind == Kind::Trail;
            const bool gravel = line.surface == "gravel";
            MeshPart& surface = mesh[trail ? Part::Trail : gravel ? Part::Gravel : Part::Asphalt];
            const float halfWidth = line.width / 2 + (road ? line.shoulder : 0);
            const size_t vertexStart = surface.vertices.size(), indexStart = surface.indices.size();
            const float lift = trail ? TrailLift : !road ? DrivewayLift : gravel ? GravelRoadLift : RoadLift;
            if (trail && grid && grid->spacing > 0)
                AddTrail(surface, samples, halfWidth, lift, *grid, height);
            else
                AddStrip(surface, samples, 0, samples.size() - 1, Across(halfWidth), lift, true, 0, height);
            ComputeNormals(surface, vertexStart, indexStart);

            // Markings: county paved roads get a dashed yellow center line, and white edge lines when fast.
            if (!road || gravel || line.owner != "county")
                continue;
            const float halfMark = MarkingWidth / 2;
            MeshPart& yellow = mesh[Part::YellowLine];
            const size_t yellowVertex = yellow.vertices.size(), yellowIndex = yellow.indices.size();
            size_t dashStart = 0;
            bool inDash = false;
            const float period = CenterDash + CenterGap;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const bool dash = std::fmod(samples[i].along, period) < CenterDash;
                if (dash && !inDash)
                    dashStart = i;
                // A dash runs from its first sample to the sample where it ends (or the line's last).
                if (inDash && (!dash || i + 1 == samples.size()))
                    AddStrip(yellow, samples, dashStart, i, { halfMark, -halfMark }, MarkingLift, false, samples[dashStart].along, height);
                inDash = dash;
            }
            ComputeNormals(yellow, yellowVertex, yellowIndex);
            if (line.speedLimit >= EdgeLineSpeed)
            {
                MeshPart& white = mesh[Part::WhiteLine];
                const size_t whiteVertex = white.vertices.size(), whiteIndex = white.indices.size();
                const float edge = line.width / 2 - 0.15f;
                AddStrip(white, samples, 0, samples.size() - 1, { edge + halfMark, edge - halfMark }, MarkingLift, false, 0, height);
                AddStrip(white, samples, 0, samples.size() - 1, { -edge + halfMark, -edge - halfMark }, MarkingLift, false, 0, height);
                ComputeNormals(white, whiteVertex, whiteIndex);
            }
        }
        return mesh;
    }
}
