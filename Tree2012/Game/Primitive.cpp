#include "pch.h"
#include "Primitive.h"
#include "RenderManager.h"
#include "MathHelper.h"

Primitive::Primitive(WorldObjectParams* wop) : WorldObject(wop), _model(nullptr)
{
}

Primitive::~Primitive()
{
	if (_model)
	{
		delete _model;
	}
}

void Primitive::Create(PrimitiveModelGenerator* generator)
{
	_model = generator->Create();
}

HRESULT Primitive::InitGraphics(RenderManager& renderManager)
{
	HRR(CleanUpDeviceObjects());

	ShaderMaterial mat;
	mat.Ambient = XMFLOAT4(.5, .5, .5, 1);
	mat.Diffuse = XMFLOAT4(0, 1.0f, 0, 1);
	mat.Specular = XMFLOAT4(.3f, .3f, .3f, 4.0f);
	mat.Reflect = XMFLOAT4(0, 0, 0, 1);
	mat.flags.y = 1; //1 for textured; 

	// Create material, mesh, and reserve render unit
	Material* newMaterial = nullptr;
	renderManager.CreateMaterial(L"ground", L"snow.dds", nullptr, nullptr, mat, &newMaterial);

	Mesh* newMesh = nullptr;
	const GeometryBufferData::BufferIndices* pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(_model->GetPrimitiveType());
	renderManager.CreateMesh(L"ground", renderManager.GetPlatform()->GetVertexBuffer(), renderManager.GetPlatform()->GetIndexBuffer(), pBufferIndices, &newMesh);

	renderManager.ReserveRenderUnit(newMaterial, newMesh, this, &m_renderUnit);

	return S_OK;
}

HRESULT Primitive::ComputeConstants(IRenderFrame* pFrameConfig)
{
	UINT startInstance = 0;
	HRR(pFrameConfig->GetInstanceIndex(this, startInstance));

	pFrameConfig->SetInstances(m_renderUnit, this, startInstance, 1);
	InstancedData* dataView = pFrameConfig->GetRenderData().instanceData;
	InstancedData* firstDataView = dataView + startInstance;

	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0); 
	const XMVECTOR vScaleCenter = XMVectorSet(0, 0, 0, 0);
	XMVECTOR vScale = XMLoadFloat3(&_scale);
	XMVECTOR vQuat = XMLoadFloat4(&this->GetParams().rotation);
	XMVECTOR vStart = XMLoadFloat3(&_position); 

	XMMATRIX transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	// Multiply by this object's world matrix
	transform = transform * XMLoadFloat4x4(&pFrameConfig->GetRenderData().world);

	XMStoreFloat4x4(&firstDataView->World, transform);

	return S_OK;
}

