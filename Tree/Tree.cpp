#include "CommonStuff.h"
#include "Tree.h"
#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "MathHelper.h"

Tree::Tree(void) : _treeModel(nullptr), WorldObject()
{
}

Tree::~Tree(void)
{
	if (_treeModel)
	{
		delete _treeModel;
	}
}

void Tree::Create(TreeModelGenerator* generator)
{
	_treeModel = generator->Create();
}

HRESULT Tree::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HR(CleanUpDeviceObjects());
	_geometry = new TreeGeometry(_treeModel);
	HR(_geometry->InitGraphics(device, pImmediateContext));
	return S_OK;
}

HRESULT Tree::ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView)
{
	_logInstanceData.clear();
	_twigInstanceData.clear();
	int currentBranch = 0;

	XMVECTOR vChildStart;
	ComputeBranchInstanceData(currentBranch, world, _treeModel->trunk, XMVectorSet(0, 0, 0, 0), time);

	int dvi = 0;
	for (int i = 0; i < _logInstanceData.size(); i++)
	{
		dataView[dvi++] = _logInstanceData[i];
	}

	for (int i = 0; i < _twigInstanceData.size(); i++)
	{
		dataView[dvi++] = _twigInstanceData[i];
	}
	assert(dvi == currentBranch);

	return S_OK;
}

HRESULT Tree::ComputeBranchInstanceData(int& currentBranch, XMMATRIX const* world, Branch const* branch, const FXMVECTOR parentStart, float time)
{
	if (time < branch->depth)
		return S_OK;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	XMMATRIX localToWorld, normalLocalToWorld;
	ComputeTransformations(&localToWorld, &normalLocalToWorld, &vChildStart, time, branch, world, parentStart);

	InstancedData data;
	XMStoreFloat4x4(&data.World, localToWorld);
	XMStoreFloat4x4(&data.WorldNormal, normalLocalToWorld); //normalLocalToWorld

	InstancedData* pData = nullptr;
	if (branch->depth < 2)
	{
		_logInstanceData.push_back(data);
	}
	else
	{
		_twigInstanceData.push_back(data);
	}

	currentBranch++;

	// Render child branches
	const int maxChildren = 4;
	for (int c = 0; c < maxChildren; c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_treeModel->pBranches[branch->Child(c)];
			ComputeBranchInstanceData(currentBranch, world, child, vChildStart, time);
		}
	}

	return S_OK;
}

HRESULT Tree::ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMMATRIX const* world, FXMVECTOR parentStart)
{
	float animScaleFactor = 1.0f;
	if (time - 5 < branch->depth)
	{
		animScaleFactor = (time - branch->depth) / 5;
	}

	XMVECTOR vStart = parentStart;
	XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end));

	// Scale branch
	XMVECTOR vMag = XMVector3Length(vEnd - vStart);
	float magY = XMVectorGetX(vMag);
	float magXZ = branch->thickness;
	magY *= animScaleFactor;
	magXZ *= animScaleFactor;
	XMVECTOR vScale = XMVectorSet(magXZ, magY, magXZ, 0);

	// Child start pos
	XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
	*vChildStart = (vEnd - vStart) * vMagY + vStart;

	// Determine rotation
	XMMATRIX mRot;
	XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
	XMVECTOR vDiff = vEnd - vStart;
	XMVECTOR vCross = XMVector3Cross(vUp, XMVector3Normalize(vDiff));
	XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
	XMVECTOR vQuat;
	float crossLenSq;
	XMStoreFloat(&crossLenSq, vCrossLenSq);
	if (crossLenSq > 0.01f) // Need better value for epsilon here
	{
		XMVECTOR vDot = XMVector3Dot(vUp, XMVector3Normalize(vDiff));
		float angle;
		XMStoreFloat(&angle, vDot);
		angle = acos(angle);
		vQuat = XMQuaternionRotationAxis(vCross, angle);
	}
	else
	{
		vQuat = XMQuaternionRotationAxis(vUp, 0);
	}

	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0);
	const XMVECTOR vScaleCenter = XMVectorSet(0, -0.5, 0, 0);
	*transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	//*transform = XMMatrixTranspose(*transform * *world);
	//*normalTransform = MathHelper::InverseTranspose(*transform);

	*transform = *transform * *world;  //TODO
	*normalTransform = MathHelper::InverseTranspose(XMMatrixTranspose(*transform));


	//*normalTransform = *transform;

	return S_OK;
}

unsigned int Tree::GetNumInstances(bool numMax)
{ 
	if (_treeModel)
	{
		if (numMax)
			return _treeModel->treeData.numBranches;
		else
			return _logInstanceData.size() + _twigInstanceData.size();
	}
	else
		return 0;
}

