//--------------------------------------------------------------------------------------
// RoadWear.hlsli: wear across a road's width (a terrain's "roadWear"; see SceneDefinition.h TerrainDefinition),
// shared by raster (Tree.hlsl) and ray tracing (RaytracingLibrary.hlsl) through Terrain.hlsli.
//
// The road mesh carries each vertex's place across the road in its tangent's w: 1 at one edge to 2 at the other
// (RoadMesh.cpp; only the sign of w means anything else). From it, by the surface's wear profile:
//   1 loose (gravel, patchy gravel, pad gravel): two wheel ruts, darker and smoother where stones are pressed in;
//     a mossy strip down the middle; dirt and fines at the edges.
//   2 hard (paved, chip seal): four polished wheel paths, darker and smoother; lighter fines along the edges.
//   3 dirt: bare, lighter ruts; weeds down the middle and along the edges.
// Every band wanders with noise along the road and is broken up by finer noise, so none is a ruled line. The
// material carries the profile in metallicRoughnessTextures.z and the strength (0 to 1) in .w (Materials.h).
//--------------------------------------------------------------------------------------

#ifndef ROAD_WEAR_HLSLI
#define ROAD_WEAR_HLSLI

#include "AntiTiling.hlsli"

// 1 within half of width of center, fading to 0 at width.
float RoadBand(float across, float center, float width)
{
    return 1 - smoothstep(width * 0.5f, width, abs(across - center));
}

// The wear at a point `across` the road (0 to 1): rgb multiplies the base color, a the roughness.
float4 RoadWear(ShaderMaterial material, float across, float3 positionW)
{
    const float profile = material.metallicRoughnessTextures.z, strength = material.metallicRoughnessTextures.w;
    if (profile <= 0 || strength <= 0)
        return 1;
    const float a = saturate(across) + (AntiTileNoise(positionW.xz * 0.35f) - 0.5f) * 0.06f;
    const float grain = AntiTileNoise(positionW.xz * 3.1f);
    const float edge = 1 - smoothstep(0.0f, 0.08f, min(a, 1 - a));
    float3 color = 1;
    float roughness = 1;
    if (profile < 1.5f)
    {
        const float rut = saturate(RoadBand(a, 0.3f, 0.08f) + RoadBand(a, 0.7f, 0.08f)) * (0.6f + 0.4f * grain);
        const float centre = RoadBand(a, 0.5f, 0.07f) * smoothstep(0.3f, 0.7f, grain);
        color *= lerp(1, 0.8f, rut);
        roughness *= lerp(1, 0.85f, rut);
        color = lerp(color, color * float3(0.75f, 0.95f, 0.6f), centre * 0.8f);
        color = lerp(color, color * float3(0.85f, 0.75f, 0.65f), edge * 0.7f);
    }
    else if (profile < 2.5f)
    {
        const float paths = saturate(RoadBand(a, 0.17f, 0.06f) + RoadBand(a, 0.36f, 0.06f) + RoadBand(a, 0.64f, 0.06f)
            + RoadBand(a, 0.83f, 0.06f)) * (0.7f + 0.3f * grain);
        color *= lerp(1, 0.88f, paths);
        roughness *= lerp(1, 0.85f, paths);
        color = lerp(color, color * float3(1.25f, 1.2f, 1.12f), edge * 0.6f * grain);
    }
    else
    {
        const float rut = saturate(RoadBand(a, 0.3f, 0.09f) + RoadBand(a, 0.7f, 0.09f));
        const float weeds = RoadBand(a, 0.5f, 0.1f) * smoothstep(0.35f, 0.75f, grain);
        color = lerp(color, color * float3(1.1f, 1.05f, 1.0f), rut);
        color = lerp(color, color * float3(0.6f, 0.95f, 0.45f), weeds);
        color = lerp(color, color * float3(0.75f, 0.95f, 0.6f), edge * 0.5f * grain);
    }
    return float4(lerp(1, color, strength), lerp(1, roughness, strength));
}

#endif
