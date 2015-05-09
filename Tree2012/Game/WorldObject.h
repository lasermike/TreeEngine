#pragma once
#include "pch.h"
#include "Geometry.h"
#include "Model.h"
#include "RenderData.h"
#include <memory>

// For rendering indirectly
struct InstancedData
{
	XMFLOAT4X4 World;
	//XMFLOAT4X4 WorldNormal;
};

typedef enum GeneratorType
{
	NullGeneratorType,
	PrimitiveGeneratorType,
	FixedTreeGeneratorType,
	LSystemGeneratorType
};

typedef enum ObjectType
{
	WorldObjectType,
	TreeType
};

typedef enum Extent
{
	TOP = 0,
	LEFT,
	RIGHT,
	BOTTOM,
	NUM_EXTENTS
};

class WorldObjectParams
{
public:
	WorldObjectParams(GeneratorType genType) :
		position(0,0,0),
		scale(1,1,1), 
		depthLOD(4),
		generatorType(genType), 
		_animationSpeed(1.0f),
		primitiveType(PrimitiveType_Box) 
	{
		XMStoreFloat4(&rotation, XMQuaternionIdentity());
	}

	virtual ~WorldObjectParams() { }

	XMFLOAT3 position;
	XMFLOAT3 scale;
	XMFLOAT4 rotation; 
	GeneratorType generatorType;
	float _animationSpeed;
	int depthLOD;
	PrimitiveType primitiveType;
};

template<typename T>
class WorldObjectParameters : public WorldObjectParams
{
private:
    T generatorParameters;

public:
	WorldObjectParameters(GeneratorType genType) : WorldObjectParams(genType) { }

	T& GetGeneratorParameters() 
	{
		return generatorParameters;
	}
};

class WorldObject
{
protected:

	Geometry*	_geometry;

	bool		_drawInstanced;

	unique_ptr<WorldObjectParams> _params;
	XMFLOAT3	_position;
	XMFLOAT3	_scale;

	XMFLOAT3	_boundingBox[2];
	XMFLOAT3	_extents[4];

	float CalcTime(float time) { return time * _params->_animationSpeed; } 
public:

	WorldObject(WorldObjectParams* pParams);
	~WorldObject(void);

	virtual ObjectType GetObjectType() { return WorldObjectType; }
	WorldObjectParams& GetParams() { return *_params; }
	template <class T> WorldObjectParameters<T>& GetParams() { return *(WorldObjectParameters<T>*)_params.get(); }

	virtual void Create(ModelGenerator* /*generator*/) { }

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();

	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData);

	virtual HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, GeometryBufferData* pGeometyData, int startInstance)
	{
		HRR(_geometry->DrawInstanced(pImmediateContext, pRenderData, pGeometyData, startInstance, GetNumInstances(false)));
		return S_OK;
	}

	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, InstancedData* dataView);
	virtual unsigned int GetNumInstances(bool /*numMax*/) { return 0; }
	XMFLOAT3* GetBoundingBox() { return _boundingBox; }
	XMVECTOR GetExtents(Extent extent);
};

