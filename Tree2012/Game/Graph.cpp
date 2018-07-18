#include "pch.h"
#include "Graph.h"
#include "Tree.h"
#include "RenderManager.h"

TreeModel* GraphModelGenerator::Create()
{
	_model = new TreeModel();

	Branch* parent = nullptr;

	for (UINT i = 1; i < _params.points.size(); i++)
	{
		XMFLOAT4 start = XMFLOAT4(_params.points[i - 1].x, _params.points[i - 1].y, 0, 0);
		XMFLOAT4 end = XMFLOAT4(_params.points[i].x, _params.points[i].y, 0, 0);

		const float thickness = 0.005f;
		Branch* child = AddBranch(parent, start, end, Stick, thickness);

		if (i == 1)
		{
			_model->trunk = child;
		}

		parent = child;
	}

	return _model;
}

void GraphModelGenerator::CreateGraph(std::vector<XMFLOAT2>& /*points*/) { }


FSGraphModel* FSGraphModelGenerator::Create()
{
	FSGraphModel* model = new FSGraphModel();
	return model;
}

FSGraph::FSGraph(WorldObjectParams* wop) : WorldObject(wop), _model(nullptr)
{
}

FSGraph::~FSGraph()
{
	if (_model)
	{
		delete _model;
	}
}

void FSGraph::Create(FSGraphModelGenerator* generator)
{
	_model = generator->Create();
}

HRESULT FSGraph::InitGraphics(RenderManager& renderManager)
{
	HRR(CleanUpDeviceObjects());

	renderManager.CreateTexture2D(L"graph", GetParams<FSGraphParams>().GetGeneratorParameters().points.data(),
		GetParams<FSGraphParams>().GetGeneratorParameters().width,
		GetParams<FSGraphParams>().GetGeneratorParameters().height );

	ShaderMaterial mat;
	mat.Ambient = XMFLOAT4(.5, .5, .5, 1);
	mat.Diffuse = XMFLOAT4(0, .6f, 0, 1);
	mat.Specular = XMFLOAT4(.3f, .3f, .3f, 4.0f);
	mat.Reflect = XMFLOAT4(0, 0, 0, 1);
	mat.flags.y = 1; //1 for textured; 

	// Create material, mesh, and reserve render unit
	Material* newMaterial = nullptr;
	renderManager.CreateMaterial(L"line0", L"graph", L"FSGraphVS.cso", L"FSGraphPS.cso", L"FSGraphVS.cso", L"FSGraphPS.cso", mat,
                  StockRenderState(StockBlendStates::AlphaBlend), &newMaterial);

	Mesh* newMesh = nullptr;
	const GeometryBufferData::BufferOffsets* pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(PrimitiveType_FSQuad);
	renderManager.CreateMesh(L"FSQuad", 
                             renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER), 
                             renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
                             pBufferOffsets, BASIC_INPUT_LAYOUT, &newMesh);

	renderManager.ReserveRenderUnit(newMaterial, newMesh, this, &m_renderUnit);

	return S_OK;
}

HRESULT FSGraph::ComputeConstants(IRenderFrame* pFrameConfig)
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

	XMStoreFloat4x4(&firstDataView->World, transform);

	return S_OK;
}

