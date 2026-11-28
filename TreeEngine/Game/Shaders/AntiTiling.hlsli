//--------------------------------------------------------------------------------------
// AntiTiling.hlsli: hides a tiling texture's repeat (a terrain's "antiTiling"; see SceneDefinition.h
// TerrainDefinition), shared by raster (Tree.hlsl) and ray tracing (RaytracingLibrary.hlsl) through Terrain.hlsli.
//
// Repeat: a smooth noise over the texture's own coordinates (about three repeats per cell) picks, region by region,
// one of eight offsets into the texture; each pixel shows one offset, blending into the next over a narrow seam, so
// the tile grid never shows and neighbouring regions show different parts of the texture. Offsets
// only (no rotation), so normal maps stay in their frame. Every map of a material uses the same offsets and blend.
// Samples take explicit gradients of the un-offset coordinates: an offset jumps between regions, which mustn't
// change the mip level.
//
// Drift: over the material's drift size (meters, world x/z) the color brightens or darkens and warms or cools, and
// the roughness shifts, by up to the drift amount, so distant ground doesn't repeat either.
//
// A material opts in through tintTextures.z (1), with the drift amount in tintTextures.w and its size in
// opticalParams.z (Materials.h).
//--------------------------------------------------------------------------------------

#ifndef ANTI_TILING_HLSLI
#define ANTI_TILING_HLSLI

struct AntiTile
{
    float2 offsetA;
    float2 offsetB;
    float blend;        // 0: all A, 1: all B.
};

bool UsesAntiTiling(ShaderMaterial material)
{
    return material.tintTextures.z > 0;
}

float AntiTileHash(float2 cell)
{
    float3 p = frac(float3(cell.xyx) * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

// Smooth value noise in [0, 1]; position in cells.
float AntiTileNoise(float2 position)
{
    const float2 cell = floor(position), f = frac(position);
    const float2 u = f * f * (3 - 2 * f);
    return lerp(lerp(AntiTileHash(cell), AntiTileHash(cell + float2(1, 0)), u.x),
        lerp(AntiTileHash(cell + float2(0, 1)), AntiTileHash(cell + float2(1, 1)), u.x), u.y);
}

float2 AntiTileOffset(float index)
{
    return float2(AntiTileHash(float2(index, 7.13f)), AntiTileHash(float2(19.7f, index)));
}

// The offsets and blend at uv (the base color's texture coordinates, in repeats).
AntiTile AntiTileAt(float2 uv)
{
    const float level = AntiTileNoise(uv * 0.35f) * 8;
    const float index = floor(level);
    AntiTile tile;
    tile.offsetA = AntiTileOffset(index);
    tile.offsetB = AntiTileOffset(index + 1);
    // A narrow band: blending two different patterns halves their contrast, so most of the ground shows one sample
    // at full contrast and only the seams between regions blend.
    tile.blend = smoothstep(0.42f, 0.58f, level - index);
    return tile;
}

// The color drift at a world position: rgb multiplies the base color, a the roughness.
float4 AntiTileDrift(ShaderMaterial material, float3 positionW)
{
    const float amount = material.tintTextures.w;
    if (!UsesAntiTiling(material) || amount <= 0 || material.opticalParams.z <= 0)
        return 1;
    const float2 p = positionW.xz / material.opticalParams.z;
    const float brightness = AntiTileNoise(p) * 2 - 1;
    const float warmth = AntiTileNoise(p * 2.7f + 31.7f) * 2 - 1;
    const float rough = AntiTileNoise(p * 1.9f - 11.3f) * 2 - 1;
    const float3 color = (1 + amount * brightness) * (1 + 0.35f * amount * warmth * float3(1, 0.2f, -0.8f));
    return float4(max(color, 0), max(1 + 0.5f * amount * rough, 0.05f));
}

#endif
