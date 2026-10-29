#include "pch.h"
#include "WorldObject.h"

static int WorldObject_nextId = 0;

HRESULT AddDefaultPbrMaterial(WorldObjectParams& params, XMFLOAT4 baseColor, float roughness, const wchar_t* baseColorTexture)
{
    PbrMaterialFactors factors;
    factors.baseColor = baseColor;
    factors.metallic = 0;
    factors.roughness = roughness;
    bool alphaBlend = false;
    ShaderMaterial material = BuildPbrShaderMaterial(factors, alphaBlend);
    MaterialTextureSources maps;
    if (baseColorTexture)
    {
        try
        {
            maps.roles[BaseColorTextureRole] = LoadSharedMaterialTextureFile(baseColorTexture, 2);
        }
        catch (const std::runtime_error&)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
        material.flags.y = 1;
    }
    // Keep the slot vectors parallel to materials.
    params.textureFilename.resize(params.materials.size());
    params.materialTextures.resize(params.materials.size());
    params.materials.push_back(material);
    params.textureFilename.push_back(L"");
    params.materialTextures.push_back(maps);
    return S_OK;
}


WorldObject::WorldObject(WorldObjectParams* pParams) : _params(pParams), _drawInstanced(true)
{
    _id = WorldObject_nextId++;
    _position = XMFLOAT3(0,0,0);
    XMStoreFloat4(&_rotation, XMQuaternionIdentity());
    _scale = XMFLOAT3(1,1,1);

    _boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);

    if (pParams)
    {
        _position = pParams->position;
        _rotation = pParams->rotation;
        _scale = pParams->scale;
    }
}

WorldObject::~WorldObject(void)
{
    CleanUpDeviceObjects();
    SafeDelete(&_params);
}

HRESULT WorldObject::InitGraphics(RenderManager& /*renderManager*/)
{
    return S_OK;
}

HRESULT WorldObject::CleanUpDeviceObjects()
{
    return S_OK;
}

HRESULT WorldObject::ComputeConstants(IRenderFrame* /*pFrameConfig*/)
{
    return E_NOTIMPL;
}

XMVECTOR WorldObject::GetExtents(Extent extent)
{
    return XMLoadFloat3(&_extents[extent]); // + XMLoadFloat3(&_position); 
}