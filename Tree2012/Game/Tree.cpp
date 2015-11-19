#include "pch.h"
#include "Tree.h"
#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "RenderManager.h"
#include "MathHelper.h"
#include <directxcolors.h>

using namespace DirectX;

Tree::Tree(WorldObjectParams* pParams) : _treeModel(nullptr), WorldObject(pParams)
{
}

Tree::~Tree(void)
{
	SafeDelete(&_treeModel);
}

void Tree::Create(TreeModelGenerator* generator)
{
	_treeModel = generator->Create();
	if (_params->_animationSpeed == 0.0f)
	{
		_params->_animationSpeed = _treeModel->treeData.numLevels / 10.0f; 
	}
}

HRESULT Tree::InitGraphics(RenderManager& renderManager)
{
	HRR(CleanUpDeviceObjects());

	ShaderMaterial trunkMaterial;
	//XMStoreFloat4(&_trunkMaterial.Diffuse, Colors::RosyBrown); 
	trunkMaterial.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	trunkMaterial.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	trunkMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
	trunkMaterial.flags.y = false; //useTextures  TODO

	// Create material, mesh, and reserve render unit
	Material* pTrunk = nullptr;
	renderManager.CreateMaterial(L"trunk", L"Bark_0005_diffuse.dds", nullptr, nullptr, trunkMaterial, &pTrunk);
	Mesh* pNewMesh = nullptr;
	const GeometryBufferData::BufferIndices* pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Cylinder);
	renderManager.CreateMesh(L"trunk", renderManager.GetVertexBuffer(), renderManager.GetIndexBuffer(), pBufferIndices, &pNewMesh);
	renderManager.ReserveRenderUnit(pTrunk, pNewMesh, this, &m_logUnit);

	Material* pTwig = nullptr;
	renderManager.CreateMaterial(L"twig", L"Bark_0005_diffuse.dds", nullptr, nullptr, trunkMaterial, &pTwig);
	pNewMesh = nullptr;
	pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Box);
	renderManager.CreateMesh(L"twig", renderManager.GetVertexBuffer(), renderManager.GetIndexBuffer(), pBufferIndices, &pNewMesh);
	renderManager.ReserveRenderUnit(pTwig, pNewMesh, this, &m_twigUnit);

	ShaderMaterial leafMaterial;
	XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
	leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
	leafMaterial.flags.y = false; //useTextures  TODO

	Material* pLeaf = nullptr;
	renderManager.CreateMaterial(L"leaf", L"", nullptr, nullptr, leafMaterial, &pLeaf);
	pNewMesh = nullptr;
	pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Box);
	renderManager.CreateMesh(L"leaf", renderManager.GetVertexBuffer(), renderManager.GetIndexBuffer(), pBufferIndices, &pNewMesh);
	renderManager.ReserveRenderUnit(pLeaf, pNewMesh, this, &m_leafUnit);

	return S_OK;
}

HRESULT Tree::ComputeConstants(IRenderFrame* pFrameConfig)
{
	UINT startInstance = 0;
	HRR(pFrameConfig->GetInstanceIndex(this, startInstance));

	InstancedData* dataView = pFrameConfig->GetRenderData().instanceData;

	// TODO handle scale and rotation of worldobject someday if needed

	// Clear bounding box
	_boundingBox[0] = XMFLOAT3(-1,-1,-1);
	_boundingBox[1] = XMFLOAT3(1,1,1);

	for (int i = 0; i < NUM_EXTENTS; i++)
	{
		_extents[i] = XMFLOAT3(0,0,0);
	}

	_logInstanceData.clear();
	_twigInstanceData.clear();
	_leafInstanceData.clear();
	int currentBranch = 0;

	XMVECTOR startPosition = XMLoadFloat3(&_position); 

	ComputeBranchInstanceData(&pFrameConfig->GetRenderData(), currentBranch, _treeModel->trunk, startPosition);

	InstancedData* logBuffer = dataView + startInstance;
	InstancedData* twigBuffer = dataView + startInstance + _logInstanceData.size();
	InstancedData* leafBuffer = dataView + startInstance + _logInstanceData.size() + _twigInstanceData.size();

	// TODO add to render unit specific data view
	if (_logInstanceData.size())
	{
		memcpy(logBuffer, &_logInstanceData[0], _logInstanceData.size() * sizeof(InstancedData));
	}

	if (_twigInstanceData.size() > 0)
	{
		memcpy(twigBuffer, &_twigInstanceData[0], _twigInstanceData.size() * sizeof(InstancedData));
	}

	if (_leafInstanceData.size() > 0)
	{
		memcpy(leafBuffer, &_leafInstanceData[0], _leafInstanceData.size() * sizeof(InstancedData));
	}

	pFrameConfig->SetInstances(m_logUnit, this, startInstance, (UINT) _logInstanceData.size());
	pFrameConfig->SetInstances(m_twigUnit, this, startInstance + (UINT) _logInstanceData.size(), (UINT)_twigInstanceData.size());
	pFrameConfig->SetInstances(m_leafUnit, this, startInstance + (UINT) _logInstanceData.size() + (UINT) _twigInstanceData.size(), (UINT)_leafInstanceData.size());
	
	return S_OK;
}

HRESULT Tree::ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, const FXMVECTOR parentStart)
{
	if (CalcTime(pRenderData->time) < branch->depth)
		return S_OK;

	pRenderData->frameStats[WORLD_MATRIX_COMPUTED_STAT].stat++;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	XMMATRIX localToWorld;
	ComputeTransformationsManual(&localToWorld, &vChildStart, CalcTime(pRenderData->time), branch, &pRenderData->world, parentStart);

	InstancedData data;
	XMStoreFloat4x4(&data.World, localToWorld);

	// Decide which geometry model to use
	switch (branch->geometryType)
	{
		case Leaf:
			_leafInstanceData.push_back(data);
			pRenderData->frameStats[NUM_LEAVES_STAT].stat++;
			break;
		case Stick:
			if (_params->depthLOD != -1 && (branch->depth < _params->depthLOD || XMVectorGetX(XMVector3LengthSq(parentStart - pRenderData->eyePos)) < 100.0f))
			{
				_logInstanceData.push_back(data);
			}
			else
			{
				_twigInstanceData.push_back(data);
			}
			pRenderData->frameStats[NUM_STICKS_STAT].stat++;

			break;
	}
	//Compute bounding box
	XMStoreFloat3(&_boundingBox[0], XMVectorMin(XMVector3Transform(XMVectorSet(-1.0f,-1.0f,-1.0f, 0), localToWorld),
												XMLoadFloat3(&_boundingBox[0]))); 
	XMStoreFloat3(&_boundingBox[1], XMVectorMax(XMVector3Transform(XMVectorSet(1.0f,1.0f,1.0f, 0), localToWorld),
												XMLoadFloat3(&_boundingBox[1]))); 

	currentBranch++;

	// Compute child branches
	for (unsigned int c = 0; c < branch->children.size(); c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_treeModel->treeData.pBranches[branch->Child(c)];
			ComputeBranchInstanceData(pRenderData, currentBranch, child, vChildStart);
		}
	}

	return S_OK;
}

HRESULT Tree::ComputeTransformationsManual(XMMATRIX* computedTransform, XMVECTOR* vComputedEnd, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart)
{
	float animScaleFactor = 1.0f;
	if (time - 5 < branch->depth)
	{
		animScaleFactor = (time - branch->depth) / 5;
	}
	else
	{
		animScaleFactor  = animScaleFactor ;
	}

	ASSERT(animScaleFactor >= 0.0f);

	XMVECTOR vStart = parentStart;
	//XMVECTOR vStart = XMLoadFloat3((XMFLOAT3*)&(branch->start)) + XMLoadFloat3(&_position);
	XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end)) + XMLoadFloat3(&_position);

	// Scale branch
	XMVECTOR vMag = XMVector3Length(vEnd - vStart);
	XMVECTOR vScale;
	
	switch (branch->geometryType)
	{
	case Leaf:
		vScale = XMVectorSet(XMVectorGetX(vMag) * 0.5f, XMVectorGetX(vMag), 0.004f, 0) * animScaleFactor;
		break;
	case Stick:
		vScale = XMVectorSet(branch->thickness, XMVectorGetX(vMag), branch->thickness, 0) * animScaleFactor;
		break;
	}

	// Child start pos
	XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
	*vComputedEnd = (vEnd - vStart) * vMagY + vStart;

	// Determine rotation
	XMMATRIX mRot;
	XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
	XMVECTOR vLeft = XMVectorSet(1, 0, 0, 0);
	XMVECTOR vDir = XMVector3Normalize(vEnd - vStart);
	XMVECTOR vCross = XMVector3Cross(vUp, vDir);
	XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
	XMVECTOR vQuat;
	float crossLenSq;
	XMStoreFloat(&crossLenSq, vCrossLenSq);
	if (crossLenSq > 0.001f) // Need better value for epsilon here
	{
		XMVECTOR vDot = XMVector3Dot(vUp, vDir);
		float angle;
		XMStoreFloat(&angle, vDot);
		angle = acos(angle);
		vQuat = XMQuaternionRotationAxis(vCross, angle);
	}
	else if (XMVectorGetY(vDir) < -0.99f)
	{ 
		vQuat = XMQuaternionRotationAxis(vLeft, XM_PI);
	}
	else 
	{
		vQuat = XMQuaternionRotationAxis(vUp, 0);
	}

	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0);
	const XMVECTOR vScaleCenter = XMVectorSet(0, -0.5, 0, 0);
	*computedTransform = MatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	*computedTransform = *computedTransform * XMLoadFloat4x4(world);  //TODO

	// Compute extents.  Keep these in local coordinates if we can.
	if (XMVectorGetY(vEnd) > _extents[TOP].y)
		XMStoreFloat3(&_extents[TOP], vEnd);

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
	//XMVECTOR vStart = XMLoadFloat3((XMFLOAT3*)&(branch->start)) + XMLoadFloat3(&_position);
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

unsigned int Tree::GetMaxInstances()
{ 
	if (_treeModel)
	{
        return _treeModel->treeData.numBranches;
	}
	else
		return 0;

}

unsigned int Tree::GetNumInstances()
{ 
	if (_treeModel)
	{
		assert((UINT)_treeModel->treeData.numBranches >= _logInstanceData.size() + _twigInstanceData.size() + _leafInstanceData.size());
        return (unsigned int) (_logInstanceData.size() + _twigInstanceData.size() + _leafInstanceData.size());
	}
	else
		return 0;
}
