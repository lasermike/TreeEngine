
struct InstancedData
{
    float4x4 World;
    uint InstanceOffset;
    uint InstanceOffsetPrev; // The skinning parent's slot.
    uint AtlasRegion; // One plus the slot whose World[0] holds the atlas rect; zero draws the whole texture.
    uint PreviousSlot; // This instance's slot in last frame's instance buffer, or NoPreviousSlot.
};

static const uint NoPreviousSlot = 0xFFFFFFFF;

// Velocity a pixel holds when nothing drew it with object motion; MotionVectorsCS fills it from the camera.
static const float NoObjectMotion = 65504.0f;

// atlasRect is (u offset, v offset, u scale, v scale).
float2 ApplyAtlasRect(float2 uv, float4 atlasRect)
{
    return atlasRect.xy + uv * atlasRect.zw;
}

// Object-transform slot meaning "no per-draw object transform" (matches UINT_MAX on the CPU).
static const uint NoObjectTransform = 0xFFFFFFFF;

struct DxrGeometryInfo
{
    uint vertexBufferOffset;
    uint indexBufferOffset;
    uint vertexCount;
    uint baseVertexLocation;    // offset into original vertex buffer for normals/UVs
    int  textureIndex;          // index into DXR texture array (-1 = no texture)
    uint isSkinned;             // 1 if skinned vertex buffer, 0 if simple
    uint instanceIndex;         // index into instance buffer for world matrix
    uint objectTransformSlot;   // instance-buffer slot of a per-draw object transform (NoObjectTransform = identity)
    uint trianglesPerInstance;  // A geometry holds a draw record's instances, this many triangles each.
};

cbuffer cbChangesPerPass : register(b1)
{
    matrix View;
    matrix Projection;
    matrix InverseViewProjection;
    matrix ViewProjectionNoJitter;      // Motion vectors: this frame, without jitter.
    matrix PreviousViewProjection;      // Last frame, without jitter.
    uint PreviousInstancesValid;        // PreviousInstanceBuffer holds last frame's instances.
};

cbuffer cbChangesEveryFrame : register(b2)
{
    DirectionalLight light;
    float4 eyePos;
    matrix shadowMatrix;
    uint globalFlags;  // bit 0 = use shadow maps; bit 1 = disable ray-traced shadows; bit 2 = hashed alpha test (raster);
                       // bit 3 = HDR scene color (linear light; tonemapped later)
    int numDirectionalLights;
    int numPointLights;
    float environmentRotationCos;
    float environmentRotationSin;
    float ambientOcclusionRadius;
    float ambientOcclusionStrength;
    uint reflectionRayCount;
    float4 sceneTime; // x is simulation time in seconds
    float4 sunShadowParams; // x: tan(angular radius), y: sample count
    float4 renderScale;     // x: texture LOD bias while upscaling (0 at native), y: 2^x; see Game/Upscaler.h
};

// Hashed alpha testing for raster (Wyman and McGuire, "Hashed Alpha Testing", 2017). Ray tracing samples alpha at
// full resolution and compares it with the material's cutoff: each ray hits with probability equal to the fraction
// of the texels under its pixel that pass. Raster samples minified textures from mipmaps, where averaged alpha
// crosses a fixed cutoff unevenly, so distant foliage thins out or fills in. Once a texture is minified, raster
// instead compares alpha with a random threshold fixed to the surface, which covers a pixel with probability equal
// to its averaged alpha: the same density as ray tracing for foliage, whose alpha is nearly all 0 or 1. Where the
// texture is at full resolution or magnified, raster keeps the exact cutoff, so authored cutout shapes (glTF "mask"
// materials with soft alpha) stay crisp; between mip 0 and mip 1 the threshold blends from one to the other.
// The threshold is hashed from the mesh's own (object-space) position, before instance, skinning and object
// transforms, on a grid about one pixel across: the pattern stays on a leaf as it grows or moves (in world space it
// would re-roll every frame); leaves sharing a mesh share it. The result is speckled where averaged alpha is
// fractional, which temporal anti-aliasing resolves.
uint3 HashPcg3d(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

// LOD crossfade (Tree::ComputeInstanceConstants): a per-pixel threshold in [0, 1) that changes every frame, so TAA
// averages the dithered layouts into a blend. Raster keeps a pixel of the layout fading in below the fade value t, and
// of the one fading out at or above it; ray tracing picks one of seven instance-mask buckets with it (RayFadeMask).
float FadeDither(uint2 pixel)
{
    const uint3 h = HashPcg3d(uint3(pixel, (uint)sceneTime.y));
    return (h.x >> 8) * (1.0f / 16777216.0f);
}

// Whether a pixel of an object with this fade value (0: not fading, +t: fading in, -t: fading out) is drawn.
bool FadeKeeps(float fade, float2 pixelPosition)
{
    if (fade == 0)
        return true;
    const float threshold = FadeDither(uint2(pixelPosition));
    return fade > 0 ? threshold < fade : threshold >= -fade;
}

// The instance-inclusion mask for a ray from this pixel: one of the buckets 1-7 (see RenderPlatform12's LodFadeMask).
uint RayFadeMask(uint2 pixel)
{
    return 1u << (1 + min(6u, (uint)(FadeDither(pixel) * 7.0f)));
}

float HashCell(float3 cell, int scale)
{
    const uint3 h = HashPcg3d(uint3(int3(cell)) + (uint)scale * uint3(0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u));
    return (h.x >> 8) * (1.0f / 16777216.0f);
}

// The threshold at an object-space position whose pixel spans footprint object-space units. Two power-of-two grids around the
// pixel size are blended so the threshold doesn't jump as the camera moves, with the blend remapped to stay uniform.
float HashedAlphaThreshold(float3 position, float footprint)
{
    const float level = log2(1.0f / max(footprint, 1e-7f));
    const float lower = floor(level);
    const float x = lerp(HashCell(floor(exp2(lower) * position), (int)lower),
        HashCell(floor(exp2(lower + 1) * position), (int)lower + 1), level - lower);
    const float a = min(level - lower, 1 - (level - lower));
    float threshold = x;
    if (a > 1e-6f)
    {
        threshold = x < 1 - a
            ? (x < a ? x * x / (2 * a * (1 - a)) : (x - 0.5f * a) / (1 - a))
            : 1 - (1 - x) * (1 - x) / (2 * a * (1 - a));
    }
    return clamp(threshold, 1e-6f, 1.0f);
}

// Whether a raster alpha-tested surface is present. textureLod is the base-color texture's level of detail, and
// position and footprint are in object space.
bool AlphaTestPasses(float alpha, float cutoff, float textureLod, float3 position, float footprint)
{
    if (!(globalFlags & 0x4) || textureLod <= 0)
        return alpha >= cutoff;
    return alpha >= lerp(cutoff, HashedAlphaThreshold(position, footprint), saturate(textureLod));
}

// The scene target holds linear light with HDR (globalFlags bit 3), tonemapped and encoded for display at the end
// of the frame (Tonemap.hlsl). Without HDR it holds display-encoded sRGB, as each shader encodes its own output.
bool HdrSceneColor()
{
    return (globalFlags & 0x8) != 0;
}

// A shaded color as the scene target stores it. Ray-tracing payloads use the same space, so reflections and
// transmission combine in linear light with HDR.
float3 ToSceneColor(float3 linearColor)
{
    return HdrSceneColor() ? max(linearColor, 0) : EncodeGltfDisplayColor(linearColor);
}

// Inputs that are display-referred today (the sky cube map, the clear color) as the scene target stores them.
float3 DisplayToScene(float3 display)
{
    if (!HdrSceneColor())
        return display;
    const float3 value = max(display, 0);
    return select(value <= 0.04045f, value / 12.92f, pow((value + 0.055f) / 1.055f, 2.4f));
}

float3 RotateEnvironmentDirection(float3 direction)
{
    return float3(
        environmentRotationCos * direction.x + environmentRotationSin * direction.z,
        direction.y,
        -environmentRotationSin * direction.x + environmentRotationCos * direction.z);
}

