 #include "pch.h"
#include "RenderManager.h"
 #include "GltfLoader.h"
 #include "SceneDefinition.h"
#include "SceneRoot.h"

#include "Primitive.h" // TEMPTEMP

#include "Settings.h"
#if !defined(TREE_XBOX)
#include <wincodec.h> // Leaf alpha for opacity micromaps.
#endif

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "implot.h"

FrameStatistic g_frameStats[MAX_FRAME_STAT] = 
{ 
    { FPS_STAT, L"FPS", UINT_FrameStatValueType, 0 },
    { WORLD_MATRIX_COMPUTED_STAT, L"World Matrix Computed", UINT_FrameStatValueType, 0 },
    { NUM_LEAVES_STAT, L"Num leaves", UINT_FrameStatValueType, 0 },
    { NUM_STICKS_STAT, L"Num sticks", UINT_FrameStatValueType, 0 },
    { DRIVER_12_STAT, L"DirectX 12", UINT_FrameStatValueType, 0 },
};

void SetFrameStat(FrameStatistic stats[], FrameStat stat, UINT value)
{
    stats[stat].stat = value;
    stats[stat].valueType = UINT_FrameStatValueType;
}

void SetFrameStat(FrameStatistic stats[], FrameStat stat, int value)
{
    stats[stat].statInt = value;
    stats[stat].valueType = int_FrameStatValueType;
}

void SetFrameStat(FrameStatistic stats[], FrameStat stat, float value)
{
    stats[stat].statFloat = value;
    stats[stat].valueType = float_FrameStatValueType;
}


RenderManager::RenderManager() : m_platform(nullptr), m_pDebugUI(nullptr)
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

void RenderManager::ApplySceneSettings(const SceneRenderSettings& settings)
{
    // Assign the scene settings only; all frame state and resources remain intact.
    m_renderData.ApplySceneSettings(settings);
}

HRESULT RenderManager::SetPlatform(HMODULE platformDLL)
{
    SafeDelete(&m_platform);

    //m_platform = new RenderPlatform12(&m_renderData);
    m_platform = new RenderPlatformDLL(platformDLL, &m_renderData, m_settings);
    
    return S_OK;
}

HRESULT RenderManager::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, SceneRoot& scene)
{
    HRR(UninitGameLevelGraphics());

    m_lastMaxInstances = maxInstances;
    m_lastUseShadowMaps = useShadowMaps;
 
    // Create vertices and indice for geometry
    GeometryGenerator::BuildGeometryBuffers(m_geometryData);
    scene.AppendGeometry(m_geometryData);

    // The platform sizes its ray-tracing geometry from instances drawn per frame, which instanced objects can make larger
    // than the instance-buffer slots they reserve.
    HRR(GetPlatform()->InitGameLevelGraphics(std::max(maxInstances, scene.GetMaxDrawnInstances()), useShadowMaps, m_geometryData));
    if (!m_renderData.environmentDiffuse.empty())
    {
        HRR(GetPlatform()->LoadTexture(SceneData::WidePath(m_renderData.environmentDiffuse).c_str(),
            MaxSceneTextures, &m_environmentDiffuse));
        HRR(GetPlatform()->LoadTexture(SceneData::WidePath(m_renderData.environmentSpecular).c_str(),
            MaxSceneTextures + 1, &m_environmentSpecular));
    }

    if (maxInstances > 0)
    {
        HRR(m_instancedBuffer.Create(sizeof(InstancedData) * maxInstances, maxInstances, GetPlatform()));
    }
    m_instanceOpacityMicromap.assign(maxInstances, -1);
    m_renderData.instanceOpacityMicromap = m_instanceOpacityMicromap.empty() ? nullptr : m_instanceOpacityMicromap.data();

    return S_OK;
}

HRESULT InstanceBufferRing::Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform)
{
    Release();
    for (auto& buffer : buffers)
        HRR(platform->CreateD3DBuffer(sizeBytes, numInstances, &buffer));

    return S_OK;
}

HRESULT RenderManager::UninitGameLevelGraphics()
{
    HRR(GetPlatform()->UninitGameLevelGraphics());

    m_instancedBuffer.Release();
    m_renderData.instanceOpacityMicromap = nullptr;
    m_instanceOpacityMicromap.clear();
    m_ommData.clear();
    m_ommByteOffsets.clear();
    m_ommTriangleIndices.clear();
    m_ommEntries.clear();
    m_ommAlpha.clear();
    const bool ommSupported = m_renderData.opacityMicromaps.supported;
    m_renderData.opacityMicromaps = {};
    m_renderData.opacityMicromaps.supported = ommSupported;

    for (auto& t : m_textures)
    {
        if (t.second)
        {
            SafeRelease(&t.second->texture);
            delete t.second;
        }
    }
    m_textures.clear();
    m_gltfTextureSlots.clear();
    if (m_environmentDiffuse)
    {
        SafeRelease(&m_environmentDiffuse->texture);
        delete m_environmentDiffuse;
        m_environmentDiffuse = nullptr;
    }
    if (m_environmentSpecular)
    {
        SafeRelease(&m_environmentSpecular->texture);
        delete m_environmentSpecular;
        m_environmentSpecular = nullptr;
    }
     
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
    m_instanceReservations.clear();
    m_nextInstanceBufferOffset = 0;
    m_perFrameInstanceData.clear();
    m_vertexShaders.clear();
    m_pixelShaders.clear();

    return S_OK;
}

HRESULT RenderManager::BeginNewFrame()
{
    m_renderData.lodDistanceScale = m_settings->m_lodDistanceScale;
    D3DBuffer* buffer = m_instancedBuffer.Get(m_renderData.frame);

    InstancedData* dataView = nullptr;
    HRR(GetPlatform()->BeginNewFrame(true, buffer, &dataView));

    //m_renderData.instanceData = dataView;
    //m_renderData.instanceBuffer = buffer;

    return S_OK;
}

HRESULT RenderManager::EndFrame()
{
    // The slots this frame's draws read, merged: reservations are in slot order and each object lists its ranges in
    // order, so adjacent ranges join as they arrive.
    m_instanceUploadRanges.clear();
    std::vector<std::pair<UINT, UINT>> objectRanges;
    for (const auto& reservation : m_instanceReservations)
    {
        objectRanges.clear();
        reservation.first->AppendInstanceUploadRanges(reservation.second.first, reservation.second.second, m_renderData.frame,
            objectRanges);
        for (const auto& range : objectRanges)
        {
            if (range.first >= range.second)
                continue;
            if (!m_instanceUploadRanges.empty() && range.first <= m_instanceUploadRanges.back().second
                && range.first >= m_instanceUploadRanges.back().first)
                m_instanceUploadRanges.back().second = std::max(m_instanceUploadRanges.back().second, range.second);
            else
                m_instanceUploadRanges.push_back(range);
        }
    }
    m_renderData.instanceUploadRanges = &m_instanceUploadRanges;
    return GetPlatform()->EndFrame(m_instancedBuffer.Get(m_renderData.frame));
}

HRESULT RenderManager::GetInstanceIndex(WorldObject* object, UINT& startInstance)
{
    startInstance = m_objectToInstanceBufferOffset[object];
    return S_OK;
}


// The view frustum's side planes and the plane through the eye (points in front have w > 0), from the view-projection
// matrix (Gribb and Hartmann). Near and far are left out: the near plane is close to the eye plane, and far is distant.
void RenderManager::UpdateCullingFrustum()
{
    const XMMATRIX viewProjection = XMMatrixMultiply(XMLoadFloat4x4(&m_renderData.view), XMLoadFloat4x4(&m_renderData.projection));
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProjection);
    const auto column = [&](int c) { return XMVectorSet(m.m[0][c], m.m[1][c], m.m[2][c], m.m[3][c]); };
    const XMVECTOR x = column(0), y = column(1), w = column(3);
    const XMVECTOR planes[] = { XMVectorAdd(w, x), XMVectorSubtract(w, x), XMVectorAdd(w, y), XMVectorSubtract(w, y), w };
    for (int i = 0; i < 5; ++i)
        XMStoreFloat4(&m_cullingPlanes[i], planes[i]);
    m_visibility.clear();
}

// Whether an object can be seen: its cull bounds intersect the view frustum, or it has none.
bool RenderManager::IsVisible(WorldObject* object)
{
    const auto known = m_visibility.find(object);
    if (known != m_visibility.end())
        return known->second;
    XMFLOAT3 bounds[2];
    bool visible = true;
    if (object->GetCullBounds(bounds))
    {
        for (const XMFLOAT4& plane : m_cullingPlanes)
        {
            // The box corner farthest along the plane's normal; outside if even that is behind the plane.
            const float px = plane.x >= 0 ? bounds[1].x : bounds[0].x;
            const float py = plane.y >= 0 ? bounds[1].y : bounds[0].y;
            const float pz = plane.z >= 0 ? bounds[1].z : bounds[0].z;
            if (plane.x * px + plane.y * py + plane.z * pz + plane.w < 0)
            {
                visible = false;
                break;
            }
        }
    }
    m_visibility.emplace(object, visible);
    m_renderData.profile.drawnObjects += visible;
    m_renderData.profile.culledObjects += !visible;
    return visible;
}

HRESULT RenderManager::RenderScene(RenderPass pass)
{
    HRR(GetPlatform()->RenderSceneSetup(pass, &m_instancedBuffer));
    if (pass == RegularPass)
        UpdateCullingFrustum();

    // Opaque geometry must populate color and depth before transparent surfaces.
    // Keep the shadow pass ordering unchanged.
    for (int phase = 0; phase < (pass == RegularPass ? 2 : 1); ++phase)
    {
        for (RenderUnit* ru : m_renderUnits)
        {
            const bool blended = ru->m_material->m_renderState.blendState != StockBlendStates::Overwrite;
            if (pass == RegularPass && blended != (phase == 1))
                continue;
            HRR(Render(ru, pass));
        }
    }

    return S_OK;
}

HRESULT RenderManager::Render(RenderUnit* ru, RenderPass pass)
{
    HRR(GetPlatform()->SetRenderUnit(ru, pass));

    for (auto object : ru->reservations)
    {
        if (pass == ShadowMapPass && (object->GetObjectType() == ObjectType_Primitive
            || ru->m_material->m_shaderMaterial.opticalParams.y > 0.0f))
            continue;

        const InstanceRange& range = m_perFrameInstanceData[ru][object];
        UINT startInstance = range.start;
        UINT numInstances = range.count;

        if (numInstances > 0)
        {
            m_renderData.currentDrawOwner = object;
            m_renderData.currentDrawCulled = pass == RegularPass && m_settings->m_viewCulling && !IsVisible(object);
            HRR(GetPlatform()->DrawIndexedInstanced(ru->m_mesh->m_bufferOffsets.IndexCount, numInstances, ru->m_mesh->m_bufferOffsets.IndexOffset,
                ru->m_mesh->m_bufferOffsets.VertexOffset, startInstance, range.objectTransformSlot));
        }
    }
    m_renderData.currentDrawOwner = nullptr;
    m_renderData.currentDrawCulled = false;
    return S_OK;
}

HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename, LoadedTexture** loadedTexture)
{
    // Load a texture from disk unless it has already been loaded

    const auto& existingTexture = m_textures.find(textureFilename);

    if (existingTexture == m_textures.end())
    {
        if (m_textures.size() >= MaxSceneTextures) return E_BOUNDS; // Later slots hold the environment cubes.
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
    HRR(GetPlatform()->CreateMaterial(name, texture, vertexShader, pixelShader, shadowVertexShader, shadowPixelShader, shaderMaterial,
                                      renderState, (int) m_materials.size(), &newMat));

    m_materials[name] = newMat;
    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderManager::LoadGltfTexture(const GltfTextureSource& source, LoadedTexture** texture, int* textureSlot)
{
    auto existing = m_textures.find(source.key);
    if (existing != m_textures.end())
    {
        *texture = existing->second;
        if (textureSlot)
        {
            auto slot = m_gltfTextureSlots.find(source.key);
            if (slot == m_gltfTextureSlots.end()) return E_UNEXPECTED;
            *textureSlot = slot->second;
        }
        return S_OK;
    }
    if (!source.IsMemoryBacked()) return E_INVALIDARG;
    if (m_textures.size() >= MaxSceneTextures) return E_BOUNDS;
    const int slot = static_cast<int>(m_textures.size());
    HRR(GetPlatform()->LoadTextureFromMemory(source.key.c_str(), source.encodedData.data(),
        source.encodedData.size(), slot, texture, source.colorSpace));
    m_textures[source.key] = *texture;
    m_gltfTextureSlots[source.key] = slot;
    if (textureSlot) *textureSlot = slot;
    return S_OK;
}

LoadedTexture* RenderManager::FindLoadedGltfTexture(const GltfTextureSource& source) const
{
    auto found = m_textures.find(source.key);
    return found == m_textures.end() ? nullptr : found->second;
}

HRESULT RenderManager::BindMaterialTexture(const GltfTextureSource& source, MaterialTextureRole role, ShaderMaterial& shaderMaterial)
{
    LoadedTexture* map = nullptr;
    int slot = -1;
    HRR(LoadGltfTexture(source, &map, &slot));
    switch (role)
    {
    case SpecularStrengthTextureRole: shaderMaterial.specularTextures.x = static_cast<float>(slot); break;
    case SpecularColorTextureRole: shaderMaterial.specularTextures.y = static_cast<float>(slot); break;
    case NormalTextureRole: shaderMaterial.normalTextures.x = static_cast<float>(slot); break;
    case MetallicRoughnessTextureRole: shaderMaterial.metallicRoughnessTextures.x = static_cast<float>(slot); break;
    case TintTextureRole: shaderMaterial.tintTextures.x = static_cast<float>(slot); break;
    default: return E_INVALIDARG; // Base color is bound by CreateMaterial.
    }
    return S_OK;
}

HRESULT RenderManager::BindMaterialTextures(const MaterialTextureSources& sources, ShaderMaterial& shaderMaterial)
{
    for (int role = 0; role < MaterialTextureRoleCount; ++role)
        if (role != BaseColorTextureRole && sources.roles[role])
            HRR(BindMaterialTexture(*sources.roles[role], static_cast<MaterialTextureRole>(role), shaderMaterial));
    return S_OK;
}

// Micro-triangles per edge of a leaf triangle: 2^6 = 64, a few texels each on a 2048 atlas.
static const UINT LeafOpacitySubdivisionLevel = 6;

std::shared_ptr<OpacityMicromap::AlphaImage> RenderManager::LoadOpacityAlpha(const GltfTextureSource& texture, bool wrap)
{
    const std::wstring key = texture.key + (wrap ? L"|wrap" : L"|clamp");
    auto found = m_ommAlpha.find(key);
    if (found != m_ommAlpha.end())
        return found->second;
    std::shared_ptr<OpacityMicromap::AlphaImage> result;
    m_ommAlpha[key] = nullptr;
#if !defined(TREE_XBOX)
    const auto& bytes = texture.encodedData;
    if (!texture.IsMemoryBacked() || bytes.size() < 4 || memcmp(bytes.data(), "DDS ", 4) == 0 || bytes.size() > UINT32_MAX)
        return nullptr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        CComPtr<IWICImagingFactory> factory;
        CComPtr<IWICStream> stream;
        CComPtr<IWICBitmapDecoder> decoder;
        CComPtr<IWICBitmapFrameDecode> frame;
        CComPtr<IWICFormatConverter> converter;
        UINT width = 0, height = 0;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
            && SUCCEEDED(factory->CreateStream(&stream))
            && SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))
            && SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) && width && height
            && SUCCEEDED(factory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                WICBitmapPaletteTypeMedianCut)))
        {
            std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
            if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data())))
            {
                result = std::make_shared<OpacityMicromap::AlphaImage>();
                result->width = width;
                result->height = height;
                result->wrap = wrap;
                result->alpha.resize(static_cast<size_t>(width) * height);
                for (size_t i = 0; i < result->alpha.size(); ++i)
                    result->alpha[i] = rgba[i * 4 + 3];
            }
        }
    }
    if (SUCCEEDED(com))
        CoUninitialize();
#endif
    m_ommAlpha[key] = result;
    return result;
}

int RenderManager::RegisterLeafOpacity(const ShaderMaterial& material, const GltfTextureSource* baseColor, XMFLOAT4 atlasRect)
{
    // Matches the any-hit alpha test: a base-color texture sampled at mip 0, compared with flags.z.
    const float cutoff = material.flags.z;
    // Not for alpha blending or transmission: their any-hit ignores hits for reasons other than alpha.
    if (!m_renderData.opacityMicromaps.supported || cutoff <= 0 || material.flags.y <= 0 || material.flags.x > 0
        || material.opticalParams.y > 0 || !baseColor)
        return -1;
    const XMFLOAT4& row0 = material.uvTransformRow0[BaseColorTextureRole];
    const XMFLOAT4& row1 = material.uvTransformRow1[BaseColorTextureRole];
    const bool wrap = !(material.specularColor.w > 0);
    wchar_t parameters[256];
    swprintf_s(parameters, L"|%g|%d|%g,%g,%g|%g,%g,%g|%g,%g,%g,%g", cutoff, wrap ? 1 : 0, row0.x, row0.y, row0.z,
        row1.x, row1.y, row1.z, atlasRect.x, atlasRect.y, atlasRect.z, atlasRect.w);
    const std::wstring key = baseColor->key + parameters;
    auto found = m_ommEntries.find(key);
    if (found != m_ommEntries.end())
        return found->second;
    m_ommEntries[key] = -1;
    const auto alpha = LoadOpacityAlpha(*baseColor, wrap);
    if (!alpha)
        return -1;

    // The leaf sprite's front-quad triangles in index order (ray tracing leaves out the coplanar back quad),
    // with the same UV path as the any-hit shader:
    // ApplyAtlasRect, then TransformMaterialUv.
    const auto& sprite = m_geometryData.spriteIndices;
    auto textureUv = [&](UINT index)
    {
        const XMFLOAT2 uv = m_geometryData.vertices[sprite.VertexOffset + index].Tex;
        const float u = atlasRect.x + uv.x * atlasRect.z, v = atlasRect.y + uv.y * atlasRect.w;
        return OpacityMicromap::Uv{ u * row0.x + v * row0.y + row0.z, u * row1.x + v * row1.y + row1.z };
    };
    auto& stats = m_renderData.opacityMicromaps;
    const int first = static_cast<int>(m_ommTriangleIndices.size());
    for (UINT triangle = 0; triangle + 2 < sprite.IndexCount / 2; triangle += 3)
    {
        const UINT* indices = &m_geometryData.indices[sprite.IndexOffset + triangle];
        const OpacityMicromap::Uv uv[3] = { textureUv(indices[0]), textureUv(indices[1]), textureUv(indices[2]) };
        const auto baked = OpacityMicromap::BakeTriangle(*alpha, cutoff, uv, LeafOpacitySubdivisionLevel);
        for (int state = 0; state < 4; ++state)
            stats.states[state] += baked.stateCounts[state];
        if (baked.specialIndex != 0)
        {
            m_ommTriangleIndices.push_back(baked.specialIndex);
            ++stats.specialTriangles;
            continue;
        }
        m_ommTriangleIndices.push_back(static_cast<int>(m_ommByteOffsets.size()));
        m_ommByteOffsets.push_back(static_cast<UINT>(m_ommData.size()));
        m_ommData.insert(m_ommData.end(), baked.data.begin(), baked.data.end());
        ++stats.micromaps;
    }
    stats.subdivisionLevel = LeafOpacitySubdivisionLevel;
    stats.bytes = m_ommData.size();
    m_ommEntries[key] = first;
    return first;
}

HRESULT RenderManager::FinalizeOpacityMicromaps()
{
    if (m_ommTriangleIndices.empty())
        return S_OK;
    const HRESULT hr = GetPlatform()->BuildOpacityMicromaps(m_ommData.data(), m_ommData.size(), m_ommByteOffsets.data(),
        static_cast<UINT>(m_ommByteOffsets.size()), LeafOpacitySubdivisionLevel, m_ommTriangleIndices.data(),
        static_cast<UINT>(m_ommTriangleIndices.size()));
    return FAILED(hr) ? hr : S_OK;
}

HRESULT RenderManager::CreateMaterial(const wchar_t* name, const GltfTextureSource* textureSource,
    const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename,
    const wchar_t* shadowVertexShaderFilename, const wchar_t* shadowPixelShaderFilename,
    ShaderMaterial& shaderMaterial, StockRenderState renderState, Material** newMaterial)
{
    if (!textureSource || !textureSource->IsMemoryBacked())
    {
        const wchar_t* filename = textureSource ? textureSource->filename.c_str() : nullptr;
        return CreateMaterial(name, filename, vertexShaderFilename, pixelShaderFilename,
            shadowVertexShaderFilename, shadowPixelShaderFilename, shaderMaterial, renderState, newMaterial);
    }

    auto existing = m_materials.find(name);
    if (existing != m_materials.end())
    {
        *newMaterial = existing->second;
        return S_FALSE;
    }

    LoadedTexture* texture = nullptr;
    HRR(LoadGltfTexture(*textureSource, &texture));

    VertexShader* vertexShader = nullptr;
    PixelShader* pixelShader = nullptr;
    VertexShader* shadowVertexShader = nullptr;
    PixelShader* shadowPixelShader = nullptr;
    if (vertexShaderFilename && *vertexShaderFilename)
    {
        HRR(LoadShader(vertexShaderFilename, ShaderType_VertexShader));
        vertexShader = m_vertexShaders[vertexShaderFilename];
    }
    if (pixelShaderFilename && *pixelShaderFilename)
    {
        HRR(LoadShader(pixelShaderFilename, ShaderType_PixelShader));
        pixelShader = m_pixelShaders[pixelShaderFilename];
    }
    if (shadowVertexShaderFilename && *shadowVertexShaderFilename)
    {
        HRR(LoadShader(shadowVertexShaderFilename, ShaderType_VertexShader));
        shadowVertexShader = m_vertexShaders[shadowVertexShaderFilename];
    }
    if (shadowPixelShaderFilename && *shadowPixelShaderFilename)
    {
        HRR(LoadShader(shadowPixelShaderFilename, ShaderType_PixelShader));
        shadowPixelShader = m_pixelShaders[shadowPixelShaderFilename];
    }

    Material* material = nullptr;
    HRR(GetPlatform()->CreateMaterial(name, texture, vertexShader, pixelShader, shadowVertexShader,
        shadowPixelShader, shaderMaterial, renderState, static_cast<int>(m_materials.size()), &material));
    m_materials[name] = material;
    *newMaterial = material;
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
        m_instanceReservations.push_back({ object, { m_nextInstanceBufferOffset, object->GetMaxInstances() } });
        m_nextInstanceBufferOffset += object->GetMaxInstances();
    }

    // Add reservation
    unit->reservations.push_back(object);
    unit->totalMaxInstances += object->GetMaxInstances(); // TODO needed?

    // Add per frame reservation
    ASSERT(m_perFrameInstanceData[unit].find(object) == m_perFrameInstanceData[unit].end());
    m_perFrameInstanceData[unit][object] = InstanceRange();

    *ppRenderUnit = unit;

    return S_OK;
}

HRESULT RenderManager::SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances,
    UINT objectTransformSlot)
{
    // Keys are reserved up front, so concurrent ComputeConstants calls only write existing entries.
    InstanceRange& range = m_perFrameInstanceData[renderUnit][object];
    range.start = startInstance;
    range.count = numInstances;
    range.objectTransformSlot = objectTransformSlot;
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
void RenderManager::Render(bool oculus, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp,
    bool renderToSharedTexture, float* clearColor)
{
    HRESULT hr = S_OK;

    GetPlatform()->RenderProlog(oculus, m_settings->m_wireframe, useAlphaBlendedRenderTarget, useShadowMaps, clearColor);

    UpdateViewProjection(&GetRenderData().view, &GetRenderData().projection, false);

    // Update chandfsges every frame CB.
    CBChangesEveryFrame cb;
    // 0x1: raster shading samples the shadow map, which DXR mode doesn't render (its raster image is replaced).
    cb.globalFlags = (m_renderData.pShadowMap && m_settings->m_shadowsEnabled && !m_settings->m_dxrEnabled ? 0x1 : 0x0)
        | (m_settings->m_shadowsEnabled ? 0x0 : 0x2)
        | (m_settings->m_hashedAlphaTest ? 0x4 : 0x0)
        | (m_renderData.hdrSceneColor ? 0x8 : 0x0);
    cb.light = m_renderData.dirLights[0];
    cb.numDirectionalLights = m_renderData.numDirectionalLights;
    cb.numPointLights = 0;
    XMScalarSinCos(&cb.environmentRotationSin, &cb.environmentRotationCos,
        XMConvertToRadians(m_renderData.environmentRotationDegrees));
    cb.ambientOcclusionRadius = m_renderData.ambientOcclusionRadius;
    cb.ambientOcclusionStrength = m_renderData.ambientOcclusionStrength;
    cb.sceneTime = XMFLOAT4(m_renderData.normalAnimationTime, 0, 0, 0);
    cb.sunShadowParams = XMFLOAT4(tanf(XMConvertToRadians(m_renderData.sunAngularDiameterDegrees) * 0.5f),
        static_cast<float>(m_renderData.sunShadowSamples), 0, 0);
    const int reflectionRays = m_settings->m_waterReflectionRays;
    cb.reflectionRayCount = m_settings->m_waterReflections
        ? static_cast<UINT>(reflectionRays < 1 ? 1 : reflectionRays > 32 ? 32 : reflectionRays)
        : 0;
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

    HRC(GetPlatform()->RenderPostProcess());  // Does effects and then copies offscreen back to render target 

    HRC(GetPlatform()->RenderDebugUI());  // If ImGui is enabled it will be initialized here

    ImGuiContext* imGuiContext = nullptr;
    ImPlotContext* imPlotContext = nullptr;
    ;
    if (SUCCEEDED(m_platform->GetImGuiContext(&imGuiContext, &imPlotContext)) &&
        imGuiContext && m_pDebugUI != nullptr)
    {
        ImGui::SetCurrentContext(imGuiContext);

        if (imPlotContext)
        {
            ImPlot::SetCurrentContext(imPlotContext);
        }
        m_pDebugUI->UpdateDebugUI(imGuiContext);
    }


    HRC(GetPlatform()->RenderEpilog(oculus, useShadowMaps, renderToSharedTexture));


Cleanup:
    if (FAILED(hr))
        m_settings->m_captureResult = hr;
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

    m_renderData.currentPass = ShadowMapPass;

    GetPlatform()->SetRenderPhase(RP_TRANSITION_TO_RENDER_SHADOW_MAP);

    DrawSceneToShadowMap();

    m_renderData.currentPass = RegularPass;

    GetPlatform()->SetRenderPhase(RP_TRANSITION_FROM_RENDER_SHADOW_MAP);

    return S_OK;
}

void RenderManager::BuildShadowTransform()
{
    // Only the first "main" light casts a shadow.
    XMVECTOR lightDir = XMLoadFloat3(&GetRenderData().dirLights[0].Direction);
    XMVECTOR targetPos = XMLoadFloat3(&GetRenderData().mSceneBounds.Center);
    XMVECTOR lightPos = GetRenderData().shadowLightPositionOverride
        ? XMLoadFloat3(&GetRenderData().shadowLightPosition)
        : -2.0f * GetRenderData().mSceneBounds.Radius * lightDir;
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetX(XMVector3Dot(XMVector3Normalize(targetPos - lightPos), up))) > 0.99f)
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

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


RenderPlatformDLL::RenderPlatformDLL(HMODULE module, RenderData* data, Settings* settings)
{
    CreateFunc createFuncPtr = (CreateFunc) ::GetProcAddress(module, "Create");
    HR(createFuncPtr(data, settings));

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
    ASSIGN_FUNC(RenderPostProcess);
    ASSIGN_FUNC(RenderDebugUI);
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
    ASSIGN_FUNC(LoadTextureFromMemory);
    ASSIGN_FUNC(BuildOpacityMicromaps);
    ASSIGN_FUNC(CreateTexture2D);
    ASSIGN_FUNC(CreateD3DBuffer);
    ASSIGN_FUNC(CreateRenderUnit);

    ASSIGN_FUNC(SetFrameSceneData);

    ASSIGN_FUNC(LoadVertexShader);
    ASSIGN_FUNC(LoadPixelShader);

    ASSIGN_FUNC(GetVertexBuffer);
    ASSIGN_FUNC(GetIndexBuffer);
    ASSIGN_FUNC(GetImGuiContext);
    ASSIGN_FUNC(GetDebugTexturePreview);

    ASSIGN_FUNC(Gui_WndProcHandler);

    ASSIGN_FUNC(GetViewport);

//    ASSIGN_FUNC(GetDevice);
}

RenderPlatformDLL::~RenderPlatformDLL()
{
}
