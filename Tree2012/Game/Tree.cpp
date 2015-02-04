#include "pch.h"
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

HRESULT Tree::ComputeConstants(ID3D11DeviceContext* /*pImmediateContext*/, RenderData* pRenderData, InstancedData* dataView)
{
	_logInstanceData.clear();
	_twigInstanceData.clear();
	int currentBranch = 0;

	XMVECTOR startPosition = XMLoadFloat3(&_position) + XMVectorSet(0, .5, 0, 0);

	ComputeBranchInstanceData(pRenderData, currentBranch, _treeModel->trunk, startPosition);

	/* TODO Implement per-branch depth world matrix as a step toward moving this computation to the GPU
	for (int level = 0; level < _treeModel->treeData.numLevels; level++)
	{
		for (int branchIndex = 0; branchIndex < _treeModel->treeData.pLevels[level].numBranches; branchIndex++)
		{
			Branch* branch = _treeModel->treeData.pBranches[_treeModel->treeData.pLevels[level].pBranchesInLevel[branchIndex]];
			XMVECTOR position = (branch->parent == -1) ? startPosition : _logInstanceData[branch->parent].World;
			ComputeBranchInstanceData(currentBranch, world, _treeModel->trunk, position, time);
		}
	}*/

	int dvi = 0;
	for (unsigned int i = 0; i < _logInstanceData.size(); i++)
	{
		dataView[dvi++] = _logInstanceData[i];
	}

	for (unsigned int i = 0; i < _twigInstanceData.size(); i++)
	{
		dataView[dvi++] = _twigInstanceData[i];
	}
	assert(dvi == currentBranch);

	return S_OK;
}

HRESULT Tree::ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, const FXMVECTOR parentStart)
{
	if (pRenderData->time < branch->depth)
		return S_OK;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	XMMATRIX localToWorld, normalLocalToWorld;
	ComputeTransformationsManual(&localToWorld, &normalLocalToWorld, &vChildStart, pRenderData->time, branch, &pRenderData->world, parentStart);

	InstancedData data;
	XMStoreFloat4x4(&data.World, localToWorld);
	XMStoreFloat4x4(&data.WorldNormal, normalLocalToWorld);

	if (branch->depth < 2)
	{
		_logInstanceData.push_back(data);
	}
	else
	{
		_twigInstanceData.push_back(data);
	}

	currentBranch++;

	// Compute child branches
	const int maxChildren = 4;
	for (int c = 0; c < maxChildren; c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_treeModel->treeData.pBranches[branch->Child(c)];
			ComputeBranchInstanceData(pRenderData, currentBranch, child, vChildStart);
		}
	}

	return S_OK;
}



HRESULT Tree::ComputeTransformationsManual(XMMATRIX* computedTransform, XMMATRIX* computedNormalTransform, XMVECTOR* vComputedEnd, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart)
{
	float animScaleFactor = 1.0f;
	if (time - 5 < branch->depth)
	{
		animScaleFactor = (time - branch->depth) / 5;
	}

	XMVECTOR vStart = parentStart;
	XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end)) + XMLoadFloat3(&_position);

	// Scale branch
	XMVECTOR vMag = XMVector3Length(vEnd - vStart);
	XMVECTOR vScale = XMVectorSet(branch->thickness, XMVectorGetX(vMag), branch->thickness, 0) * animScaleFactor;

	// Child start pos
	XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
	*vComputedEnd = (vEnd - vStart) * vMagY + vStart;

	// Determine rotation
	XMMATRIX mRot;
	XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
	XMVECTOR vDir = vEnd - vStart;
	XMVECTOR vCross = XMVector3Cross(vUp, XMVector3Normalize(vDir));
	XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
	XMVECTOR vQuat;
	float crossLenSq;
	XMStoreFloat(&crossLenSq, vCrossLenSq);
	if (crossLenSq > 0.01f) // Need better value for epsilon here
	{
		XMVECTOR vDot = XMVector3Dot(vUp, XMVector3Normalize(vDir));
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
	*computedTransform = MatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	*computedTransform = *computedTransform * XMLoadFloat4x4(world);  //TODO
	*computedNormalTransform = MathHelper::InverseTranspose(XMMatrixTranspose(*computedTransform ));

	return S_OK;
}

HRESULT Tree::ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart)
{
	float animScaleFactor = 1.0f;
	if (time - 5 < branch->depth)
	{
		animScaleFactor = (time - branch->depth) / 5;
	}

	XMVECTOR vStart = parentStart;
	XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end)) + XMLoadFloat3(&_position);

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

	*transform = *transform * XMLoadFloat4x4(world);  //TODO
	*normalTransform = MathHelper::InverseTranspose(XMMatrixTranspose(*transform));

	return S_OK;
}

unsigned int Tree::GetNumInstances(bool numMax)
{ 
	if (_treeModel)
	{
		if (numMax)
			return _treeModel->treeData.numBranches;
		else
			return (unsigned int) (_logInstanceData.size() + _twigInstanceData.size());
	}
	else
		return 0;
}

