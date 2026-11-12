#pragma once
#include "ObjectHighlight.h"
#include <cmath>

namespace HighlightGeometry
{
    struct Vertex
    {
        DirectX::XMFLOAT4 position;
        DirectX::XMFLOAT4 color;
        float depthTest;
    };

    inline bool Clip(DirectX::XMFLOAT4& a, DirectX::XMFLOAT4& b)
    {
        auto plane = [](const DirectX::XMFLOAT4& p, int i)
        {
            switch (i)
            {
            case 0: return p.w + p.x;
            case 1: return p.w - p.x;
            case 2: return p.w + p.y;
            case 3: return p.w - p.y;
            case 4: return p.z;
            default: return p.w - p.z;
            }
        };
        for (int i = 0; i < 6; ++i)
        {
            const float da = plane(a, i), db = plane(b, i);
            if (da < 0 && db < 0) return false;
            if ((da < 0) != (db < 0))
            {
                const float t = da / (da - db);
                DirectX::XMFLOAT4 p(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                    a.z + t * (b.z - a.z), a.w + t * (b.w - a.w));
                if (da < 0) a = p;
                else b = p;
            }
        }
        return a.w > 1e-6f && b.w > 1e-6f;
    }

    inline void Append(const ObjectHighlight& highlight, DirectX::FXMMATRIX viewProjection,
        float width, float height, std::vector<Vertex>& vertices)
    {
        using namespace DirectX;
        if (width <= 0 || height <= 0 || highlight.lineWidth <= 0) return;
        const float minimum[] = { highlight.bounds[0].x, highlight.bounds[0].y, highlight.bounds[0].z };
        const float maximum[] = { highlight.bounds[1].x, highlight.bounds[1].y, highlight.bounds[1].z };
        for (int axis = 0; axis < 3; ++axis)
        {
            const float low = minimum[axis], high = maximum[axis];
            if (!std::isfinite(low) || !std::isfinite(high) || low > high) return;
        }
        XMFLOAT4 corners[8];
        for (int i = 0; i < 8; ++i)
            XMStoreFloat4(&corners[i], XMVector4Transform(XMVectorSet(highlight.bounds[i & 1].x,
                highlight.bounds[(i >> 1) & 1].y, highlight.bounds[(i >> 2) & 1].z, 1), viewProjection));
        for (const auto adornment : highlight.adornments)
        {
            if (adornment != HighlightAdornment::BoundingBox) continue;
            for (int i = 0; i < 8; ++i)
            {
                for (int axis = 1; axis <= 4; axis <<= 1)
                {
                    if (i & axis) continue;
                    XMFLOAT4 a = corners[i], b = corners[i | axis];
                    if (!Clip(a, b)) continue;
                    a = XMFLOAT4(a.x / a.w, a.y / a.w, a.z / a.w, 1);
                    b = XMFLOAT4(b.x / b.w, b.y / b.w, b.z / b.w, 1);
                    const float dx = (b.x - a.x) * width, dy = (b.y - a.y) * height;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    if (length < 1e-6f) continue;
                    const float ox = -dy / length * highlight.lineWidth / width;
                    const float oy = dx / length * highlight.lineWidth / height;
                    const float depth = highlight.occlusion == HighlightOcclusion::SceneDepth ? 1.0f : 0.0f;
                    Vertex quad[] = {
                        { XMFLOAT4(a.x + ox, a.y + oy, a.z, 1), highlight.color, depth },
                        { XMFLOAT4(a.x - ox, a.y - oy, a.z, 1), highlight.color, depth },
                        { XMFLOAT4(b.x + ox, b.y + oy, b.z, 1), highlight.color, depth },
                        { XMFLOAT4(b.x - ox, b.y - oy, b.z, 1), highlight.color, depth }
                    };
                    for (const int index : { 0, 1, 2, 2, 1, 3 }) vertices.push_back(quad[index]);
                }
            }
        }
    }
}
