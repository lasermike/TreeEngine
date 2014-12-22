#pragma once
#include "CommonStuff.h"
#include <vector>
#include "TreeGeometry.h"
#include "WorldObject.h"
#include "TreeModel.h"

typedef long HRESULT;

class TreeModelGenerator;
struct Branch;

class Tree : public WorldObject
{
private:

	//TreeGeometry*	_geometry;
	TreeModel* _treeModel;
	//TreeModel* GetTreeModel() { return (TreeModel*)_model; }

	std::vector<InstancedData>			_logInstanceData;
	std::vector<InstancedData>			_twigInstanceData;

	HRESULT ComputeBranchIndirect(int& currentBranch, XMMATRIX const* world, Branch const* branch, const FXMVECTOR parentStart, float time);
	HRESULT ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMMATRIX const* world, const FXMVECTOR parentStart);


public:
	Tree(void);
	virtual ~Tree(void);

	virtual void Create(ModelGenerator* generator) { return Create((TreeModelGenerator*)generator); }
	void Create(TreeModelGenerator* generator);

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();
	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float time);

	//HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometyData, int startInstance);

	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView);
	virtual unsigned int GetNumInstances(bool numMax);
};

