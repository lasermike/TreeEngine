
struct InstancedData
{
    float4x4 World;
    uint InstanceOffset;
    uint InstanceOffsetPrev;
    uint InstanceOffsetNext;
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

