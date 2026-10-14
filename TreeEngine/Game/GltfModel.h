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
};

class GltfModel : public WorldObject
{
    struct RenderBatch
    {
        RenderUnit* renderUnit = nullptr;
        std::vector<size_t> partIndices;
    };

    std::shared_ptr<GltfAsset> m_asset;
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
