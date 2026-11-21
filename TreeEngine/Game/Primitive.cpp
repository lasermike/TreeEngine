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

    if (_params->materials.size() < 1)
    {
        // Default: rough green PBR material over the snow texture. The small red/blue
        // components reproduce the former Phong default's white specular wash.
        HRR(AddDefaultPbrMaterial(*_params, XMFLOAT4(0.076f, 1, 0.076f, 1), 0.6f, L"snow.dds"));
    }

    // Default texture for programmatically supplied legacy materials
    if (_params->textureFilename.size() < 1)
    {
        _params->textureFilename.push_back(L"snow.dds");
    }

    std::wstring vsFilename;
    std::wstring psFilename;

    if (_params->cubeMap)
    {
        vsFilename = L"SkyBoxVS.cso";
        psFilename = L"SkyBoxPS.cso";
    }


    wchar_t resourceName[64];
    swprintf(resourceName, 64, L"primitive%d", _id);

    // Create material, mesh, and reserve render unit
    ShaderMaterial shaderMaterial = _params->materials[0];
    shaderMaterial.ApplyTextureCoordScale(_params->textureCoordScale.x, _params->textureCoordScale.y);
    Material* newMaterial = nullptr;
    StockRenderState renderState;
    if (_params->cubeMap)
        renderState.depthStencilState = StockDepthStencilStates::DepthLessThanNoZWriteNoStencil;
    ApplyPbrRenderState(shaderMaterial, renderState);
    const auto* maps = _params->MaterialTextures(0);
    if (maps && !maps->Empty())
    {
        HRR(renderManager.BindMaterialTextures(*maps, shaderMaterial));
        HRR(renderManager.CreateMaterial(resourceName, maps->roles[BaseColorTextureRole].get(), vsFilename.c_str(), psFilename.c_str(), nullptr, nullptr, shaderMaterial, renderState, &newMaterial));
    }
    else
        renderManager.CreateMaterial(resourceName, _params->textureFilename[0].c_str(), vsFilename.c_str(), psFilename.c_str(), nullptr, nullptr, shaderMaterial, renderState, &newMaterial);
    newMaterial->m_isSkybox = _params->cubeMap;

    Mesh* newMesh = nullptr;
    const GeometryBufferData::BufferOffsets* pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(_model->GetPrimitiveType());
    XMVECTOR minimum = XMVectorReplicate(FLT_MAX), maximum = XMVectorReplicate(-FLT_MAX);
    const auto& vertices = renderManager.GetGeometryBufferData().vertices;
    for (UINT i = 0; i < pBufferOffsets->VertexCount; ++i)
    {
        const auto point = XMLoadFloat3(&vertices[pBufferOffsets->VertexOffset + i].Pos);
        minimum = XMVectorMin(minimum, point);
        maximum = XMVectorMax(maximum, point);
    }
    XMStoreFloat3(&m_localBounds[0], minimum);
    XMStoreFloat3(&m_localBounds[1], maximum);
    renderManager.CreateMesh(resourceName,
                             renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
                             renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
                             pBufferOffsets, BASIC_INPUT_LAYOUT, &newMesh);

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

    XMStoreFloat4x4(&firstDataView->World, transform);
    XMVECTOR minimum = XMVectorReplicate(FLT_MAX), maximum = XMVectorReplicate(-FLT_MAX);
    for (UINT corner = 0; corner < 8; ++corner)
    {
        const auto point = XMVector3TransformCoord(XMVectorSet(m_localBounds[corner & 1].x,
            m_localBounds[(corner >> 1) & 1].y, m_localBounds[(corner >> 2) & 1].z, 1), transform);
        minimum = XMVectorMin(minimum, point);
        maximum = XMVectorMax(maximum, point);
    }
    XMStoreFloat3(&_boundingBox[0], minimum);
    XMStoreFloat3(&_boundingBox[1], maximum);
    firstDataView[0].InstanceOffset = startInstance;
    firstDataView[0].AtlasRegion = 0;
    firstDataView[0].PreviousSlot = startInstance;

    return S_OK;
}
