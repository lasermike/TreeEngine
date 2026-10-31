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
    std::vector<RenderBatch> m_batches;

public:
    explicit GltfModel(GltfModelParams* parameters);

    ObjectType GetObjectType() override { return ObjectType_Model; }
    void AppendGeometry(GeometryBufferData& geometryData) override;
    HRESULT InitGraphics(RenderManager& renderManager) override;
    HRESULT ComputeConstants(IRenderFrame* frame) override;
    unsigned int GetNumInstances() override;
    unsigned int GetMaxInstances() override;
};
