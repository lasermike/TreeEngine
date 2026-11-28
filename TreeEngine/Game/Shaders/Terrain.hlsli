//--------------------------------------------------------------------------------------
// Terrain.hlsli: terrain material layers (Game/Terrain.h), shared by raster (Tree.hlsl) and ray tracing
// (RaytracingLibrary.hlsl). A terrain's material names its layers in terrainLayers through biplanarParams.z (first
// layer + 1) and .w (count). Each layer is a material of its own (maps by texture slot, tiling through its UV
// transform, biplanar for steep rock) with a coverage rule: a height band and a slope band, each with soft edges,
// shifted by noise so the boundaries wander. Layers paint over the ones before them; the first covers everything.
//
// The including shader declares, before including this file:
//   StructuredBuffer<TerrainLayer> terrainLayers;
//   float4 SampleTerrainTexture(int slot, bool clampMap, float2 uv, float2 uvDx, float2 uvDy);
//--------------------------------------------------------------------------------------

#ifndef TERRAIN_HLSLI
#define TERRAIN_HLSLI

#include "AntiTiling.hlsli"

static const uint MaxTerrainLayers = 4;

bool IsTerrainMaterial(ShaderMaterial material)
{
    return material.biplanarParams.z > 0;
}

float TerrainHash(float2 cell)
{
    float3 p = frac(float3(cell.xyx) * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

// Smooth value noise in [-1, 1], two octaves; position in noise cells.
float TerrainNoise(float2 position)
{
    float sum = 0, amplitude = 0.65f;
    [unroll] for (uint octave = 0; octave < 2; ++octave)
    {
        const float2 cell = floor(position), f = frac(position);
        const float2 u = f * f * (3 - 2 * f);
        const float a = TerrainHash(cell), b = TerrainHash(cell + float2(1, 0));
        const float c = TerrainHash(cell + float2(0, 1)), d = TerrainHash(cell + float2(1, 1));
        sum += amplitude * (lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 2 - 1);
        position = position * 2.03f + 17.7f;
        amplitude *= 0.5f;
    }
    return sum / 0.975f;
}

// 1 inside [low, high], fading to 0 over `blend` on each side.
float TerrainBand(float value, float low, float high, float blend)
{
    const float b = max(blend, 1e-3f);
    return smoothstep(low - b, low + b, value) * (1 - smoothstep(high - b, high + b, value));
}

// How much of a layer covers a point: height (meters) and slope (degrees from flat) bands, their limits shifted by
// noise (height by layer.noise.x meters, slope by .y degrees, in cells of .z meters).
float TerrainCoverage(TerrainLayer layer, float3 positionW, float height, float3 normalW)
{
    const float noise = layer.noise.z > 0 ? TerrainNoise(positionW.xz / layer.noise.z) : 0;
    const float slope = degrees(acos(saturate(normalW.y)));
    return TerrainBand(height + noise * layer.noise.x, layer.height.x, layer.height.y, layer.height.z)
        * TerrainBand(slope + noise * layer.noise.y, layer.slope.x, layer.slope.y, layer.slope.z);
}

// A layer map through the top-down UVs, or blended over the biplanar projections for a biplanar layer.
float4 SampleTerrainLayerMap(int slot, bool clampMap, ShaderMaterial material, uint role, float2 uv, float2 uvDx, float2 uvDy,
    Biplanar biplanar)
{
    if (material.biplanarParams.x <= 0)
        return SampleTerrainTexture(slot, clampMap, TransformMaterialUv(material, uv, role),
            TransformMaterialUvGradient(material, uvDx, role), TransformMaterialUvGradient(material, uvDy, role));
    float4 result = 0;
    [unroll] for (uint i = 0; i < 2; ++i)
        if (biplanar.weight[i] > 0)
            result += biplanar.weight[i] * SampleTerrainTexture(slot, clampMap, TransformMaterialUv(material, biplanar.uv[i], role),
                TransformMaterialUvGradient(material, biplanar.uvDx[i], role), TransformMaterialUvGradient(material, biplanar.uvDy[i], role));
    return result;
}

// A top-down layer map at the two anti-tiling offsets, blended (one sample where the blend is all one side).
float4 SampleTerrainAntiTiled(int slot, bool clampMap, ShaderMaterial material, uint role, float2 uv, float2 uvDx,
    float2 uvDy, AntiTile tile)
{
    const float2 roleUv = TransformMaterialUv(material, uv, role);
    const float2 dx = TransformMaterialUvGradient(material, uvDx, role), dy = TransformMaterialUvGradient(material, uvDy, role);
    if (tile.blend <= 0.001f)
        return SampleTerrainTexture(slot, clampMap, roleUv + tile.offsetA, dx, dy);
    if (tile.blend >= 0.999f)
        return SampleTerrainTexture(slot, clampMap, roleUv + tile.offsetB, dx, dy);
    return lerp(SampleTerrainTexture(slot, clampMap, roleUv + tile.offsetA, dx, dy),
        SampleTerrainTexture(slot, clampMap, roleUv + tile.offsetB, dx, dy), tile.blend);
}

// The height layers see: the surface's, or on a skirt (which hangs below its node's edge to fill cracks) its edge's.
// Skirt vertices carry their drop in the tangent's w: |w| = 1 + drop.
float TerrainMaterialHeight(float3 positionW, float tangentW)
{
    return positionW.y + max(abs(tangentW) - 1, 0);
}

// The terrain's surface at a point: base color (in Diffuse and Ambient, so lighting takes a texture color of 1),
// metallic and roughness blended over its layers, and the shading normal. positionW and its screen derivatives drive
// the coverage (with height, see TerrainMaterialHeight) and the biplanar projections; uv (world x and -z) and its
// derivatives the top-down maps; the tangent frame is the vertex's (u along tangentW, v along bitangentW).
ShaderMaterial TerrainSurface(ShaderMaterial base, float3 positionW, float height, float3 positionDx, float3 positionDy,
    float3 normalW, float3 tangentW, float3 bitangentW, float2 uv, float2 uvDx, float2 uvDy, out float3 shadingNormal)
{
    const uint first = (uint)base.biplanarParams.z - 1;
    const uint count = min((uint)base.biplanarParams.w, MaxTerrainLayers);
    // Each layer's share: its coverage, less what the layers above it cover.
    float weights[MaxTerrainLayers] = { 0, 0, 0, 0 };
    float remaining = 1;
    for (int i = (int)count - 1; i >= 0; --i)
    {
        const float coverage = i == 0 ? 1 : TerrainCoverage(terrainLayers[first + i], positionW, height, normalW);
        weights[i] = remaining * coverage;
        remaining *= 1 - coverage;
    }

    ShaderMaterial surface = base;
    surface.Diffuse = 0;
    surface.Ambient = 0;
    surface.specularParams.yz = 0;
    float3 normalSum = 0;
    for (uint layerIndex = 0; layerIndex < count; ++layerIndex)
    {
        const float weight = weights[layerIndex];
        if (weight < 1e-3f)
            continue;
        const TerrainLayer layer = terrainLayers[first + layerIndex];
        const ShaderMaterial m = layer.material;
        Biplanar biplanar = (Biplanar)0;
        if (m.biplanarParams.x > 0)
            biplanar = BiplanarSetup(m, positionW, positionDx, positionDy, normalW, uv, uvDx, uvDy);
        // Anti-tiling for the top-down layers (biplanar rock keeps its projections).
        const bool antiTiled = UsesAntiTiling(m) && m.biplanarParams.x <= 0;
        const AntiTile tile = AntiTileAt(TransformMaterialUv(m, uv, BaseColorTextureRole));

        // (if/else rather than ?:, so only the chosen side samples.)
        float4 color = 1;
        if (layer.textures.x >= 0)
        {
            if (antiTiled)
                color = SampleTerrainAntiTiled((int)layer.textures.x, layer.textures.y > 0, m, BaseColorTextureRole, uv, uvDx, uvDy, tile);
            else
                color = SampleTerrainLayerMap((int)layer.textures.x, layer.textures.y > 0, m, BaseColorTextureRole, uv, uvDx, uvDy, biplanar);
        }
        float metallic = m.specularParams.y, roughness = m.specularParams.z;
        if (m.metallicRoughnessTextures.x >= 0)
        {
            float4 mr;
            if (antiTiled)
                mr = SampleTerrainAntiTiled((int)m.metallicRoughnessTextures.x, m.metallicRoughnessTextures.y > 0, m,
                    MetallicRoughnessTextureRole, uv, uvDx, uvDy, tile);
            else
                mr = SampleTerrainLayerMap((int)m.metallicRoughnessTextures.x, m.metallicRoughnessTextures.y > 0, m,
                    MetallicRoughnessTextureRole, uv, uvDx, uvDy, biplanar);
            metallic *= mr.b;
            roughness *= mr.g;
        }
        float3 normal = normalW;
        if (m.normalTextures.x >= 0)
        {
            // Each projection's normal in its own frame, blended in world space (see Tree.hlsl ShadeSurface).
            normal = 0;
            const uint projections = m.biplanarParams.x > 0 ? 2 : 1;
            for (uint p = 0; p < projections; ++p)
            {
                const float projectionWeight = m.biplanarParams.x > 0 ? biplanar.weight[p] : 1;
                if (projectionWeight <= 0)
                    continue;
                const uint axis = m.biplanarParams.x > 0 ? biplanar.axis[p] : 1;
                const float2 projectionUv = m.biplanarParams.x > 0 ? biplanar.uv[p] : uv;
                const float2 projectionDx = m.biplanarParams.x > 0 ? biplanar.uvDx[p] : uvDx;
                const float2 projectionDy = m.biplanarParams.x > 0 ? biplanar.uvDy[p] : uvDy;
                float4 normalSample;
                if (antiTiled)
                    normalSample = SampleTerrainAntiTiled((int)m.normalTextures.x, m.normalTextures.z > 0, m, NormalTextureRole,
                        projectionUv, projectionDx, projectionDy, tile);
                else
                    normalSample = SampleTerrainTexture((int)m.normalTextures.x, m.normalTextures.z > 0,
                        TransformMaterialUv(m, projectionUv, NormalTextureRole), TransformMaterialUvGradient(m, projectionDx, NormalTextureRole),
                        TransformMaterialUvGradient(m, projectionDy, NormalTextureRole));
                float3 tangentNormal = normalSample.xyz * 2 - 1;
                tangentNormal.xy *= m.normalTextures.y;
                float3 tangent, bitangent;
                if (axis == 1)
                {
                    tangent = tangentW;
                    bitangent = bitangentW;
                    TransformNormalBasis(m, tangent, bitangent);
                }
                else
                    BiplanarSideFrame(m, axis, normalW, normalW, float3(0, 1, 0), tangent, bitangent);
                normal += projectionWeight * normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normalW);
            }
            normal = normalize(normal);
        }

        surface.Diffuse += weight * m.Diffuse * color;
        surface.Ambient += weight * m.Ambient * color;
        surface.specularParams.y += weight * metallic;
        surface.specularParams.z += weight * roughness;
        normalSum += weight * normal;
    }
    shadingNormal = normalize(normalSum);
    // The drift, once over the blended layers; the result is marked done, so the shader's own drift for other
    // surfaces doesn't apply it again.
    const float4 drift = AntiTileDrift(base, positionW);
    surface.Diffuse.rgb *= drift.rgb;
    surface.Ambient.rgb *= drift.rgb;
    surface.specularParams.z *= drift.a;
    surface.tintTextures.zw = 0;
    return surface;
}

#endif
