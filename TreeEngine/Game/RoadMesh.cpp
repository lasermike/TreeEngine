#include "pch.h"
#include "RoadMesh.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    struct Sample
    {
        XMFLOAT2 point;     // World x, z.
        XMFLOAT2 side;      // Unit vector to the line's left (perpendicular to its direction).
        float along;        // Meters from the line's start.
    };

    // Points every SampleSpacing meters (and at every vertex), with their sideways directions.
    std::vector<Sample> Resample(const std::vector<XMFLOAT2>& line, XMFLOAT2 offset)
    {
        // Evenly along the whole line, SampleSpacing apart or a little less, ends included: however finely smoothing
        // split the line, the cross-sections stay about a meter apart.
        std::vector<XMFLOAT2> points;
        std::vector<float> distance;
        for (const XMFLOAT2& p : line)
        {
            const XMFLOAT2 q(p.x + offset.x, p.y + offset.y);
            if (!points.empty() && std::hypot(q.x - points.back().x, q.y - points.back().y) < 1e-4f)
                continue;
            distance.push_back(points.empty() ? 0 : distance.back() + std::hypot(q.x - points.back().x, q.y - points.back().y));
            points.push_back(q);
        }
        std::vector<Sample> samples;
        if (points.size() < 2)
            return samples;
        const float length = distance.back();
        const int steps = std::max(1, static_cast<int>(std::ceil(length / Roads::SampleSpacing - 1e-4f)));
        size_t segment = 0;
        for (int s = 0; s <= steps; ++s)
        {
            const float along = length * s / steps;
            while (segment + 2 < points.size() && distance[segment + 1] < along)
                ++segment;
            const float span = distance[segment + 1] - distance[segment];
            const float t = span > 0 ? std::min(1.0f, std::max(0.0f, (along - distance[segment]) / span)) : 0;
            const XMFLOAT2& a = points[segment];
            const XMFLOAT2& b = points[segment + 1];
            samples.push_back({ XMFLOAT2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t), XMFLOAT2(0, 0), along });
        }
        // Sideways from the direction across each sample (central differences), so bends get mitered widths.
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const XMFLOAT2& p = samples[i > 0 ? i - 1 : i].point;
            const XMFLOAT2& q = samples[i + 1 < samples.size() ? i + 1 : i].point;
            float dx = q.x - p.x, dz = q.y - p.y;
            const float length = std::hypot(dx, dz);
            if (length < 1e-6f)
            {
                samples[i].side = i > 0 ? samples[i - 1].side : XMFLOAT2(1, 0);
                continue;
            }
            dx /= length;
            dz /= length;
            samples[i].side = XMFLOAT2(-dz, dx);
        }
        return samples;
    }

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

// Chaikin corner cutting: each pass replaces every inner corner with two points a quarter of the way along its
// segments, keeping the ends, so the polylines the importer simplified bend smoothly. The segments are first split
// into pieces of at most SmoothingPiece meters, so a cut never reaches more than a quarter piece from a corner:
// the bend moves well under a road's half-width, however long the importer's segments are.
std::vector<XMFLOAT2> Roads::SmoothCenterline(const std::vector<XMFLOAT2>& input)
{
    std::vector<XMFLOAT2> line;
    for (size_t i = 0; i + 1 < input.size(); ++i)
    {
        const XMFLOAT2 a = input[i], b = input[i + 1];
        const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / SmoothingPiece)));
        for (int p = 0; p < pieces; ++p)
            line.push_back(XMFLOAT2(a.x + (b.x - a.x) * p / pieces, a.y + (b.y - a.y) * p / pieces));
    }
    if (!input.empty())
        line.push_back(input.back());
    for (int pass = 0; pass < Roads::SmoothingPasses && line.size() > 2; ++pass)
    {
        std::vector<XMFLOAT2> next = { line.front() };
        for (size_t i = 0; i + 1 < line.size(); ++i)
        {
            const XMFLOAT2 a = line[i], b = line[i + 1];
            if (i > 0)
                next.push_back(XMFLOAT2(0.75f * a.x + 0.25f * b.x, 0.75f * a.y + 0.25f * b.y));
            if (i + 2 < line.size())
                next.push_back(XMFLOAT2(0.25f * a.x + 0.75f * b.x, 0.25f * a.y + 0.75f * b.y));
        }
        next.push_back(line.back());
        line.swap(next);
    }
    return line;
}

namespace Roads
{
    Mesh BuildMesh(const Network& network, XMFLOAT3 offset, const std::function<bool(float, float, float&)>& height)
    {
        Mesh mesh;
        for (const Line& line : network.lines)
        {
            const std::vector<Sample> samples = Resample(SmoothCenterline(line.points), XMFLOAT2(offset.x, offset.z));
            if (samples.size() < 2)
                continue;
            const bool road = line.kind == Kind::Road;
            const bool gravel = line.surface == "gravel";
            MeshPart& surface = mesh[gravel ? Part::Gravel : Part::Asphalt];
            const float halfWidth = line.width / 2 + (road ? line.shoulder : 0);
            const size_t vertexStart = surface.vertices.size(), indexStart = surface.indices.size();
            const float lift = !road ? DrivewayLift : gravel ? GravelRoadLift : RoadLift;
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
