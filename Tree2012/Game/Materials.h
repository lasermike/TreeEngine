#pragma once

#include "pch.h"
#include <directxmath.h>

using namespace DirectX;

struct DirectionalLight
{
	DirectionalLight() { ZeroMemory(this, sizeof(this)); }

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;
	XMFLOAT3 Direction;
	float Pad; // Pad the last float so we can set an array of lights if we wanted.
};

struct PointLight
{
	PointLight() { ZeroMemory(this, sizeof(this)); }

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular;

	// Packed into 4D vector: (Position, Range)
	XMFLOAT3 Position;
	float Range;

	// Packed into 4D vector: (A0, A1, A2, Pad)
	XMFLOAT3 Att;
	float Pad; // Pad the last float so we can set an array of lights if we wanted.
};


struct ShaderMaterial
{
	ShaderMaterial() 
    { 
        Ambient = Diffuse = Specular = Reflect = XMFLOAT4(0,0,0, 1.0f);
        flags = XMFLOAT4(0,0,0,0);
    }

	XMFLOAT4 Ambient;
	XMFLOAT4 Diffuse;
	XMFLOAT4 Specular; // w = SpecPower
	XMFLOAT4 Reflect;
	XMFLOAT4 flags; // x = n/a, y = useTexture
};

struct CBMaterial
{
    ShaderMaterial material;
};

class Materials
{
public:
	Materials();
	~Materials();
};

