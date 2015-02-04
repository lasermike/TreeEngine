#pragma once
#include "WorldObject.h"
#include "Materials.h"

class PrimitiveModel : public Model
{
	PrimitiveType _primitiveType;

public:
	PrimitiveModel(PrimitiveType primitiveType) : _primitiveType(primitiveType)
	{
	}

	PrimitiveType GetPrimitiveType() { return _primitiveType;  }
};

class PrimitiveModelGenerator : public ModelGenerator
{
	PrimitiveType _primitiveType;

public:
	PrimitiveModelGenerator(PrimitiveType primitiveType) : _primitiveType(primitiveType)
	{
	}

	virtual PrimitiveModel* Create()
	{
		return new PrimitiveModel(_primitiveType);
	}
};

class Primitive : public WorldObject
{
	PrimitiveModel*	_model;

public:
	Primitive();
	~Primitive();

	virtual void Create(ModelGenerator* generator) { return Create((PrimitiveModelGenerator*)generator); }
	virtual void Create(PrimitiveModelGenerator* generator);
	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);

	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, InstancedData* dataView) override;

	virtual unsigned int GetNumInstances(bool /*numMax*/) { return 1; }

};

