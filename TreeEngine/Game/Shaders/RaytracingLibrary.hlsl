//--------------------------------------------------------------------------------------
// RaytracingLibrary.hlsl
//
// Single HLSL file containing a Ray Generation, Miss, Any-Hit and Closest Hit Shader
// Compiled as a single /Tlib_6_3 DXIL shader library.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Materials.fx"
#include "SharedTypes.hlsli"

#include "LocalRootSignature.hlsl"

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);

cbuffer Params : register(b0)
{
	uint dispatchWidth;
	uint dispatchHeight;
    uint rayFlags;
    float holeSize;
};

struct RayPayload
{
	float dummy;    // Minimum of 4 bytes required for payloads.
};


// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
}


#if 0
[shader("raygeneration")]
void RayGenerationShader()
{
    float3 rayDir;
    float3 origin;

    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    //GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    float2 xy = DispatchRaysIndex().xy + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), Projection);

    world.xyz /= world.w;
    origin = float3(-6.0f, 1.5f, -6.0); // eyePos; ////g_sceneCB.cameraPosition.xyz;
    rayDir = normalize(world.xyz - origin);


    // Trace the ray.
    // Set the ray's extents.
    //RayDesc ray;
    //ray.Origin = origin;
    //ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    //ray.TMin = 0.001;
    //ray.TMax = 10000.0;

    //TEMPTEMP
    rayDir = float3(0, 0, 1);  //float3(0, 0, 1);

    RayDesc ray = { origin, 0.0f, rayDir, 100.0f };
    RayPayload payload = { 0.0 };
    uint missShaderIndex = 1;
    TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, ray, payload);

    // Write the raytraced color to the output texture.
    //RenderTarget[DispatchRaysIndex().xy] = payload.color;
}
#endif

#if 1
//WIP
[shader("raygeneration")]
void RayGenerationShader()
{
    // Orthographic projection, just as if we were already in NDC.  But this is world coordinates?
    float2 vpos = DispatchRaysIndex().xy;
    //float3 rayOrigin = eyePos; //float3(-1, 1, -4); //float3(-1, 1, -5);
    float3 rayOrigin = float3(-1, 1, -1); //float3(-1, 1, -5);

    rayOrigin.xy += float2(2, -2) * (vpos / float2(dispatchWidth, dispatchHeight));

    //float3 rayDir = float3(0, 0, 1);
    float3 rayDir = float3(0, 0, 1) + float3(float2(1, -1) * (vpos / float2(dispatchWidth, dispatchHeight)), 0);

    RayDesc myRay = { rayOrigin, 0.0f, rayDir, 100.0f };
    RayPayload payload = { 0.0f };

    uint missShaderIndex = 1;
    TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, myRay, payload);
}
#endif


#if 0
// Original orth.  works

[shader("raygeneration")]
void RayGenerationShader()
{
	// Orthographic projection, just as if we were already in NDC.  But this is world coordinates?
	float2 vpos = DispatchRaysIndex().xy;
	float3 rayOrigin = float3(-1, 1, -4); //float3(-1, 1, -5);

	rayOrigin.xy += float2(2, -2) * (vpos / float2(dispatchWidth, dispatchHeight));

	float3 rayDir = float3(0, 0, 1);  //float3(0, 0, 1);

	RayDesc myRay = { rayOrigin, 0.0f, rayDir, 100.0f };
	RayPayload payload = { 0.0f };

	uint missShaderIndex = 1;
	TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, myRay, payload);
}
#endif

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
    float3 triangleCentre = 1.0f / 3.0f;

    float distanceToCentre = length(triangleCentre - barycentrics);

    if (distanceToCentre < holeSize)
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
	renderOutput[DispatchRaysIndex().xy] = float4(barycentrics, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
	renderOutput[DispatchRaysIndex().xy] = float4(0.5, 0, 0, 1);
}
