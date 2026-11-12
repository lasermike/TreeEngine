#pragma once
#include <DirectXMath.h>
#include <vector>

enum class HighlightOcclusion
{
    AlwaysVisible,
    SceneDepth
};

enum class HighlightAdornment
{
    BoundingBox
};

// Producers choose meaning and appearance; the renderer only draws typed adornments.
struct ObjectHighlight
{
    DirectX::XMFLOAT3 bounds[2];
    DirectX::XMFLOAT4 color = DirectX::XMFLOAT4(1, 1, 0, 1);
    float lineWidth = 2;
    HighlightOcclusion occlusion = HighlightOcclusion::AlwaysVisible;
    std::vector<HighlightAdornment> adornments = { HighlightAdornment::BoundingBox };
};
