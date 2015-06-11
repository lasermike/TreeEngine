#include "pch.h"
#include "Tree.h"
#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "RenderManager.h"
#include "MathHelper.h"

Tree::Tree(WorldObjectParams* pParams) : _treeModel(nullptr), WorldObject(pParams)
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
	if (_params->_animationSpeed == 0.0f)
	{
		_params->_animationSpeed = _treeModel->treeData.numLevels / 10.0f; 
	}
}

HRESULT Tree::InitGraphics(RenderManager& renderManager)
{
	HRR(CleanUpDeviceObjects());

	ShaderMaterial trunkMaterial;
	trunkMaterial.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	//XMStoreFloat4(&_trunkMaterial.Diffuse, Colors::RosyBrown); 
	trunkMaterial.Diffuse = XMFLOAT4(1.0f, .7f, .3f, 1.0f);
	trunkMaterial.Specular = XMFLOAT4(.4f, .4f, .4f, 1.0f);
	trunkMaterial.flags.y = false; //true; //useTextures  TODO

	// Create material, mesh, and reserve render unit
	Material* pTrunk = nullptr;
	renderManager.CreateMaterial(L"trunk", L"", trunkMaterial, &pTrunk);
	Mesh* pNewMesh = nullptr;
	const GeometryBufferData::BufferIndices* pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Cylinder);
	renderManager.CreateMesh(L"trunk", renderManager.GetVertexBuffer(), renderManager.GetIndexBuffer(), pBufferIndices, &pNewMesh);
	renderManager.ReserveRenderUnit(pTrunk, pNewMesh, this, &m_logUnit);

	Material* pTwig = nullptr;
	renderManager.CreateMaterial(L"twig", L"", trunkMaterial, &pTwig);
	pNewMesh = nullptr;
	pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Box);
	renderManager.CreateMesh(L"twig", renderManager.GetVertexBuffer(), renderManager.GetIndexBuffer(), pBufferIndices, &pNewMesh);
	renderManager.ReserveRenderUnit(pTwig, pNewMesh, this, &m_twigUnit);

	return S_OK;
}

HRESULT Tree::ComputeConstants(IRenderFrame* pFrameConfig, InstancedData* dataView, UINT startInstance)
{
	// Clear bounding box
	_boundingBox[0] = XMFLOAT3(-1,-1,-1);
	_boundingBox[1] = XMFLOAT3(1,1,1);

	for (int i = 0; i < NUM_EXTENTS; i++)
	{
		_extents[i] = XMFLOAT3(0,0,0);
	}

	_logInstanceData.clear();
	_twigInstanceData.clear();
	int currentBranch = 0;

	XMVECTOR startPosition = XMLoadFloat3(&_position); // + XMVectorSet(0, .5, 0, 0);

	ComputeBranchInstanceData(&pFrameConfig->GetRenderData(), currentBranch, _treeModel->trunk, startPosition);

	InstancedData* logBuffer = dataView + startInstance;
	InstancedData* twigBuffer = dataView  + startInstance + _logInstanceData.size();

	// TODO add to render unit specific data view
	int dvi = 0;
	for (unsigned int i = 0; i < _logInstanceData.size(); i++)
	{
		logBuffer[dvi++] = _logInstanceData[i];
	}

	dvi = 0;
	for (unsigned int i = 0; i < _twigInstanceData.size(); i++)
	{
		twigBuffer[dvi++] = _twigInstanceData[i];
	}

	pFrameConfig->SetInstances(m_logUnit, this, startInstance, (UINT) _logInstanceData.size());
	pFrameConfig->SetInstances(m_twigUnit, this, startInstance + (UINT) _logInstanceData.size(), (UINT) _twigInstanceData.size());

	return S_OK;
}

HRESULT Tree::ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, const FXMVECTOR parentStart)
{
	if (CalcTime(pRenderData->time) < branch->depth)
		return S_OK;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	XMMATRIX localToWorld;
	ComputeTransformationsManual(&localToWorld, &vChildStart, CalcTime(pRenderData->time), branch, &pRenderData->world, parentStart);

	InstancedData data;
	XMStoreFloat4x4(&data.World, localToWorld);

	if (branch->depth < _params->depthLOD)
	{
		_logInstanceData.push_back(data);
	}
	else
	{
		_twigInstanceData.push_back(data);
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
		assert((UINT)_treeModel->treeData.numBranches >= _logInstanceData.size() + _twigInstanceData.size());
        return (unsigned int) (_logInstanceData.size() + _twigInstanceData.size());
	}
	else
		return 0;
}
