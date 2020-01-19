 #include "pch.h"
#include "RenderManager.h"

#include "Primitive.h" // TEMPTEMP

FrameStatistic g_frameStats[MAX_FRAME_STAT] = 
{ 
    { FPS_STAT, L"FPS", 0 }, 
    { WORLD_MATRIX_COMPUTED_STAT, L"World Matrix Computed", 0 }, 
    { NUM_LEAVES_STAT, L"Num leaves", 0 },
    { NUM_STICKS_STAT, L"Num sticks", 0 },
    { DRIVER_12_STAT, L"DirectX 12", 0 },
};


RenderManager::RenderManager() : m_platform(nullptr)
{
    m_displayMode = Monitor;

    m_renderData.frameStats = g_frameStats;

    m_light.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    m_light.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    m_light.Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    m_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);

    m_nextInstanceBufferOffset = 0;
}

RenderManager::~RenderManager()
{
    UninitDevice();
    SafeDelete(&m_platform);
}

HRESULT RenderManager::SetPlatform(HMODULE platformDLL)
{
    SafeDelete(&m_platform);

    //m_platform = new RenderPlatform12(&m_renderData);
    m_platform = new RenderPlatformDLL(platformDLL, &m_renderData);
    
    return S_OK;
}


HRESULT RenderManager::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps)
{
    HRR(UninitGameLevelGraphics());

    m_lastMaxInstances = maxInstances;
    m_lastUseShadowMaps = useShadowMaps;
 
    // Create vertices and indice for geometry
    GeometryGenerator::BuildGeometryBuffers(m_geometryData);

    GetPlatform()->InitGameLevelGraphics(maxInstances, useShadowMaps, m_geometryData);

    if (maxInstances > 0)
    {
        HRR(m_instancedBuffer.Create(sizeof(InstancedData) * maxInstances, maxInstances, GetPlatform()));
    }

    return S_OK;
}

HRESULT DoubleBuffer::Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform)
{
    delete buffers[0];
    delete buffers[1];

    HRR(platform->CreateD3DBuffer(sizeBytes, numInstances, &buffers[0]));
    HRR(platform->CreateD3DBuffer(sizeBytes, numInstances, &buffers[1]));

    return S_OK;
}

HRESULT RenderManager::UninitGameLevelGraphics()
{
    HRR(GetPlatform()->UninitGameLevelGraphics());

    m_instancedBuffer.Release();

    for (auto& t : m_textures)
    {
        if (t.second)
        {
            SafeRelease(&t.second->texture);
            delete t.second;
        }
    }
    m_textures.clear();
     
    for (auto m : m_materials)
    {
        if (m.second)
        {
            SafeDelete(&m.second);
        }
    }
    m_materials.clear();

    m_meshes.clear();
    m_renderUnits.clear();
    m_objectToInstanceBufferOffset.clear();
    m_nextInstanceBufferOffset = 0;
    m_perFrameInstanceData.clear();
    m_vertexShaders.clear();
    m_pixelShaders.clear();

    return S_OK;
}

HRESULT RenderManager::BeginNewFrame()
{
    D3DBuffer* buffer = m_instancedBuffer.Get(m_renderData.frame);

    InstancedData* dataView = nullptr;
    GetPlatform()->BeginNewFrame(true, buffer, &dataView);

    m_renderData.instanceData = dataView;
    m_renderData.instanceBuffer = buffer;

    return S_OK;
}

HRESULT RenderManager::EndFrame()
{
    return GetPlatform()->EndFrame(m_instancedBuffer.Get(m_renderData.frame));
}

HRESULT RenderManager::GetInstanceIndex(WorldObject* object, UINT& startInstance)
{
    startInstance = m_objectToInstanceBufferOffset[object];
    return S_OK;
}


HRESULT RenderManager::RenderScene(RenderPass pass)
{
    HRR(GetPlatform()->RenderSceneSetup(pass, &m_instancedBuffer));

    // Render each unit
    for (RenderUnit* ru : m_renderUnits)
    {
        Render(ru, pass);
    }

    return S_OK;
}

HRESULT RenderManager::Render(RenderUnit* ru, RenderPass pass)
{
    GetPlatform()->SetRenderUnit(ru, pass);

    for (auto object : ru->reservations)
    {
        if (pass == ShadowMapPass && object->GetObjectType() == PrimitiveObjectType)
            continue;

        UINT startInstance = m_perFrameInstanceData[ru][object].first;
        UINT numInstances = m_perFrameInstanceData[ru][object].second;

        GetPlatform()->DrawIndexedInstanced(ru->m_mesh->m_bufferOffsets->IndexCount, numInstances, ru->m_mesh->m_bufferOffsets->IndexOffset,
            ru->m_mesh->m_bufferOffsets->VertexOffset, startInstance);
    }
    return S_OK;
}

HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename, LoadedTexture** loadedTexture)
{
    // Load a texture from disk unless it has already been loaded

    const auto& existingTexture = m_textures.find(textureFilename);

    if (existingTexture == m_textures.end())
    {
        HRR(GetPlatform()->LoadTexture(textureFilename, (int) m_textures.size(), loadedTexture));
        m_textures[textureFilename] = *loadedTexture;
    }
    else
    {
        *loadedTexture = existingTexture->second;
    }

    return S_OK;
}


HRESULT RenderManager::LoadShader(const wchar_t* shaderFilename, ShaderType shaderType)
{
    switch (shaderType)
    {
    case ShaderType_VertexShader:
    {
        if (m_vertexShaders[shaderFilename])
        {
            return S_OK;
        }

        VertexShader* vertexShader = nullptr;
        HRR(GetPlatform()->LoadVertexShader(shaderFilename, &vertexShader));

        m_vertexShaders[shaderFilename] = vertexShader;

        break;
    }
    case ShaderType_PixelShader:
    {
        if (m_pixelShaders[shaderFilename])
        {
            return S_OK;
        }

        PixelShader* pixelShader = nullptr;
        HRR(GetPlatform()->LoadPixelShader(shaderFilename, &pixelShader));

        // Load regular pixel Shader
        m_pixelShaders[shaderFilename] = pixelShader;

        break;
    }
    }
    return S_OK;
}


HRESULT RenderManager::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height)
{
    LoadedTexture* texture = nullptr;

    HRR(GetPlatform()->CreateTexture2D(name, points, width, height, (int) m_textures.size(), &texture));

    // Success
    assert(texture);

    m_textures[name] = texture;

    return S_OK;
}


HRESULT RenderManager::CreateMaterial(const wchar_t* name, const wchar_t* textureFilename,
    const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename,
    const wchar_t* shadowVertexShaderFilename, const wchar_t* shadowPixelShaderFilename,
    ShaderMaterial& shaderMaterial, StockRenderState renderState, Material** newMaterial)
{
    auto existing = m_materials.find(name);
    if (existing != m_materials.end())
    {
        // TODO: Assert if parameters do not match
        *newMaterial = m_materials[name];
        return S_FALSE;
    }

    // Create a new material
    LoadedTexture* texture = nullptr;
    if (textureFilename && *textureFilename)
    {
        LoadTexture(textureFilename, &texture);
    }

    VertexShader* vertexShader = nullptr;
    PixelShader* pixelShader = nullptr;

    if (vertexShaderFilename && *vertexShaderFilename)
    {
        LoadShader(vertexShaderFilename, ShaderType_VertexShader);
        vertexShader = m_vertexShaders[vertexShaderFilename];
        assert(vertexShader);
    }

    if (pixelShaderFilename && *pixelShaderFilename)
    {
        LoadShader(pixelShaderFilename, ShaderType_PixelShader);
        pixelShader = m_pixelShaders[pixelShaderFilename];
        assert(pixelShader);
    }

    // Shadow shaders
    VertexShader* shadowVertexShader = nullptr;
    PixelShader* shadowPixelShader = nullptr;

    if (shadowVertexShaderFilename && *shadowVertexShaderFilename)
    {
        LoadShader(shadowVertexShaderFilename, ShaderType_VertexShader);
        shadowVertexShader = m_vertexShaders[shadowVertexShaderFilename];
        assert(shadowVertexShader);
    }

    if (shadowPixelShaderFilename && *shadowPixelShaderFilename)
    {
        LoadShader(shadowPixelShaderFilename, ShaderType_PixelShader);
        shadowPixelShader = m_pixelShaders[shadowPixelShaderFilename];
        assert(shadowPixelShader);
    }


    Material* newMat = nullptr;
    GetPlatform()->CreateMaterial(name, texture, vertexShader, pixelShader, shadowVertexShader, shadowPixelShader, shaderMaterial,
                                  renderState, (int) m_materials.size(), &newMat);

    m_materials[name] = newMat;
    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderManager::CreateMesh(const wchar_t* name, D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer,
    const GeometryBufferData::BufferOffsets* bufferOffsets, InputLayouts inputLayout, Mesh** newMesh)
{
    m_meshes.emplace(std::make_pair(name, Mesh(vertexBuffer, indexBuffer, bufferOffsets, inputLayout)));
    *newMesh = &m_meshes[name];
    return S_OK;
}

HRESULT RenderManager::ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit)
{
    RenderUnit* unit = nullptr;

    for (RenderUnit* ru : m_renderUnits)
    {
        if (ru->m_material == material && ru->m_mesh == mesh) 
        {
            unit = ru;
            break;
        }
    }

    if (unit == nullptr)
    {
        HRR(GetPlatform()->CreateRenderUnit(material, mesh, &unit));
        m_renderUnits.push_back(unit);
    }

    // Update object to instance buffer look up table if not present
    if (m_objectToInstanceBufferOffset.find(object) == m_objectToInstanceBufferOffset.end())
    {
        m_objectToInstanceBufferOffset[object] = m_nextInstanceBufferOffset;
        m_nextInstanceBufferOffset += object->GetMaxInstances();
    }

    // Add reservation
    unit->reservations.push_back(object);
    unit->totalMaxInstances += object->GetMaxInstances(); // TODO needed?

    // Add per frame reservation
    ASSERT(m_perFrameInstanceData[unit].find(object) == m_perFrameInstanceData[unit].end());
    m_perFrameInstanceData[unit][object].first = 0;
    m_perFrameInstanceData[unit][object].second = 0;

    *ppRenderUnit = unit;

    return S_OK;
}

HRESULT RenderManager::SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances)
{
    m_perFrameInstanceData[renderUnit][object].first = startInstance;
    m_perFrameInstanceData[renderUnit][object].second = numInstances;
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT RenderManager::InitDevice()
{
    HRESULT hr = S_OK;

    HRR(GetPlatform()->InitDevice());

    return hr;
}

HRESULT RenderManager::UpdateView(XMFLOAT4X4* pViewMat, bool shadowPass)
{
    ASSERT(0);
    CBNeverChanges cbNeverChanges;
    //XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(pViewMat)));
    return GetPlatform()->UpdateView(cbNeverChanges, shadowPass);
}

HRESULT RenderManager::UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass)
{
    return GetPlatform()->UpdateViewProjection(pViewMat, pProjMat, shadowPass);
}

HRESULT RenderManager::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    // Release resources
    GetPlatform()->ReleaseSwapChainResources();

    // Initialize the projection matrix
    GetRenderData().projectionData.screenWidth = windowWidth;
    GetRenderData().projectionData.screenHeight = windowHeight;
    GetRenderData().projectionData.fov = XM_PIDIV4;

    // Create new resources
    HRR(GetPlatform()->OnResize(windowWidth, windowHeight, renderToSharedTexture));


    ASSERT(GetRenderData().projectionData.nearClippingPlane != 0);
    ASSERT(GetRenderData().projectionData.farClippingPlane != 0);
    ASSERT(GetRenderData().projectionData.screenWidth != 0);
    ASSERT(GetRenderData().projectionData.screenHeight != 0);
    ASSERT(GetRenderData().projectionData.fov != 0);

    XMStoreFloat4x4(&GetRenderData().projection, XMMatrixPerspectiveFovLH(GetRenderData().projectionData.fov,
        GetRenderData().projectionData.screenWidth / (float)GetRenderData().projectionData.screenHeight,
        GetRenderData().projectionData.nearClippingPlane, GetRenderData().projectionData.farClippingPlane));

    ASSERT(!XMMatrixIsIdentity(XMLoadFloat4x4(&GetRenderData().projection)));

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------

void RenderManager::UninitDevice()
{
    UninitGameLevelGraphics();

    //SafeDelete(&GetRenderData().pShadowMap);

    GetPlatform()->UninitDevice();
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void RenderManager::Render(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp, bool showShadowBuffer,
    bool renderToSharedTexture, float* clearColor)
{
    HRESULT hr = S_OK;

    GetPlatform()->RenderProlog(oculus, wireframe, useAlphaBlendedRenderTarget, useShadowMaps, clearColor);

    UpdateViewProjection(&GetRenderData().view, &GetRenderData().projection, false);

    // Update chandfsges every frame CB.
    CBChangesEveryFrame cb;
    cb.globalFlags = m_renderData.pShadowMap ? 0x1 : 0x0;
    cb.light = m_renderData.dirLights[0];
    cb.numDirectionalLights = m_renderData.numDirectionalLights;
    cb.numPointLights = 0;
    assert(m_renderData.numDirectionalLights < 2);

    XMStoreFloat4(&cb.eyePos, m_renderData.eyePos);
    cb.shadowMatrix = m_renderData.shadowTransform;

    GetPlatform()->SetFrameSceneData(&cb);

    // Draw everything
    HRC(RenderScene(RegularPass));

    // Show frame statistics
    if (showHelp)
    {
        DrawFrameStats();
    }

    HRC(GetPlatform()->RenderEpilog(oculus, useShadowMaps, showShadowBuffer, renderToSharedTexture));

Cleanup:
    return;
}

HRESULT RenderManager::DrawFrameStats()
{
    GetPlatform()->BeginDrawText();

    float y = 10;

    for (int i = 0; i < MAX_FRAME_STAT; i++)
    {
        wchar_t text[128];
        swprintf(text, 128, L"%s %d", GetRenderData().frameStats[i].name,
            GetRenderData().frameStats[i].stat);
        GetPlatform()->DrawText2(0, y, 0x33444444, text);
        y += 34.0f;
    }

    GetPlatform()->EndDrawText();
    return S_OK;
}

HRESULT RenderManager::ChangePlatform(int platform)
{
    UninitDevice();
    InitDevice();

    return S_OK;
}

HRESULT RenderManager::RenderShadowMap()
{
    BuildShadowTransform();

    GetPlatform()->SetRenderPhase(RP_TRANSITION_TO_RENDER_SHADOW_MAP);

    DrawSceneToShadowMap();

    GetPlatform()->SetRenderPhase(RP_TRANSITION_FROM_RENDER_SHADOW_MAP);

    return S_OK;
}

void RenderManager::BuildShadowTransform()
{
    // Only the first "main" light casts a shadow.
    XMVECTOR lightDir = XMLoadFloat3(&GetRenderData().dirLights[0].Direction);
    XMVECTOR lightPos = -2.0f * GetRenderData().mSceneBounds.Radius * lightDir;
    XMVECTOR targetPos = XMLoadFloat3(&GetRenderData().mSceneBounds.Center);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

    // Transform bounding sphere to light space.
    XMFLOAT3 sphereCenterLS;
    XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

    // Ortho frustum in light space encloses scene.
    float l = sphereCenterLS.x - GetRenderData().mSceneBounds.Radius;
    float b = sphereCenterLS.y - GetRenderData().mSceneBounds.Radius;
    float n = sphereCenterLS.z - GetRenderData().mSceneBounds.Radius / 1.25f;
    float r = sphereCenterLS.x + GetRenderData().mSceneBounds.Radius;
    float t = sphereCenterLS.y + GetRenderData().mSceneBounds.Radius;
    float f = sphereCenterLS.z + GetRenderData().mSceneBounds.Radius * 2.75f;
    XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    XMMATRIX S = V*P*T;

    XMStoreFloat4x4(&GetRenderData().lightView, V);
    XMStoreFloat4x4(&GetRenderData().lightProj, P);
    XMStoreFloat4x4(&GetRenderData().shadowTransform, S);
}

void RenderManager::DrawSceneToShadowMap()
{
    UpdateViewProjection(&GetRenderData().lightView, &GetRenderData().lightProj, true);

    // Draw everything
    HR(RenderScene(ShadowMapPass));

    UpdateViewProjection(&GetRenderData().view, &GetRenderData().projection, false);
}


RenderPlatformDLL::RenderPlatformDLL(HMODULE module, RenderData* data)
{
    CreateFunc createFuncPtr = (CreateFunc) ::GetProcAddress(module, "Create");
    HR(createFuncPtr(data));

    InitDeviceFuncPtr = (InitDeviceFunc) ::GetProcAddress(module, "InitDevice");

#define ASSIGN_FUNC(METHOD) \
    METHOD##FuncPtr = (METHOD##Func) ::GetProcAddress(module, #METHOD); \
    ASSERT(METHOD##FuncPtr);

    ASSIGN_FUNC(SetWindow);

    ASSIGN_FUNC(InitDevice);
    ASSIGN_FUNC(UninitDevice);

    ASSIGN_FUNC(ReleaseSwapChainResources);
    ASSIGN_FUNC(OnResize);
    ASSIGN_FUNC(GetSwapChain);

    ASSIGN_FUNC(UpdateView);
    ASSIGN_FUNC(UpdateViewProjection);

    ASSIGN_FUNC(InitGameLevelGraphics);
    ASSIGN_FUNC(UninitGameLevelGraphics);

    ASSIGN_FUNC(BeginNewFrame);
    ASSIGN_FUNC(EndFrame);

    ASSIGN_FUNC(RenderProlog);
    ASSIGN_FUNC(RenderEpilog);

    ASSIGN_FUNC(RenderSceneSetup);
    ASSIGN_FUNC(SetRenderPhase);

    ASSIGN_FUNC(DrawIndexedInstanced);

    ASSIGN_FUNC(BeginDrawText);
    ASSIGN_FUNC(DrawText2);
    ASSIGN_FUNC(EndDrawText);

    ASSIGN_FUNC(CreateMaterial);
    ASSIGN_FUNC(SetRenderUnit);
    ASSIGN_FUNC(LoadTexture);
    ASSIGN_FUNC(CreateTexture2D);
    ASSIGN_FUNC(CreateD3DBuffer);
    ASSIGN_FUNC(CreateRenderUnit);

    ASSIGN_FUNC(SetFrameSceneData);

    ASSIGN_FUNC(LoadVertexShader);
    ASSIGN_FUNC(LoadPixelShader);

    ASSIGN_FUNC(GetVertexBuffer);
    ASSIGN_FUNC(GetIndexBuffer);

    ASSIGN_FUNC(GetViewport);

//    ASSIGN_FUNC(GetDevice);
}

RenderPlatformDLL::~RenderPlatformDLL()
{

}
