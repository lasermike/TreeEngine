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
SamplerState samShadow : register( s1 );

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

SamplerComparisonState samShadowCompState
{
	Filter   = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	AddressU = BORDER;
	AddressV = BORDER;
	AddressW = BORDER;
	BorderColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

    ComparisonFunc = LESS;
};


//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos : POSITION;
	float3 NormalL : NORMAL;
	float2 Tex : TEXCOORD0;
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
};


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
	float4x4 world = input.World;
    /*[flatten]
	if (mat.flags.x > 0) //use shear Shadow
	{
		world = world  * mat.shadowMatrix ;
	}*/

	PS_INPUT output = (PS_INPUT)0;
	output.PosW = mul(float4(input.Pos, 1.0f), transpose(world)).xyz;
	output.NormalW = mul(input.NormalL, (float3x3)input.WorldNormal);;

	output.Pos = mul(float4(output.PosW, 1.0f), View);
	output.Pos = mul(output.Pos, Projection);
	output.Tex = input.Tex;

	// Generate projective tex-coords to project shadow map onto scene.
	//float4x4 shadowTransform = mul(transpose(world), shadowMatrix);
	output.ShadowPosH = mul(float4(output.PosW, 1.0), transpose(shadowMatrix));

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


	// Sum the light contribution from each light source.  
	//[unroll]
	//for (int i = 0; i < gLightCount; ++i)
	//{
		float4 A, D, S;
		ComputeDirectionalLight(mat, textureColor, light /*gDirLights[i]*/, pin.NormalW, toEye,
			A, D, S);

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

	//float4x4 worldViewProj = transpose(output.World) * View * Projection;

	float4 pos = mul(float4(input.Pos, 1.0f), transpose( input.World));
	pos = mul(pos, View);
	output.PosH = mul(pos, Projection);
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