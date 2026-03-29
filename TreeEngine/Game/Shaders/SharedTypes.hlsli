
struct InstancedData
{
    float4x4 World;
    uint InstanceOffset;
    uint InstanceOffsetPrev;
    uint InstanceOffsetNext;
};

struct DxrGeometryInfo
{
    uint vertexBufferOffset;
    uint indexBufferOffset;
    uint vertexCount;
    uint baseVertexLocation;    // offset into original vertex buffer for normals/UVs
    int  textureIndex;          // index into DXR texture array (-1 = no texture)
    uint isSkinned;             // 1 if skinned vertex buffer, 0 if simple
    uint instanceIndex;         // index into instance buffer for world matrix
    uint pad;
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
};

