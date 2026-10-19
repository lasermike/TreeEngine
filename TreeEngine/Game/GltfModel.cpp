#include "pch.h"
#include "GltfModel.h"
#include "RenderManager.h"

#include <limits>
#include <map>

GltfModel::GltfModel(GltfModelParams* parameters)
    : WorldObject(parameters),
      m_asset(parameters->asset),
      m_forceLit(parameters->forceLit)
{
}

void GltfModel::AppendGeometry(GeometryBufferData& geometryData)
{
    m_asset->AppendGeometry(geometryData);
}

HRESULT GltfModel::InitGraphics(RenderManager& renderManager)
{
    HRR(CleanUpDeviceObjects());
    m_batches.clear();

    std::wstring assetName(m_asset->sourceFilename.begin(), m_asset->sourceFilename.end());
    std::vector<Material*> materials(m_asset->materials.size());
    for (size_t i = 0; i < m_asset->materials.size(); ++i)
    {
        const auto& imported = m_asset->materials[i];
        const GltfTextureSource* texture = imported.textureIndex >= 0
            ? &m_asset->textures[static_cast<size_t>(imported.textureIndex)]
            : nullptr;
        StockRenderState state;
        state.samplerState = imported.wrapTexture
            ? StockSamplerStates::MinMagMipLinearUVWWrap
            : StockSamplerStates::MinMagMipLinearUVWClamp;
        if (imported.alphaBlend)
        {
            state.blendState = StockBlendStates::AlphaBlend;
            state.depthStencilState = StockDepthStencilStates::DepthLessThanNoZWriteNoStencil;
        }
        ShaderMaterial shaderMaterial = imported.shaderMaterial;
        if (imported.specularTextureIndex >= 0)
        {
            LoadedTexture* map = nullptr;
            int slot = -1;
            HRR(renderManager.LoadGltfTexture(m_asset->textures[imported.specularTextureIndex], &map, &slot));
            shaderMaterial.specularTextures.x = static_cast<float>(slot);
        }
        if (imported.normalTextureIndex >= 0)
        {
            LoadedTexture* map = nullptr;
            int slot = -1;
            HRR(renderManager.LoadGltfTexture(m_asset->textures[imported.normalTextureIndex], &map, &slot));
            shaderMaterial.normalTextures.x = static_cast<float>(slot);
        }
        if (imported.specularColorTextureIndex >= 0)
        {
            LoadedTexture* map = nullptr;
            int slot = -1;
            HRR(renderManager.LoadGltfTexture(m_asset->textures[imported.specularColorTextureIndex], &map, &slot));
            shaderMaterial.specularTextures.y = static_cast<float>(slot);
        }
        if (m_forceLit)
            shaderMaterial.flags.w = 0.0f;
        std::wstring name = assetName + L"#material" + std::to_wstring(i)
            + (m_forceLit ? L"#forceLit" : L"");
        HRR(renderManager.CreateMaterial(
            name.c_str(), texture, nullptr, nullptr, nullptr, nullptr,
            shaderMaterial, state, &materials[i]));
    }

    std::vector<Mesh*> meshes(m_asset->primitives.size());
    for (size_t i = 0; i < m_asset->primitives.size(); ++i)
    {
        std::wstring name = assetName + L"#primitive" + std::to_wstring(i);
        HRR(renderManager.CreateMesh(
            name.c_str(),
            renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER),
            &m_asset->primitives[i].bufferOffsets,
            BASIC_INPUT_LAYOUT,
            &meshes[i]));
    }

    std::map<std::pair<size_t, size_t>, size_t> batchIndices;
    for (size_t partIndex = 0; partIndex < m_asset->renderParts.size(); ++partIndex)
    {
        const auto& part = m_asset->renderParts[partIndex];
        auto key = std::make_pair(part.primitiveIndex, part.materialIndex);
        auto existing = batchIndices.find(key);
        if (existing == batchIndices.end())
        {
            RenderBatch batch;
            HRR(renderManager.ReserveRenderUnit(
                materials[part.materialIndex],
                meshes[part.primitiveIndex],
                this,
                &batch.renderUnit));
            batch.partIndices.push_back(partIndex);
            batchIndices.emplace(key, m_batches.size());
            m_batches.push_back(std::move(batch));
        }
        else
        {
            m_batches[existing->second].partIndices.push_back(partIndex);
        }
    }

    return S_OK;
}

HRESULT GltfModel::ComputeConstants(IRenderFrame* frame)
{
    UINT startInstance = 0;
    HRR(frame->GetInstanceIndex(this, startInstance));
    auto* instanceData = frame->GetRenderData().instanceData;

    XMVECTOR center = XMVectorZero();
    XMVECTOR scale = XMLoadFloat3(&_scale);
    XMVECTOR rotation = XMLoadFloat4(&_rotation);
    XMVECTOR translation = XMLoadFloat3(&_position);
    XMMATRIX authoredTransform = XMMatrixTransformation(center, center, scale, center, rotation, translation);

    UINT nextInstance = startInstance;
    for (const auto& batch : m_batches)
    {
        HRR(frame->SetInstances(batch.renderUnit, this, nextInstance, static_cast<UINT>(batch.partIndices.size())));
        for (size_t partIndex : batch.partIndices)
        {
            XMMATRIX importedTransform = XMLoadFloat4x4(&m_asset->renderParts[partIndex].transform);
            XMStoreFloat4x4(&instanceData[nextInstance].World, importedTransform * authoredTransform);
            instanceData[nextInstance].InstanceOffset = nextInstance;
            instanceData[nextInstance].InstanceOffsetPrev = nextInstance;
            ++nextInstance;
        }
    }

    XMVECTOR boundsMin = XMVectorReplicate(std::numeric_limits<float>::max());
    XMVECTOR boundsMax = XMVectorReplicate(std::numeric_limits<float>::lowest());
    const XMFLOAT3& localMin = m_asset->boundsMin;
    const XMFLOAT3& localMax = m_asset->boundsMax;
    for (UINT corner = 0; corner < 8; ++corner)
    {
        XMVECTOR position = XMVectorSet(
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z,
            1.0f);
        position = XMVector3TransformCoord(position, authoredTransform);
        boundsMin = XMVectorMin(boundsMin, position);
        boundsMax = XMVectorMax(boundsMax, position);
    }
    XMStoreFloat3(&_boundingBox[0], boundsMin);
    XMStoreFloat3(&_boundingBox[1], boundsMax);
    return S_OK;
}

unsigned int GltfModel::GetNumInstances()
{
    return static_cast<unsigned int>(m_asset->renderParts.size());
}

unsigned int GltfModel::GetMaxInstances()
{
    return GetNumInstances();
}
