#pragma once
#include "pch.h"
#include <vector>
#include "TreeGeometry.h"
#include "WorldObject.h"
#include "TreeModel.h"
#include "Materials.h"

typedef long HRESULT;

class TreeModelGenerator;
struct Branch;

class Tree : public WorldObject
{
private:

	TreeModel* _treeModel;
	std::vector<InstancedData>			_logInstanceData;
	std::vector<InstancedData>			_twigInstanceData;

	HRESULT ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, const FXMVECTOR parentStart);
	HRESULT ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, const FXMVECTOR parentStart);
	HRESULT ComputeTransformationsManual(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart);

public:
	Tree(WorldObjectParams* pParams);
	virtual ~Tree(void);

	virtual void Create(ModelGenerator* generator) { return Create((TreeModelGenerator*)generator); }
	void Create(TreeModelGenerator* generator);
	virtual ObjectType GetObjectType() { return TreeType; }

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);

	virtual HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, GeometryBufferData* pGeometyData, int startInstance);

	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, InstancedData* dataView) override;
	virtual unsigned int GetNumInstances(bool numMax);
};

