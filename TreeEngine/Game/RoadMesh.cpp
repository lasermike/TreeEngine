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

    // A surface laid on ungraded ground (a trail) can dip under a ridge between its rows and columns, where its flat
    // triangles cut a corner of the ground: each triangle is checked at points an eighth of the way apart, and where
    // the ground there comes within `lift` of it, its corners are raised to clear it by `lift` (twice over, since a
    // raised corner changes its neighbors' triangles only upward).
    void ClearGround(Roads::MeshPart& part, size_t firstIndex, float lift, const std::function<bool(float, float, float&)>& height)
    {
        const int steps = 8;
        for (int pass = 0; pass < 2; ++pass)
        {
            std::vector<float> raise(part.vertices.size(), 0.0f);
            for (size_t i = firstIndex; i + 2 < part.indices.size(); i += 3)
            {
                const UINT corner[3] = { part.indices[i], part.indices[i + 1], part.indices[i + 2] };
                const XMFLOAT3& a = part.vertices[corner[0]].Pos;
                const XMFLOAT3& b = part.vertices[corner[1]].Pos;
                const XMFLOAT3& c = part.vertices[corner[2]].Pos;
                float deficit = 0;
                for (int u = 0; u <= steps; ++u)
                    for (int v = 0; u + v <= steps; ++v)
                    {
                        const float wu = float(u) / steps, wv = float(v) / steps, wa = 1 - wu - wv;
                        const float x = a.x * wa + b.x * wu + c.x * wv, z = a.z * wa + b.z * wu + c.z * wv;
                        const float y = a.y * wa + b.y * wu + c.y * wv;
                        float ground = 0;
                        if (height(x, z, ground))
                            deficit = std::max(deficit, ground + lift - y);
                    }
                for (const UINT v : corner)
                    raise[v] = std::max(raise[v], deficit);
            }
            for (size_t v = 0; v < raise.size(); ++v)
                part.vertices[v].Pos.y += raise[v];
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
    Mesh BuildMesh(const Network& network, XMFLOAT3 offset, const std::function<bool(float, float, float&)>& height)
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
            AddStrip(surface, samples, 0, samples.size() - 1, Across(halfWidth), lift, true, 0, height);
            if (trail)
                ClearGround(surface, indexStart, lift, height);
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
