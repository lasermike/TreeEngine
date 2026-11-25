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
        bool trails)
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
            if (trail && !trails)
                continue;
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

namespace
{
    typedef XMFLOAT2 Point;

    float Cross(const Point& o, const Point& a, const Point& b)
    {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    // The part of a convex polygon on the left of a -> b (inside, for a counterclockwise outline's edge).
    std::vector<Point> ClipLeft(const std::vector<Point>& polygon, const Point& a, const Point& b)
    {
        std::vector<Point> out;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& p = polygon[i];
            const Point& q = polygon[(i + 1) % polygon.size()];
            const float dp = Cross(a, b, p), dq = Cross(a, b, q);
            if (dp >= 0)
                out.push_back(p);
            if ((dp >= 0) != (dq >= 0))
            {
                const float t = dp / (dp - dq);
                out.push_back(Point(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t));
            }
        }
        return out;
    }

    float Area(const std::vector<Point>& polygon)
    {
        float area = 0;
        for (size_t i = 0; i < polygon.size(); ++i)
            area += polygon[i].x * polygon[(i + 1) % polygon.size()].y - polygon[(i + 1) % polygon.size()].x * polygon[i].y;
        return area / 2;
    }

    // The convex outlines of a ribbon `halfWidth` either side of its samples: a quad per pair of cross-sections, two
    // triangles where a tight bend folds it.
    template <typename Outline>
    void Ribbon(const std::vector<Sample>& samples, float halfWidth, std::vector<Outline>& out)
    {
        for (size_t k = 0; k + 1 < samples.size(); ++k)
        {
            const Sample& s0 = samples[k];
            const Sample& s1 = samples[k + 1];
            std::vector<Point> quad = {
                Point(s0.point.x - s0.side.x * halfWidth, s0.point.y - s0.side.y * halfWidth),
                Point(s1.point.x - s1.side.x * halfWidth, s1.point.y - s1.side.y * halfWidth),
                Point(s1.point.x + s1.side.x * halfWidth, s1.point.y + s1.side.y * halfWidth),
                Point(s0.point.x + s0.side.x * halfWidth, s0.point.y + s0.side.y * halfWidth) };
            if (Area(quad) < 0)
                std::reverse(quad.begin(), quad.end());
            bool convex = true;
            for (size_t i = 0; i < 4; ++i)
                convex = convex && Cross(quad[i], quad[(i + 1) % 4], quad[(i + 2) % 4]) >= 0;
            std::vector<std::vector<Point>> pieces;
            if (convex)
                pieces.push_back(quad);
            else
            {
                pieces.push_back({ quad[0], quad[1], quad[2] });
                pieces.push_back({ quad[0], quad[2], quad[3] });
            }
            for (auto& piece : pieces)
            {
                if (Area(piece) < 0)
                    std::reverse(piece.begin(), piece.end());
                if (Area(piece) < 1e-6f)
                    continue;
                Outline outline;
                outline.points = piece;
                outline.minX = outline.minZ = 1e30f;
                outline.maxX = outline.maxZ = -1e30f;
                for (const Point& p : piece)
                {
                    outline.minX = std::min(outline.minX, p.x); outline.maxX = std::max(outline.maxX, p.x);
                    outline.minZ = std::min(outline.minZ, p.y); outline.maxZ = std::max(outline.maxZ, p.y);
                }
                out.push_back(outline);
            }
        }
    }
}

namespace Roads
{
    TrailBuilder::TrailBuilder(const Network& network, XMFLOAT3 offset)
    {
        for (const Line& line : network.lines)
        {
            std::vector<Sample> samples = SampleCenterline(line.points);
            for (Sample& sample : samples)
                sample.point = XMFLOAT2(sample.point.x + offset.x, sample.point.y + offset.z);
            if (samples.size() < 2)
                continue;
            if (line.kind == Kind::Trail)
                Ribbon(samples, line.width / 2, m_trails);
            else
                Ribbon(samples, line.width / 2 + (line.kind == Kind::Road ? line.shoulder : 0), m_covers);
        }
    }

    void TrailBuilder::Bounds(float& minX, float& minZ, float& maxX, float& maxZ) const
    {
        minX = minZ = 1e30f;
        maxX = maxZ = -1e30f;
        for (const Outline& o : m_trails)
        {
            minX = std::min(minX, o.minX); maxX = std::max(maxX, o.maxX);
            minZ = std::min(minZ, o.minZ); maxZ = std::max(maxZ, o.maxZ);
        }
    }

    MeshPart TrailBuilder::Build(const GroundGrid& grid, int i0, int j0, int i1, int j1,
        const std::function<bool(float, float, float&)>& height) const
    {
        MeshPart part;
        const auto emit = [&](const std::vector<Point>& polygon, const Point& o, const Point& u, const Point& v,
            float h0, float hu, float hv)
        {
            // The terrain triangle's plane: h0 at o, hu at o + u, hv at o + v.
            const float det = u.x * v.y - u.y * v.x;
            const UINT base = static_cast<UINT>(part.vertices.size());
            for (const Point& p : polygon)
            {
                const float dx = p.x - o.x, dz = p.y - o.y;
                const float a = (dx * v.y - dz * v.x) / det, b = (u.x * dz - u.y * dx) / det;
                SimpleVertex vertex = {};
                vertex.Pos = XMFLOAT3(p.x, h0 + (hu - h0) * a + (hv - h0) * b + TrailLift, p.y);
                vertex.Normal = XMFLOAT3(0, 1, 0);
                vertex.Tex = XMFLOAT2(p.x, -p.y);
                vertex.TangentU = XMFLOAT4(1, 0, 0, 1);
                part.vertices.push_back(vertex);
            }
            // Counterclockwise in (x, z) is clockwise seen from above: these indices make each triangle face up.
            for (UINT i = 1; i + 1 < polygon.size(); ++i)
                part.indices.insert(part.indices.end(), { base, base + i + 1, base + i });
        };
        // A piece minus every road or driveway outline it overlaps: the piece's parts outside each in turn.
        const auto uncovered = [&](const std::vector<Point>& piece)
        {
            std::vector<std::vector<Point>> pieces = { piece };
            for (const Outline& cover : m_covers)
            {
                std::vector<std::vector<Point>> next;
                for (const auto& p : pieces)
                {
                    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
                    for (const Point& q : p)
                    {
                        minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
                        minZ = std::min(minZ, q.y); maxZ = std::max(maxZ, q.y);
                    }
                    if (maxX <= cover.minX || minX >= cover.maxX || maxZ <= cover.minZ || minZ >= cover.maxZ)
                    {
                        next.push_back(p);
                        continue;
                    }
                    // P minus a convex C: for each edge k of C, P inside edges before k and outside edge k.
                    std::vector<Point> inside = p;
                    const auto& c = cover.points;
                    for (size_t k = 0; k < c.size() && inside.size() >= 3; ++k)
                    {
                        const Point& a = c[k];
                        const Point& b = c[(k + 1) % c.size()];
                        const std::vector<Point> outside = ClipLeft(inside, b, a);
                        if (outside.size() >= 3 && std::abs(Area(outside)) > 1e-6f)
                            next.push_back(outside);
                        inside = ClipLeft(inside, a, b);
                    }
                }
                pieces.swap(next);
            }
            return pieces;
        };
        for (const Outline& outline : m_trails)
        {
            const int ci0 = std::max(i0, static_cast<int>(std::floor((outline.minX - grid.originX) / grid.spacing)));
            const int ci1 = std::min(i1, static_cast<int>(std::floor((outline.maxX - grid.originX) / grid.spacing)));
            const int cj0 = std::max(j0, static_cast<int>(std::floor((outline.minZ - grid.originZ) / grid.spacing)));
            const int cj1 = std::min(j1, static_cast<int>(std::floor((outline.maxZ - grid.originZ) / grid.spacing)));
            for (int j = cj0; j <= cj1; ++j)
                for (int i = ci0; i <= ci1; ++i)
                {
                    const float x0 = grid.originX + i * grid.spacing, z0 = grid.originZ + j * grid.spacing;
                    const float x1 = x0 + grid.spacing, z1 = z0 + grid.spacing;
                    float h00, h10, h01, h11;
                    if (!height(x0, z0, h00) || !height(x1, z0, h10) || !height(x0, z1, h01) || !height(x1, z1, h11))
                        continue;
                    const Point c00(x0, z0), c10(x1, z0), c01(x0, z1), c11(x1, z1);
                    for (int t = 0; t < 2; ++t)
                    {
                        // The cell's two triangles, counterclockwise: split from its +x corner to its +z corner.
                        std::vector<Point> polygon = t ? std::vector<Point>{ c10, c11, c01 } : std::vector<Point>{ c00, c10, c01 };
                        for (size_t e = 0; e < outline.points.size() && polygon.size() >= 3; ++e)
                            polygon = ClipLeft(polygon, outline.points[e], outline.points[(e + 1) % outline.points.size()]);
                        if (polygon.size() < 3 || std::abs(Area(polygon)) < 1e-6f)
                            continue;
                        for (const auto& piece : uncovered(polygon))
                        {
                            if (t == 0)
                                emit(piece, c00, Point(grid.spacing, 0), Point(0, grid.spacing), h00, h10, h01);
                            else
                                emit(piece, c11, Point(-grid.spacing, 0), Point(0, -grid.spacing), h11, h01, h10);
                        }
                    }
                }
        }
        ComputeNormals(part, 0, 0);
        return part;
    }
}
