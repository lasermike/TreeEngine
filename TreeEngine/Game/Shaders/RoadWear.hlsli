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
//
// The constants below shape the effect. Places and widths are fractions of the road's width (0 one edge, 1 the
// other); a band is full within half its width of its centre and gone at its width. Colors multiply the base color;
// roughness factors multiply the roughness. The terrain's roadWear scales them all toward no change. Edit and
// rebuild (the shaders compile with RenderPlatform12).
//--------------------------------------------------------------------------------------

#ifndef ROAD_WEAR_HLSLI
#define ROAD_WEAR_HLSLI

#include "AntiTiling.hlsli"

// Noise, for all profiles.
static const float RoadWearWander = 0.06f;          // How far the bands wander across the road (fraction of width).
static const float RoadWearWanderScale = 0.35f;     // Its noise per meter: lower wanders over longer stretches.
static const float RoadWearGrainScale = 3.1f;       // The breakup noise per meter: higher is finer speckle.
static const float RoadWearEdgeWidth = 0.08f;       // Each edge band, from the edge inward.

// 1: loose surfaces (gravel, patchy gravel, pad gravel).
static const float2 LooseRuts = float2(0.3f, 0.7f); // The two wheel ruts' centres,
static const float LooseRutWidth = 0.08f;           // and their width.
static const float LooseRutBreakup = 0.4f;          // How much the grain thins the ruts (0 solid, 1 broken).
static const float LooseRutDarken = 0.8f;           // Color in the ruts (stones pressed into darker fines).
static const float LooseRutRoughness = 0.85f;       // Roughness in the ruts (packed smoother).
static const float LooseCentreWidth = 0.07f;        // The centre strip's width (at 0.5),
static const float2 LooseCentreGrain = float2(0.3f, 0.7f);  // the grain range over which it fills in (patchy moss),
static const float LooseCentreAmount = 0.8f;        // how strongly,
static const float3 LooseCentreColor = float3(0.75f, 0.95f, 0.6f); // and its tint (moss).
static const float LooseEdgeAmount = 0.7f;          // The edges' strength,
static const float3 LooseEdgeColor = float3(0.85f, 0.75f, 0.65f);  // and tint (dirt and fines).

// 2: hard surfaces (paved, chip seal).
static const float4 HardPaths = float4(0.17f, 0.36f, 0.64f, 0.83f);  // Four wheel paths' centres (two lanes),
static const float HardPathWidth = 0.06f;           // their width,
static const float HardPathBreakup = 0.3f;          // how much the grain breaks them up,
static const float HardPathDarken = 0.88f;          // their color (polished darker),
static const float HardPathRoughness = 0.85f;       // and roughness (polished smoother).
static const float HardEdgeAmount = 0.6f;           // The edges' strength (times the grain),
static const float3 HardEdgeColor = float3(1.25f, 1.2f, 1.12f);    // and tint (lighter fines and grit).

// 3: dirt.
static const float2 DirtRuts = float2(0.3f, 0.7f);  // The two ruts' centres,
static const float DirtRutWidth = 0.09f;            // their width,
static const float3 DirtRutColor = float3(1.1f, 1.05f, 1.0f);      // and tint (bare, lighter).
static const float DirtWeedWidth = 0.1f;            // The weedy centre's width (at 0.5),
static const float2 DirtWeedGrain = float2(0.35f, 0.75f);   // the grain range over which weeds fill in,
static const float3 DirtWeedColor = float3(0.6f, 0.95f, 0.45f);    // and their tint.
static const float DirtEdgeAmount = 0.5f;           // The edges' strength (times the grain),
static const float3 DirtEdgeColor = float3(0.75f, 0.95f, 0.6f);    // and tint (grassy).

// Ragged edges (a terrain's "roadEdges"): within the edge band (the material's opticalParams.w, a fraction of the
// width from each edge) the surface is cut away, more of it nearer the edge, so the ground shows through between
// clumps of surface. What stays is chosen by clumpy noise and the texture's brightness (standing in for height: the
// lighter stones stand proud of darker fines), so stones reach out over the ground rather than fading.
static const float RoadEdgeClumpScale = 2.3f;       // The clumps' noise per meter: higher is smaller clumps.
static const float RoadEdgeFineScale = 9.0f;        // A finer noise per meter that frays the clumps.
static const float RoadEdgeFineWeight = 0.3f;       // How much it frays them.
static const float RoadEdgeTextureWeight = 0.6f;    // How much the texture's brightness decides what stays.
static const float RoadEdgeTextureGain = 3.0f;      // Brightness to height: higher makes more of a dark surface stay.

// 1 within half of width of center, fading to 0 at width.
float RoadBand(float across, float center, float width)
{
    return 1 - smoothstep(width * 0.5f, width, abs(across - center));
}

// Whether a road surface's pixel stays, at a point `across` it (0 to 1), with its base color (the texture's).
bool RoadEdgeKeeps(ShaderMaterial material, float across, float3 positionW, float3 baseColor)
{
    const float width = material.opticalParams.w;
    if (width <= 0)
        return true;
    const float a = saturate(across);
    const float inward = min(a, 1 - a) / width;     // 0 at the edge, 1 where the band ends.
    if (inward >= 1)
        return true;
    const float clumps = lerp(AntiTileNoise(positionW.xz * RoadEdgeClumpScale),
        AntiTileNoise(positionW.xz * RoadEdgeFineScale + 7.3f), RoadEdgeFineWeight);
    const float height = saturate(dot(baseColor, float3(0.3f, 0.59f, 0.11f)) * RoadEdgeTextureGain);
    return lerp(clumps, height, RoadEdgeTextureWeight) > 1 - inward;
}

// The wear at a point `across` the road (0 to 1): rgb multiplies the base color, a the roughness.
float4 RoadWear(ShaderMaterial material, float across, float3 positionW)
{
    const float profile = material.metallicRoughnessTextures.z, strength = material.metallicRoughnessTextures.w;
    if (profile <= 0 || strength <= 0)
        return 1;
    const float a = saturate(across) + (AntiTileNoise(positionW.xz * RoadWearWanderScale) - 0.5f) * RoadWearWander;
    const float grain = AntiTileNoise(positionW.xz * RoadWearGrainScale);
    const float edge = 1 - smoothstep(0.0f, RoadWearEdgeWidth, min(a, 1 - a));
    float3 color = 1;
    float roughness = 1;
    if (profile < 1.5f)
    {
        const float rut = saturate(RoadBand(a, LooseRuts.x, LooseRutWidth) + RoadBand(a, LooseRuts.y, LooseRutWidth))
            * (1 - LooseRutBreakup + LooseRutBreakup * grain);
        const float centre = RoadBand(a, 0.5f, LooseCentreWidth) * smoothstep(LooseCentreGrain.x, LooseCentreGrain.y, grain);
        color *= lerp(1, LooseRutDarken, rut);
        roughness *= lerp(1, LooseRutRoughness, rut);
        color = lerp(color, color * LooseCentreColor, centre * LooseCentreAmount);
        color = lerp(color, color * LooseEdgeColor, edge * LooseEdgeAmount);
    }
    else if (profile < 2.5f)
    {
        const float paths = saturate(RoadBand(a, HardPaths.x, HardPathWidth) + RoadBand(a, HardPaths.y, HardPathWidth)
            + RoadBand(a, HardPaths.z, HardPathWidth) + RoadBand(a, HardPaths.w, HardPathWidth))
            * (1 - HardPathBreakup + HardPathBreakup * grain);
        color *= lerp(1, HardPathDarken, paths);
        roughness *= lerp(1, HardPathRoughness, paths);
        color = lerp(color, color * HardEdgeColor, edge * HardEdgeAmount * grain);
    }
    else
    {
        const float rut = saturate(RoadBand(a, DirtRuts.x, DirtRutWidth) + RoadBand(a, DirtRuts.y, DirtRutWidth));
        const float weeds = RoadBand(a, 0.5f, DirtWeedWidth) * smoothstep(DirtWeedGrain.x, DirtWeedGrain.y, grain);
        color = lerp(color, color * DirtRutColor, rut);
        color = lerp(color, color * DirtWeedColor, weeds);
        color = lerp(color, color * DirtEdgeColor, edge * DirtEdgeAmount * grain);
    }
    return float4(lerp(1, color, strength), lerp(1, roughness, strength));
}

#endif
