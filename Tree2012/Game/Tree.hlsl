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
SamplerState samLinear : register( s0 );

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
};

cbuffer cbChangesPerObject : register (b3)
{
	Material mat;
}

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
};


//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
	float4x4 world = input.World  ;
    [flatten]
	if (mat.flags.x > 0) //useShadow
	{
		world = mat.shadowMatrix * world ;
	}

	PS_INPUT output = (PS_INPUT)0;
	output.PosW = mul(float4(input.Pos, 1.0f), transpose(world)).xyz;
	output.NormalW = mul(input.NormalL, (float3x3)input.WorldNormal); // TEMP, use gWorldInvTranspose);

	output.Pos = mul(float4(output.PosW, 1.0f), View);
	output.Pos = mul(output.Pos, Projection);
	output.Tex = input.Tex;

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

	// Sum the light contribution from each light source.  
	//[unroll]
	//for (int i = 0; i < gLightCount; ++i)
	//{
		float4 A, D, S;
		ComputeDirectionalLight(mat, textureColor, light /*gDirLights[i]*/, pin.NormalW, toEye,
			A, D, S);

		ambient += A;
		diffuse += D;
		spec += S;
	//}

	float4 litColor = ambient + diffuse + spec;

	// Common to take alpha from diffuse material.
	litColor.a = mat.Diffuse.a;

	return litColor;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS3( VS_INPUT input )
{
	float4x4 world = input.World;
	if (mat.flags.x > 0)  //useShadow
	{
		world *= mat.shadowMatrix;
	}

    PS_INPUT output = (PS_INPUT)0;
	output.PosW = mul(float4(input.Pos, 1.0f), world).xyz;;
	output.NormalW = mul(input.NormalL, (float3x3)input.WorldNormal); // TEMP, use gWorldInvTranspose);
	
	output.Pos = mul(float4(output.PosW, 1.0f), View);
	output.Pos = mul(output.Pos, Projection);
    output.Tex = input.Tex;

    return output;
}

float4 PS2( PS_INPUT input) : SV_Target
{
    return txDiffuse.Sample( samLinear, input.Tex ) ;
}
