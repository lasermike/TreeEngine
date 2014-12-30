#pragma once
#include "pch.h"
#include "Geometry.h"
#include "Model.h"

// For rendering indirectly
struct InstancedData
{
	XMFLOAT4X4 World;
	XMFLOAT4X4 WorldNormal;
};

class WorldObject
{
protected:

	Geometry*	_geometry;

	bool		_drawInstanced;

public:
	WorldObject(void);
	~WorldObject(void);

	virtual void Create(ModelGenerator* generator) { }

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();

	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time);

	HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometyData, int startInstance)
	{
		HR(_geometry->DrawInstanced(pImmediateContext, world, eyePos, t, pGeometyData, startInstance, GetNumInstances(false)));
		return S_OK;
	}

	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView);
	virtual unsigned int GetNumInstances(bool /*numMax*/) { return 0; }
};

