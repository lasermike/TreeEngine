#pragma once
#include "pch.h"
#include "Model.h"
#include "RenderData.h"
#include <GeometryGenerator.h>
#include "GltfLoader.h"
#include "StockRenderStates.h"
#include "Lod.h"
#include <memory>

class RenderManager;
class WorldObject;

interface IRenderFrame;

enum GeneratorType
{
    NullGeneratorType,
    PrimitiveGeneratorType,
    FixedTreeGeneratorType,
    LSystemGeneratorType,
    GraphGeneratorType,
    ModelGeneratorType,
};

enum ObjectType
{
    ObjectType_World,
    ObjectType_Primitive,
    ObjectType_Tree,
    ObjectType_Graph,
    ObjectType_Model,
    ObjectType_MAX = ObjectType_Model
};

enum Extent
{
    TOP = 0,
    LEFT,
    RIGHT,
    BOTTOM,
    NUM_EXTENTS
};

class WorldObjectParams
{
public:
    WorldObjectParams(GeneratorType genType) :
        position(0, 0, 0),
        scale(1, 1, 1),
        textureCoordScale(1, 1),
        depthLOD(4),
        generatorType(genType),
        _animationSpeed(1.0f),
        primitiveType(PrimitiveType_Box),
        cubeMap(false)
    {
        XMStoreFloat4(&rotation, XMQuaternionIdentity());
    }

    virtual ~WorldObjectParams() { }

    std::string name; // Stable authored object ID; empty for legacy scenes.

    XMFLOAT3 position;
    XMFLOAT3 scale;
    XMFLOAT2 textureCoordScale; // Multiply texture UVs; (1, 1) preserves the original mapping.
    XMFLOAT4 rotation; // Quaternion
    GeneratorType generatorType;
    float _animationSpeed;
    int depthLOD;
    Lod::Thresholds lod;        // Distance LOD (scene "lod"); trees: full-detail sticks within lod.distances[0].
    PrimitiveType primitiveType;
    bool cubeMap;
    std::vector<std::wstring> textureFilename;
    std::vector<ShaderMaterial> materials;
    std::vector<MaterialTextureSources> materialTextures; // Optional PBR maps, parallel to materials.
    std::vector<PrimitiveType> meshes;

    // A tree leaf sprite from a texture atlas. Trees without regions draw the whole leaf texture on every leaf.
    struct LeafAtlasRegion
    {
        XMFLOAT4 atlasRect;   // (u offset, v offset, u scale, v scale), written to an instance slot for the shaders.
        XMFLOAT2 stemOffset;  // Moves the unit leaf quad so the sprite's stem sits on the branch.
        float widthPerLength; // Leaf width as a fraction of its length.
        float weight;         // Relative chance of a leaf using this region.
    };
    std::vector<LeafAtlasRegion> leafRegions;
    float leafTwistRadians = 0; // Largest random roll of a leaf about its own axis, either way.
    float leafSizeVariation = 0; // Largest random change to a leaf's length, as a fraction.

    // Set for instances: the object whose model (and, for trees, branch poses) this object reuses.
    WorldObject* instanceSource = nullptr;

    const MaterialTextureSources* MaterialTextures(size_t slot) const
    {
        return slot < materialTextures.size() ? &materialTextures[slot] : nullptr;
    }
};

// Appends a dielectric PBR material slot, used for objects authored without materials.
// baseColor is linear; baseColorTexture is an optional sRGB map.
HRESULT AddDefaultPbrMaterial(WorldObjectParams& params, XMFLOAT4 baseColor, float roughness, const wchar_t* baseColorTexture);

// Render state for a material slot: PBR materials choose blending and base-color
// sampling like imported glTF materials; legacy materials keep the caller's defaults.
inline void ApplyPbrRenderState(const ShaderMaterial& material, StockRenderState& state)
{
    if (material.specularParams.x == 0) return;
    state.samplerState = material.specularColor.w > 0
        ? StockSamplerStates::MinMagMipLinearUVWClamp
        : StockSamplerStates::MinMagMipLinearUVWWrap;
    if (material.flags.x != 0)
    {
        state.blendState = StockBlendStates::AlphaBlend;
        state.depthStencilState = StockDepthStencilStates::DepthLessThanNoZWriteNoStencil;
    }
}

template<typename T>
class WorldObjectParameters : public WorldObjectParams
{
private:
    T generatorParameters;

public:
    WorldObjectParameters(GeneratorType genType) : WorldObjectParams(genType) { }

    WorldObjectParameters(WorldObjectParameters<T>& src) : WorldObjectParams(src)
    {
        generatorParameters = src.generatorParameters;
    }

    T& GetGeneratorParameters()
    {
        return generatorParameters;
    }
};

class WorldObject
{
protected:

    bool        _drawInstanced;

    WorldObjectParams* _params;
    XMFLOAT3    _position;
    XMFLOAT4    _rotation; // Quaternion
    XMFLOAT3    _scale;

    XMFLOAT3    _boundingBox[2];
    XMFLOAT3    _extents[4];

    int         _id;

    float CalcTime(float time) { return time * _params->_animationSpeed; }
public:

    WorldObject(WorldObjectParams* pParams);
    virtual ~WorldObject(void);

    virtual ObjectType GetObjectType() { return ObjectType_World; }
    WorldObjectParams& GetParams() { return *_params; }
    template <class T> WorldObjectParameters<T>& GetParams() { return *(WorldObjectParameters<T>*)_params; }

    virtual void Create(ModelGenerator* /*generator*/) { }

    virtual void AppendGeometry(GeometryBufferData& /*geometryData*/) { }
    virtual HRESULT InitGraphics(RenderManager& renderManager);
    virtual HRESULT CleanUpDeviceObjects();

    virtual HRESULT ComputeConstants(IRenderFrame* pFrame);
    virtual unsigned int GetNumInstances() { return 0; }
    virtual unsigned int GetMaxInstances() { return 0; }
    // Instances drawn per frame, which can exceed the instance-buffer slots an instanced object reserves.
    virtual unsigned int GetMaxDrawnInstances() { return GetMaxInstances(); }
    bool IsInstance() const { return _params->instanceSource != nullptr; }
    XMFLOAT3* GetBoundingBox() { return _boundingBox; }
    // Conservative world-space bounds of everything the object draws, for view culling. Objects without them
    // (the default) are never culled.
    virtual bool GetCullBounds(XMFLOAT3 bounds[2]) const { return false; }
    XMVECTOR GetExtents(Extent extent);
    XMVECTOR GetPosition() { return XMLoadFloat3(&_position); }
    XMVECTOR GetRotation() { return XMLoadFloat4(&_rotation); }
    XMVECTOR GetScale() { return XMLoadFloat3(&_scale); }

    void SetPosition(XMVECTOR pos) { return XMStoreFloat3(&_position, pos); }
    void SetRotation(XMVECTOR rot) { return XMStoreFloat4(&_rotation, rot); }
    void SetScale(XMVECTOR scale)
    {
        XMStoreFloat3(&_scale, scale);
    }

};
