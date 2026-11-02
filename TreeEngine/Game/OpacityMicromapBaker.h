#pragma once
#include <cstdint>
#include <vector>

// Builds DXR 1.2 opacity micromaps (OMMs) for alpha-tested triangles on the CPU. An OMM splits a triangle into
// 4^level micro-triangles and stores, for each, whether the texture there is certainly opaque, certainly
// transparent, or unknown; rays then skip the any-hit shader everywhere except unknown micro-triangles.
namespace OpacityMicromap
{
    // D3D12_RAYTRACING_OPACITY_MICROMAP_STATE values, two bits each in OC1_4_STATE data.
    enum State : uint8_t { Transparent = 0, Opaque = 1, UnknownTransparent = 2, UnknownOpaque = 3 };

    // D3D12_RAYTRACING_OPACITY_MICROMAP_SPECIAL_INDEX values, for triangles that need no data.
    const int FullyTransparentIndex = -1;
    const int FullyOpaqueIndex = -2;

    // Index of the micro-triangle containing barycentrics (u, v) at a subdivision level, along the space-filling
    // curve the DXR and Vulkan specifications define (u weights the triangle's second vertex, v its third).
    uint32_t BarycentricsToIndex(float u, float v, uint32_t level);

    // Mip-0 alpha of a texture, with the material's addressing.
    struct AlphaImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> alpha;
        bool wrap = true; // false clamps
    };

    struct Uv { float u, v; };

    struct TriangleOpacity
    {
        int specialIndex = 0;      // FullyTransparentIndex or FullyOpaqueIndex when data is empty; otherwise 0.
        std::vector<uint8_t> data; // OC1_4_STATE: two bits per micro-triangle, lowest bits first.
        uint32_t stateCounts[4] = {};
    };

    // Classifies each micro-triangle against an alpha cutoff (a texel passes when alpha >= cutoff), conservatively
    // for bilinear sampling at mip 0: a micro-triangle is opaque or transparent only if every texel that could
    // contribute to a sample inside it agrees. uv holds the triangle's texture coordinates in vertex order.
    TriangleOpacity BakeTriangle(const AlphaImage& image, float cutoff, const Uv uv[3], uint32_t level);
}
