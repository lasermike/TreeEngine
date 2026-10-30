
struct InstancedData
{
    float4x4 World;
    uint InstanceOffset;
    uint InstanceOffsetPrev;
    uint InstanceOffsetNext;
};

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
};

cbuffer cbChangesPerPass : register(b1)
{
    matrix View;
    matrix Projection;
    matrix InverseViewProjection;
};

cbuffer cbChangesEveryFrame : register(b2)
{
    DirectionalLight light;
    float4 eyePos;
    matrix shadowMatrix;
    uint globalFlags;  // bit 0 = use shadow maps
    int numDirectionalLights;
    int numPointLights;
    float environmentRotationCos;
    float environmentRotationSin;
    float ambientOcclusionRadius;
    float ambientOcclusionStrength;
    float ambientOcclusionPadding;
};

float3 RotateEnvironmentDirection(float3 direction)
{
    return float3(
        environmentRotationCos * direction.x + environmentRotationSin * direction.z,
        direction.y,
        -environmentRotationSin * direction.x + environmentRotationCos * direction.z);
}

