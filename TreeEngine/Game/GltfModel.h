#pragma once

#include "GltfLoader.h"
#include "WorldObject.h"
#include <memory>
#include <vector>

struct RenderUnit;

class GltfModelParams : public WorldObjectParams
{
public:
    GltfModelParams() : WorldObjectParams(ModelGeneratorType) {}

    std::shared_ptr<GltfAsset> asset;
    bool forceLit = false;
    std::string animatedNormalMaterial;
    XMFLOAT2 normalVelocity1 = { 0, 0 };
    XMFLOAT2 normalVelocity2 = { 0, 0 };
    float normalStrength = 1;
    // See SceneData::ModelDefinition: a tint texture for one material, and its scene-relative path for saving.
    std::string tintMaterial;
    std::string tintTexturePath;
    std::shared_ptr<const GltfTextureSource> tintTexture;
    XMFLOAT2 tintUvScale = { 1, 1 };
    XMFLOAT2 tintUvOffset = { 0, 0 };
    // See SceneData::ModelDefinition: biplanar mapping for one material.
    std::string biplanarMaterial;
    float biplanarUvPerUnit = 0;
    float biplanarSharpness = 1;
};

class GltfModel : public WorldObject
{
    struct RenderBatch
    {
        RenderUnit* renderUnit = nullptr;
        std::vector<size_t> partIndices;
    };

    std::shared_ptr<GltfAsset> m_asset;
    bool m_forceLit;
    std::string m_animatedNormalMaterial;
    XMFLOAT2 m_normalVelocity1;
    XMFLOAT2 m_normalVelocity2;
    float m_normalStrength;
    std::string m_tintMaterial;
    std::shared_ptr<const GltfTextureSource> m_tintTexture;
    XMFLOAT2 m_tintUvScale;
    XMFLOAT2 m_tintUvOffset;
    std::string m_biplanarMaterial;
    float m_biplanarUvPerUnit;
    float m_biplanarSharpness;
    std::vector<RenderBatch> m_batches;
    bool m_boundsValid = false;

public:
    explicit GltfModel(GltfModelParams* parameters);

    ObjectType GetObjectType() override { return ObjectType_Model; }
    void AppendGeometry(GeometryBufferData& geometryData) override;
    HRESULT InitGraphics(RenderManager& renderManager) override;
    HRESULT ComputeConstants(IRenderFrame* frame) override;
    bool GetCullBounds(XMFLOAT3 bounds[2]) const override
    {
        if (!m_boundsValid) return false;
        bounds[0] = _boundingBox[0];
        bounds[1] = _boundingBox[1];
        return true;
    }
    unsigned int GetNumInstances() override;
    unsigned int GetMaxInstances() override;
};
