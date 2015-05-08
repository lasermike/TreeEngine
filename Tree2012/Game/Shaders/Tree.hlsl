//--------------------------------------------------------------------------------------
// File: Tutorial07.fx
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Materials.fx"

//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
Texture2D txDiffuse : register( t0 );
Texture2D txShadowMap : register( t1 );
SamplerState samLinear : register( s0 );
SamplerComparisonState samShadowCompState  : register( s1 );

cbuffer cbNeverChanges : register( b0 )
{
    matrix View;
	Material groundMaterial;
};

cbuffer cbChangeOnResize : register( b1 )
{
    matrix Projection;
};

cbuffer cbChangesEveryFrame : register( b2 )
{
	DirectionalLight light;
	float3 eyePos;
	matrix worldToCamera;
	matrix shadowMatrix;
};

cbuffer cbChangesPerObject : register (b3)
{
	Material mat;
	float4x4 texTransform;
}

//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex : TEXCOORD0;
	float3 TangentL : TANGENT;
	float4x4 World  : WORLD;
	float4x4 WorldNormal  : WORLDNORMAL;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
	float3 PosW : POSITION;
	float2 Tex : TEXCOORD0;
	float3 NormalW : NORMAL;
	float4 ShadowPosH : TEXCOORD1;
	float3 ViewDirection : NORMAL1;
	float3 T : TEXCOORD3;
	float3 B : TEXCOORD4;
	float3 N : TEXCOORD5;
	//float3 lightDirTang : NORMAL2;
};

//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
float3x3 WorldToTangentSpace(float3 unitNormalL, float3 tangentL, float3x3 world)
{
	// Uncompress each component from [0,1] to [-1,1].
	//float3 normalT = 2.0f*normalMapSample - 1.0f;

	// Build orthonormal basis.
	float3 N = unitNormalL;
	float3 T = normalize(tangentL - dot(tangentL, N)*N);
	float3 B = cross(N, T);

	float3x3 TBN = float3x3(T, B, N);

	return TBN;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
	PS_INPUT output = (PS_INPUT)0;
	output.PosW = mul(float4(input.Pos, 1.0f), input.World).xyz;
	output.NormalW = mul(input.NormalL, (float3x3)input.WorldNormal);

	output.Pos = mul(float4(output.PosW, 1.0f), transpose(View));
	output.Pos = mul(output.Pos, transpose(Projection));
	output.Tex = input.Tex;

	// View direction.  Calcuate here and have it interpolated by to the pixel shader
	output.ViewDirection = output.PosW - eyePos;

	// TBN vectors for tangent space
	float3 worldNormal = mul( input.NormalL, (float3x3) input.World );
	output.N = normalize( worldNormal );

	float3 worldTangent = mul( input.TangentL, (float3x3) input.World );
	output.T = normalize(worldTangent);

	float3 worldBinormal = normalize(cross(output.N, output.T));
	worldBinormal =	mul( worldBinormal, (float3x3) input.World );
	output.B = normalize( worldBinormal );

	// Generate projective tex-coords to project shadow map onto scene.
	output.ShadowPosH = mul(float4(output.PosW, 1.0), shadowMatrix);
	 
	return output;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS(PS_INPUT pin) : SV_Target
{
	// Interpolating normal can unnormalize it, so normalize it.
	pin.NormalW = normalize(pin.NormalW);

	// The toEye vector is used in lighting.
	float3 toEye = eyePos - pin.PosW;

	// Cache the distance to the eye from this surface point.
	float distToEye = length(toEye);

	// Normalize.
	toEye /= distToEye;

	float3x3 TBN = float3x3( normalize(pin.T), normalize(pin.B), normalize(pin.N) ); //transforms world=>tangent space
	float3 normal = mul( float3(0,0,1), TBN);
	//float3 normal = pin.NormalW;

	// Lighting.

	// Start with a sum of zero. 
	float4 ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 spec = float4(0.0f, 0.0f, 0.0f, 0.0f);

	float4 textureColor;
	if (mat.flags.y > 0)  //use texture
		textureColor = txDiffuse.Sample(samLinear, pin.Tex);
	else
		textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

	// Only the first light casts a shadow.
	float3 shadow = float3(1.0f, 1.0f, 1.0f);
	shadow[0] = CalcShadowFactor(samShadowCompState, txShadowMap, pin.ShadowPosH);

	//float3 normal = NormalToWorldSpace(pin.NormalL, pin.NormalL, pin.TangentL, pin.World);

	// Sum the light contribution from each light source.  
	//[unroll]
	//for (int i = 0; i < gLightCount; ++i)
	//{
		float4 A, D, S;
		//ComputeDirectionalLight(mat, textureColor, light2 /*gDirLights[i]*/, pin.NormalW, pin.viewDirTang.xyz, A, D, S);
		ComputeDirectionalLight(mat, textureColor, light /*gDirLights[i]*/, normal, toEye, A, D, S);

		ambient += A;
		diffuse += shadow[0]*D;
		spec    += shadow[0]*S;
		//diffuse += D;
		//spec += S;
	//}

	float4 litColor = ambient + diffuse + spec;

	// Common to take alpha from diffuse material.
	litColor.a = mat.Diffuse.a;

	return litColor;
}

struct ShadowMapVertexOut
{
	float4 PosH : SV_POSITION;
	float2 Tex  : TEXCOORD;
};
 

ShadowMapVertexOut BuildShadowMapVS(VS_INPUT input)
{
	ShadowMapVertexOut output;

	float4 pos = mul(float4(input.Pos, 1.0f), input.World);
	pos = mul(pos, transpose(View));
	output.PosH = mul(pos, transpose(Projection));
	output.Tex = input.Tex;


	return output;
}

// This is only used for alpha cut out geometry, so that shadows 
// show up correctly.  Geometry that does not need to sample a
// texture can use a NULL pixel shader for depth pass.
void BuildShadowMapPS(ShadowMapVertexOut pin)
{
	// TODO support alpha map
	//float4 diffuse = gDiffuseMap.Sample(samLinear, pin.Tex);

	// Don't write transparent pixels to the shadow map.
	//clip(diffuse.a - 0.15f);
}

struct DSVertexIn
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex     : TEXCOORD;
};

struct DSVertexOut
{
	float4 PosH : SV_POSITION;
	float2 Tex  : TEXCOORD;
};
 

DSVertexOut DrawScreenQuadVS(DSVertexIn vin)
{
	DSVertexOut vout;

	float4x4 worldViewProj = float4x4(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, -0.5f, 0.0f, 1.0f);

	vout.PosH = mul(float4(vin.PosL, 1.0f), worldViewProj);

	vout.Tex  = vin.Tex;
	
	return vout;
}

float4 DrawScreenQuadPS(DSVertexOut pin) : SV_Target
{
	float4 c = txDiffuse.Sample(samLinear, pin.Tex).r;
	
	// draw as grayscale
	return float4(c.rrr, 1);
}
 


/*
matrix MatrixTransformation
(
    float3 ScalingOrigin, 
    float4 ScalingOrientationQuaternion, 
    float3 Scaling, 
    float3 RotationOrigin, 
    float4 RotationQuaternion, 
    float3 Translation
)
{
    matrix M;
    NegScalingOrigin;
    float3  VScalingOrigin;
    matrix MScalingOriginI;
    matrix MScalingOrientation;
    matrix MScalingOrientationT;
    matrix MScaling;
    float3 VRotationOrigin;
    matrix MRotation;
    float3 VTranslation;

    float3 NegScalingOrigin     = -ScalingOrigin;

    MScalingOriginI      = XMMatrixTranslationFromVector(NegScalingOrigin);
    MScalingOrientation  = XMMatrixRotationQuaternion(ScalingOrientationQuaternion);
    MScalingOrientationT = XMMatrixTranspose(MScalingOrientation);
    MScaling             = XMMatrixScalingFromVector(Scaling);
    VRotationOrigin      = _mm_and_ps(RotationOrigin,g_XMMask3);
    MRotation            = XMMatrixRotationQuaternion(RotationQuaternion);
    VTranslation         = _mm_and_ps(Translation,g_XMMask3);

    M      = XMMatrixMultiply(MScalingOriginI, MScalingOrientationT);
    M      = XMMatrixMultiply(M, MScaling);
    M      = XMMatrixMultiply(M, MScalingOrientation);
    M.r[3] = XMVectorAdd(M.r[3], VScalingOrigin);
    M.r[3] = XMVectorSubtract(M.r[3], VRotationOrigin);
    M      = XMMatrixMultiply(M, MRotation);
    M.r[3] = XMVectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = XMVectorAdd(M.r[3], VTranslation);

    return M;
}
*/

	//float4 vPosTS = WorldToTangentSpace(float4(input.Pos, 1), input.NormalL, input.TangentL, input.World);
	//float4 eyePosTS = WorldToTangentSpace(float4(eyePos, 1), input.NormalL, input.TangentL, input.World);
	//float4 lightPosTS = WorldToTangentSpace(float4(light.Direction, 1), input.NormalL, input.TangentL, input.World);
	//output.viewDirTang = normalize(eyePosTS - vPosTS);
	//output.lightDirTang = normalize(lightPosTS - eyePosTS);
	//
	//float4 eyePosTS4 = output.viewDirTang = WorldToTangentSpace(float4(eyePosTS), 1), input.NormalL, input.TangentL, transpose(input.World) );
	//output.viewDirTang = WorldToTangentSpace(float4(normalize(input.Pos), 1), input.NormalL, input.TangentL, transpose(input.World) );
	//output.lightDirTang = WorldToTangentSpace(float4(light.Direction, 1), input.NormalL, input.TangentL, transpose(input.World));

	//output.viewDirTang = WorldToTangentSpace(float4(normalize(eyePos - input.Pos.xyz), 1), input.NormalL, input.TangentL, transpose(input.World) );
	//output.lightDirTang = WorldToTangentSpace(float4(light.Direction, 1), input.NormalL, input.TangentL, transpose(input.World));
	
