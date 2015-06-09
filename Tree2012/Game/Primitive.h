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
	RenderUnit* m_renderUnit;

public:
	Primitive(WorldObjectParams* wop);
	~Primitive();
	virtual ObjectType GetObjectType() { return PrimitiveObjectType; }

	virtual void Create(ModelGenerator* generator) { return Create((PrimitiveModelGenerator*)generator); }
	virtual void Create(PrimitiveModelGenerator* generator);
	virtual HRESULT InitGraphics(RenderManager& renderManager);

	virtual HRESULT ComputeConstants(IRenderFrameConfig* pFrameConfig, InstancedData* dataView, UINT startInstance) override;

	virtual unsigned int GetNumInstances() { return 1; }
	virtual unsigned int GetMaxInstances() { return 1; }

};

