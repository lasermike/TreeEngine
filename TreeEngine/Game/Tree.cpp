#include "pch.h"
#include "Tree.h"
#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "RenderManager.h"
#include "MathHelper.h"
#include <directxcolors.h>

using namespace DirectX;

Tree::Tree(WorldObjectParams* pParams) : _treeModel(nullptr), WorldObject(pParams)
{
}

// A repeatable value in [0, 1) for one branch; salt picks independent streams (region, twist, size).
static float BranchRandom(int branchId, uint32_t seed, uint32_t salt)
{
    uint32_t hash = static_cast<uint32_t>(branchId) * 0x9E3779B1u ^ seed ^ salt;
    hash ^= hash >> 16; hash *= 0x85EBCA6Bu; hash ^= hash >> 13; hash *= 0xC2B2AE35u; hash ^= hash >> 16;
    return hash / 4294967296.0f;
}

Tree::~Tree(void)
{
    SafeDelete(&_treeModel);
}

void Tree::Create(TreeModelGenerator* generator)
{
    _treeModel = generator->Create();
    m_poseValid = false;
    m_branchPoses.assign(_treeModel->treeData.numBranches, BranchPose());
    m_matureTime = 0;
    for (int i = 0; i < _treeModel->treeData.numBranches; ++i)
        m_matureTime = std::max(m_matureTime, static_cast<float>(_treeModel->treeData.pBranches[i].depth) + 5.0f);

    // Each leaf gets a weighted atlas region, and optionally a twist and size, from a hash of its branch ID and
    // the object ID, so the choice is the same every run and differs between trees grown from the same rules.
    const int branches = _treeModel->treeData.numBranches;
    const auto& regions = _params->leafRegions;
    m_leafRegions.clear();
    m_leafTwists.clear();
    m_leafScales.clear();
    if (!regions.empty()) m_leafRegions.assign(branches, -1);
    if (_params->leafTwistRadians > 0) m_leafTwists.assign(branches, 0.0f);
    if (_params->leafSizeVariation > 0) m_leafScales.assign(branches, 1.0f);
    uint32_t seed = 2166136261u;
    for (unsigned char character : _params->name) seed = (seed ^ character) * 16777619u;
    float totalWeight = 0;
    for (const auto& region : regions) totalWeight += region.weight;
    for (int i = 0; i < branches; ++i)
    {
        const Branch& branch = _treeModel->treeData.pBranches[i];
        if (branch.geometryType != Leaf || branch.id < 0 || branch.id >= branches)
            continue;
        if (!m_leafRegions.empty())
        {
            float pick = BranchRandom(branch.id, seed, 0) * totalWeight;
            int chosen = static_cast<int>(regions.size()) - 1;
            for (size_t r = 0; r < regions.size(); ++r)
            {
                if (pick < regions[r].weight) { chosen = static_cast<int>(r); break; }
                pick -= regions[r].weight;
            }
            m_leafRegions[branch.id] = chosen;
        }
        if (!m_leafTwists.empty())
            m_leafTwists[branch.id] = (BranchRandom(branch.id, seed, 0x68E31DA4u) * 2 - 1) * _params->leafTwistRadians;
        if (!m_leafScales.empty())
            m_leafScales[branch.id] = 1 + (BranchRandom(branch.id, seed, 0xB5297A4Du) * 2 - 1) * _params->leafSizeVariation;
    }
    if (_params->_animationSpeed == 0.0f)
    {
        _params->_animationSpeed = _treeModel->treeData.numLevels / 10.0f;
    }

    if (_params->meshes.size() == 0)
    {
        _params->meshes.push_back(PrimitiveType_Cylinder);
        _params->meshes.push_back(PrimitiveType_Box);
        _params->meshes.push_back(PrimitiveType_Box);
    }
}

HRESULT Tree::InitGraphics(RenderManager& renderManager)
{
    HRR(CleanUpDeviceObjects());
    m_poseValid = false;
    m_instanceLayoutsValid = false;

    // Instances are never generated; they draw the source's meshes (set when the source was created).
    if (IsInstance())
        _params->meshes = InstanceSource()->GetParams().meshes;

    if (_params->materials.size() < 1)
    {
        // Default: red-tinted bark trunk and untextured dark green leaves (linear colors).
        // The trunk's green/blue reproduce the former Phong default's specular wash.
        HRR(AddDefaultPbrMaterial(*_params, XMFLOAT4(1, 0.17f, 0.17f, 1), 0.8f, L"Bark_0005_diffuse.dds"));
        HRR(AddDefaultPbrMaterial(*_params, XMFLOAT4(0, 0.2158605f, 0, 1), 0.8f, nullptr));
    }

    // Default texture for programmatically supplied legacy materials
    if (_params->textureFilename.size() < 1)
    {
        _params->textureFilename.push_back(L"Bark_0005_diffuse.dds");
    }

    GeometryBuffer geometryBuffer = PRIMITIVE_GEOMETRY_BUFFER;
    std::wstring vsFilename, shadowVsFilename;
    std::wstring psFilename;
    InputLayouts inputLayout = BASIC_INPUT_LAYOUT;
    if (_params->meshes[0] == PrimitiveType_SkinnedCylinder)
    {
        inputLayout = SKINNED_INPUT_LAYOUT;
        geometryBuffer = SKINNED_PRIMITIVE_GEOMETRY_BUFFER;
        vsFilename = L"VSSkinned.cso";
        shadowVsFilename = L"BuildShadowMapVSSkinned.cso";
    }

    // Create material, mesh, and reserve render unit
    auto shaderMaterials = _params->materials;
    for (auto& material : shaderMaterials)
    {
        material.ApplyTextureCoordScale(_params->textureCoordScale.x, _params->textureCoordScale.y);
    }
    // Materials are named by their content, so trees with identical materials (instances, and trees using the same
    // scene material) share one. Each distinct material takes one of the renderer's maxNumMaterials slots.
    auto contentName = [](const wchar_t* prefix, const ShaderMaterial& shaderMaterial, const std::wstring& texture,
        const wchar_t* vs, const wchar_t* shadowVs, const StockRenderState& state)
    {
        uint64_t hash = 14695981039346656037ull;
        auto mix = [&hash](const void* data, size_t size)
        {
            for (size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ull;
        };
        mix(&shaderMaterial, sizeof(shaderMaterial));
        mix(texture.c_str(), texture.size() * sizeof(wchar_t));
        for (const wchar_t* shader : { vs, shadowVs })
        {
            const std::wstring text = shader ? shader : L"";
            mix(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        }
        const int fields[] = { static_cast<int>(state.blendState), static_cast<int>(state.samplerState),
            static_cast<int>(state.rasterizerState), static_cast<int>(state.depthStencilState) };
        mix(fields, sizeof(fields));
        wchar_t suffix[17];
        swprintf_s(suffix, L"%016llx", static_cast<unsigned long long>(hash));
        return std::wstring(prefix) + suffix;
    };
    auto createMaterial = [&](const wchar_t* prefix, size_t slot, const wchar_t* textureFilename,
        const wchar_t* vs, const wchar_t* shadowVs, StockRenderState state, Material** material) -> HRESULT
    {
        ApplyPbrRenderState(shaderMaterials[slot], state);
        const auto* maps = _params->MaterialTextures(slot);
        if (!maps || maps->Empty())
        {
            const auto name = contentName(prefix, shaderMaterials[slot], textureFilename ? textureFilename : L"", vs, shadowVs, state);
            return renderManager.CreateMaterial(name.c_str(), textureFilename, vs, nullptr, shadowVs, nullptr, shaderMaterials[slot], state, material);
        }
        HRR(renderManager.BindMaterialTextures(*maps, shaderMaterials[slot]));
        // The bound material holds every map's texture slot; the base-color map is passed separately.
        const auto& baseColor = maps->roles[BaseColorTextureRole];
        const auto name = contentName(prefix, shaderMaterials[slot], baseColor ? baseColor->key : L"", vs, shadowVs, state);
        return renderManager.CreateMaterial(name.c_str(), baseColor.get(), vs, nullptr, shadowVs, nullptr, shaderMaterials[slot], state, material);
    };
    Material* pBranchLOD0 = nullptr;
    HRR(createMaterial(L"BranchLOD0_", 0, _params->textureFilename[0].c_str(), vsFilename.c_str(), shadowVsFilename.c_str(), StockRenderState(), &pBranchLOD0));
    Mesh* pNewMesh = nullptr;
    const GeometryBufferData::BufferOffsets* pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(_params->meshes[0]);
    renderManager.CreateMesh(L"BranchLOD0", renderManager.GetPlatform()->GetVertexBuffer(geometryBuffer), renderManager.GetPlatform()->GetIndexBuffer(geometryBuffer), pBufferOffsets, inputLayout, &pNewMesh);
    renderManager.ReserveRenderUnit(pBranchLOD0, pNewMesh, this, &m_segmentRenderUnits[TreeSegmentIndex(TreeSegment::BranchLOD0)]);

    Material* pBranchLOD1 = nullptr;
    HRR(createMaterial(L"BranchLOD1_", 0, _params->textureFilename[0].c_str(), nullptr, nullptr, StockRenderState(), &pBranchLOD1));
    pNewMesh = nullptr;
    pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(PrimitiveType_Box);
    renderManager.CreateMesh(L"BranchLOD1", renderManager.GetPlatform()->GetVertexBuffer(geometryBuffer), renderManager.GetPlatform()->GetIndexBuffer(geometryBuffer), pBufferOffsets, BASIC_INPUT_LAYOUT, &pNewMesh);
    renderManager.ReserveRenderUnit(pBranchLOD1, pNewMesh, this, &m_segmentRenderUnits[TreeSegmentIndex(TreeSegment::BranchLOD1)]);

    StockRenderState leafLOD0State;
    leafLOD0State.blendState = StockBlendStates::Overwrite;
    Material* pLeafLOD0 = nullptr;
    const wchar_t* leafLOD0Texture = _params->textureFilename.size() > 1 ? _params->textureFilename[1].c_str() : nullptr;
    HRR(createMaterial(L"LeafLOD0_", 1, leafLOD0Texture, nullptr, nullptr, leafLOD0State, &pLeafLOD0));
    // Opacity micromaps let DXR skip the leaf alpha test wherever the texture is certainly opaque or transparent.
    // Instances draw the source's layouts, which carry the source's micromaps.
    m_leafOpacityMicromaps.clear();
    if (!IsInstance())
    {
        const auto* maps = _params->MaterialTextures(1);
        const GltfTextureSource* baseColor = maps ? maps->roles[BaseColorTextureRole].get() : nullptr;
        if (_params->leafRegions.empty())
            m_leafOpacityMicromaps.push_back(renderManager.RegisterLeafOpacity(shaderMaterials[1], baseColor, XMFLOAT4(0, 0, 1, 1)));
        for (const auto& region : _params->leafRegions)
            m_leafOpacityMicromaps.push_back(renderManager.RegisterLeafOpacity(shaderMaterials[1], baseColor, region.atlasRect));
    }
    pNewMesh = nullptr;
    pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(PrimitiveType_Sprite);
    renderManager.CreateMesh(L"LeafLOD0", renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER), renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), pBufferOffsets, BASIC_INPUT_LAYOUT, &pNewMesh);
    renderManager.ReserveRenderUnit(pLeafLOD0, pNewMesh, this, &m_segmentRenderUnits[TreeSegmentIndex(TreeSegment::LeafLOD0)]);

    return S_OK;
}

void Tree::AccumulateProfile(RenderProfile& profile) const
{
    if (_params->instanceSource) return; // Instances reuse the source's pose.
    profile.poseCacheBytes += m_branchPoses.capacity() * sizeof(BranchPose);
    if (m_reusePose) profile.reusedPoseBranches += m_numBranchInstances;
    else profile.rebuiltPoseBranches += m_numBranchInstances;
}

HRESULT Tree::ComputeConstants(IRenderFrame* pFrameConfig)
{
    // 2 passes
    // 1st pass - animate bones and determine number of model groups
    // 2nd pass - build world matrix for each bone

    if (IsInstance())
        return ComputeInstanceConstants(pFrameConfig);

    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants");

    UINT startInstance = 0;
    HRR(pFrameConfig->GetInstanceIndex(this, startInstance));

    InstancedData* dataView = pFrameConfig->GetRenderData().instanceData;
    RenderData& renderData = pFrameConfig->GetRenderData();
    const float poseTime = std::min(CalcTime(renderData.time), m_matureTime);
    m_reusePose = renderData.cacheTreePoses && m_poseValid && poseTime == m_lastPoseTime
        && XMVector3Equal(XMLoadFloat3(&_position), XMLoadFloat3(&m_lastPosePosition))
        && XMVector4Equal(XMLoadFloat4(&_rotation), XMLoadFloat4(&m_lastPoseRotation))
        && XMVector3Equal(XMLoadFloat3(&_scale), XMLoadFloat3(&m_lastPoseScale));
    const bool reuseInstances = m_reusePose && startInstance == m_lastInstanceStart
        && _params->depthLOD == m_lastDepthLOD
        && XMVector3Equal(renderData.eyePos, XMLoadFloat3(&m_lastEyePosition));

    if (!reuseInstances)
    {
        m_cachedLeafCount = m_cachedStickCount = 0;
        if (!m_reusePose)
        {
            _boundingBox[0] = XMFLOAT3(-1, -1, -1);
            _boundingBox[1] = XMFLOAT3(1, 1, 1);
            for (int i = 0; i < NUM_EXTENTS; i++)
                _extents[i] = XMFLOAT3(0, 0, 0);
        }

        for (auto& instanceData : m_segmentInstanceData)
        {
            instanceData.clear();
        }
        for (auto& opacityMicromaps : m_segmentOpacityMicromaps)
            opacityMicromaps.clear();
        for (auto& branchIds : m_segmentBranchIds)
            branchIds.clear();
        m_numInstancesPerSegment.fill(0);

        if (m_treeFrames.size() < _treeModel->treeData.numBranches)
        {
            m_treeFrames.resize(_treeModel->treeData.numBranches);
        }

        if (!m_reusePose)
        {
            m_poseValid = false;
            BranchInstance firstFrame = { _treeModel->trunk, nullptr, _position, XMFLOAT3(), XMFLOAT3(), -1, 0 };
            m_treeFrames[0] = firstFrame;
            m_numBranchInstances = 1;
        }

        for (int currentFrame = 0; currentFrame < m_numBranchInstances; currentFrame++)
        {
            BranchInstance& frame = m_treeFrames[currentFrame];
            if (!m_reusePose)
            {
                HRR(ComputeBranchVectorAtTime(frame, &renderData));
            }
            else if (CalcTime(renderData.time) >= frame.branch->depth
                && (frame.branch->geometryType == Leaf || frame.branch->geometryType == Stick))
            {
                m_numInstancesPerSegment[TreeSegmentIndex(SelectSegmentType(frame, &renderData))]++;
            }
            frame.instanceOffset = -1;
        }

        for (int currentFrame = 0; currentFrame < m_numBranchInstances; currentFrame++)
        {
            BranchInstance& frame = m_treeFrames[currentFrame];
            HRR(ComputeBranchInstanceSkinningMatrix(frame, &renderData, startInstance));
        }
        m_lastPoseTime = poseTime;
        m_lastPosePosition = _position;
        m_lastPoseRotation = _rotation;
        m_lastPoseScale = _scale;
        m_poseValid = true;
        XMStoreFloat3(&m_lastEyePosition, renderData.eyePos);
        m_lastDepthLOD = _params->depthLOD;
        m_lastInstanceStart = startInstance;
    }
    else
    {
        renderData.frameStats[NUM_LEAVES_STAT].stat += m_cachedLeafCount;
        renderData.frameStats[NUM_STICKS_STAT].stat += m_cachedStickCount;
    }
    renderData.frameStats[WORLD_MATRIX_COMPUTED_STAT].stat = m_numBranchInstances;

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Compute Constants memcpy");

    // The atlas rects that leaves' AtlasRegion values point at, rewritten every frame like the rest of the buffer.
    if (!m_leafRegions.empty())
    {
        UINT slot = AtlasRectStart(startInstance);
        for (const auto& region : _params->leafRegions)
        {
            InstancedData rect = {};
            rect.World._11 = region.atlasRect.x;
            rect.World._12 = region.atlasRect.y;
            rect.World._13 = region.atlasRect.z;
            rect.World._14 = region.atlasRect.w;
            rect.InstanceOffset = rect.InstanceOffsetPrev = rect.PreviousSlot = slot;
            dataView[slot++] = rect;
        }
    }

    UINT segmentOffset = startInstance;
    for (size_t segmentIndex = 0; segmentIndex < TreeSegmentCount; ++segmentIndex)
    {
        InstanceDataArray& instanceData = m_segmentInstanceData[segmentIndex];
        if (instanceData.size() > 0)
        {
            memcpy(dataView + segmentOffset, &instanceData[0], instanceData.size() * sizeof(InstancedData));
            // Written where the instances land: skipped branches shift slots from the precounted InstanceOffset.
            if (renderData.instanceOpacityMicromap)
                std::copy(m_segmentOpacityMicromaps[segmentIndex].begin(), m_segmentOpacityMicromaps[segmentIndex].end(),
                    renderData.instanceOpacityMicromap + segmentOffset);
            LinkPreviousSlots(dataView, segmentOffset, m_segmentBranchIds[segmentIndex], m_slotHistory[0], renderData.frame);
        }

        pFrameConfig->SetInstances(m_segmentRenderUnits[segmentIndex], this, segmentOffset, (UINT)instanceData.size());
        segmentOffset += (UINT)instanceData.size();
    }

    if (m_instanceCount > 0)
    {
        // The layouts depend only on the pose and depth LOD, so they are rebuilt only when those change.
        if (!m_reusePose || !m_instanceLayoutsValid || m_instanceLayoutDepthLOD != _params->depthLOD)
            BuildInstanceLayouts(startInstance, renderData.instanceOpacityMicromap);
        for (int kind = 0; kind < InstanceLayoutCount; ++kind)
        {
            InstanceLayout& layout = m_instanceLayouts[kind];
            for (size_t segmentIndex = 0; segmentIndex < TreeSegmentCount; ++segmentIndex)
            {
                InstanceDataArray& instanceData = layout.segments[segmentIndex];
                if (instanceData.size() > 0)
                {
                    memcpy(dataView + layout.start[segmentIndex], &instanceData[0], instanceData.size() * sizeof(InstancedData));
                    LinkPreviousSlots(dataView, layout.start[segmentIndex], layout.branchIds[segmentIndex],
                        m_slotHistory[1 + kind], renderData.frame);
                }
            }
        }
    }

    PIXEndEvent();

    return S_OK;
}

HRESULT Tree::ComputeBranchVectorAtTime(BranchInstance& frame, RenderData* pRenderData)
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants ComputeBranchVectorAtTime");

    if (CalcTime(pRenderData->time) < frame.branch->depth)
    {
        return S_FALSE;
    }

    XMVECTOR vChildStart;
    XMVECTOR vParentStart = XMVectorSelect(g_XMOne, XMLoadFloat3(&frame.startPosition), g_XMSelect1110.v);

    switch (frame.branch->geometryType)
    {
    case Leaf:
    case Stick:
        m_numInstancesPerSegment[TreeSegmentIndex(SelectSegmentType(frame, pRenderData))]++;
        break;
    }

    ComputeBranchVectorEndPoint(&vChildStart, CalcTime(pRenderData->time), frame);


    // Compute child branches
    for (unsigned int c = 0; c < frame.branch->children.size(); c++)
    {
        if (frame.branch->Child(c) != 0)
        {
            Branch* child = &_treeModel->treeData.pBranches[frame.branch->Child(c)];

            XMFLOAT3 childStart;
            XMStoreFloat3(&childStart, vChildStart);

            BranchInstance childFrame = { child, &frame, childStart, XMFLOAT3(), XMFLOAT3(), -1, frame.instanceOffset };
            m_treeFrames[m_numBranchInstances++] = childFrame;
        }
    }

    PIXEndEvent();

    return S_OK;
}

const WorldObjectParams::LeafAtlasRegion* Tree::LeafRegion(const Branch* branch) const
{
    if (branch->id < 0 || static_cast<size_t>(branch->id) >= m_leafRegions.size()) return nullptr;
    const int region = m_leafRegions[branch->id];
    return region < 0 ? nullptr : &_params->leafRegions[region];
}

UINT Tree::AtlasRectStart(UINT startInstance) const
{
    const UINT branches = _treeModel->treeData.numBranches;
    return startInstance + (m_instanceCount > 0 ? branches * (1 + InstanceLayoutCount) : branches);
}

int Tree::LeafOpacityMicromap(const Branch* branch) const
{
    if (branch->geometryType != Leaf || m_leafOpacityMicromaps.empty())
        return -1;
    if (m_leafRegions.empty())
        return m_leafOpacityMicromaps[0];
    if (branch->id < 0 || static_cast<size_t>(branch->id) >= m_leafRegions.size() || m_leafRegions[branch->id] < 0)
        return -1;
    return m_leafOpacityMicromaps[m_leafRegions[branch->id]];
}

UINT Tree::AtlasRegionSlot(UINT startInstance, const Branch* branch) const
{
    if (branch->id < 0 || static_cast<size_t>(branch->id) >= m_leafRegions.size() || m_leafRegions[branch->id] < 0)
        return 0;
    return AtlasRectStart(startInstance) + m_leafRegions[branch->id] + 1;
}

TreeSegment Tree::SelectSegmentType(BranchInstance& frame, RenderData* pRenderData) const
{
    if (frame.branch->geometryType == Leaf)
    {
        return TreeSegment::LeafLOD0;
    }

    bool useBranchLOD0 = _params->depthLOD != -1 &&
        (frame.branch->depth < _params->depthLOD ||
            XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&frame.startPosition) - pRenderData->eyePos)) < 100.0f);

    return useBranchLOD0 ? TreeSegment::BranchLOD0 : TreeSegment::BranchLOD1;
}

HRESULT Tree::ComputeBranchInstanceSkinningMatrix(BranchInstance& frame, RenderData* pRenderData, int startInstance)
{
    if (frame.branch->id < 0 || static_cast<size_t>(frame.branch->id) >= m_branchPoses.size())
        return E_UNEXPECTED;
    BranchPose& pose = m_branchPoses[frame.branch->id];
    if (!m_reusePose)
    {
        XMMATRIX localToWorld;
        HRESULT hr = ComputeTransformationsManual(&localToWorld, frame);
        pose.renderable = hr == S_OK;
        if (hr != S_OK) return hr;
        if (const auto* region = LeafRegion(frame.branch))
            localToWorld = XMMatrixMultiply(XMMatrixTranslation(region->stemOffset.x, region->stemOffset.y, 0), localToWorld);
        if (frame.branch->id >= 0 && static_cast<size_t>(frame.branch->id) < m_leafTwists.size()
            && m_leafTwists[frame.branch->id] != 0)
        {
            // Roll the leaf about its own axis, through its base (the branch start, shifted like every branch).
            const XMVECTOR base = XMLoadFloat3(&frame.startPosition) + XMVectorSet(0, -0.5f, 0, 0);
            const XMVECTOR axis = XMVector3Normalize(XMLoadFloat3(&frame.endPosition) - XMLoadFloat3(&frame.startPosition));
            localToWorld = localToWorld * XMMatrixTranslationFromVector(-base)
                * XMMatrixRotationNormal(axis, m_leafTwists[frame.branch->id]) * XMMatrixTranslationFromVector(base);
        }
        localToWorld = localToWorld * XMMatrixTranslationFromVector(-XMLoadFloat3(&_position))
            * XMMatrixAffineTransformation(XMLoadFloat3(&_scale), g_XMZero,
                XMLoadFloat4(&_rotation), XMLoadFloat3(&_position));
        XMStoreFloat4x4(&pose.world, localToWorld);
    }
    if (!pose.renderable) return S_FALSE;

    InstancedData data = {};
    data.World = pose.world;
    data.AtlasRegion = AtlasRegionSlot(startInstance, frame.branch);


    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants push_back");

    TreeSegment segment = SelectSegmentType(frame, pRenderData);
    size_t segmentIndex = TreeSegmentIndex(segment);
    int instanceOffset = startInstance;
    for (size_t i = 0; i < segmentIndex; ++i)
    {
        instanceOffset += m_numInstancesPerSegment[i];
    }
    instanceOffset += m_segmentInstanceData[segmentIndex].size();

    data.InstanceOffset = instanceOffset;
    if (frame.parent && frame.parent->instanceOffset != 0xFFFFFFFF)
    {
        data.InstanceOffsetPrev = frame.parent->instanceOffset;
    }
    else
    {
        data.InstanceOffsetPrev = instanceOffset;
    }

    m_segmentInstanceData[segmentIndex].push_back(data);
    m_segmentOpacityMicromaps[segmentIndex].push_back(LeafOpacityMicromap(frame.branch));
    m_segmentBranchIds[segmentIndex].push_back(frame.branch->id);

    switch (frame.branch->geometryType)
    {
    case Leaf:
        m_cachedLeafCount++;
        pRenderData->frameStats[NUM_LEAVES_STAT].stat++;
        break;
    case Stick:
        m_cachedStickCount++;
        pRenderData->frameStats[NUM_STICKS_STAT].stat++;
        break;
    }

    frame.instanceOffset = instanceOffset;

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants ComputeBranchInstanceData");

    //Compute bounding box
    if (!m_reusePose)
    {
        XMMATRIX localToWorld = XMLoadFloat4x4(&pose.world);
        for (int corner = 0; corner < 8; ++corner)
        {
            const XMVECTOR point = XMVector3Transform(XMVectorSet(corner & 1 ? 1.0f : -1.0f,
                corner & 2 ? 1.0f : -1.0f, corner & 4 ? 1.0f : -1.0f, 1), localToWorld);
            XMStoreFloat3(&_boundingBox[0], XMVectorMin(point, XMLoadFloat3(&_boundingBox[0])));
            XMStoreFloat3(&_boundingBox[1], XMVectorMax(point, XMLoadFloat3(&_boundingBox[1])));
        }
    }

    return S_OK;
}

inline XMMATRIX XM_CALLCONV TEMatrixLookToLH
(
    FXMVECTOR EyePosition,
    FXMVECTOR EyeDirection,
    FXMVECTOR UpDirection
)
{
    assert(!XMVector3Equal(EyeDirection, XMVectorZero()));
    assert(!XMVector3IsInfinite(EyeDirection));
    assert(!XMVector3Equal(UpDirection, XMVectorZero()));
    assert(!XMVector3IsInfinite(UpDirection));

    XMVECTOR R0;
    XMVECTOR R1;
    XMVECTOR R2;

    XMVECTOR vCross = XMVector3Cross(UpDirection, EyeDirection);
    XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
    float crossLenSq;
    XMStoreFloat(&crossLenSq, vCrossLenSq);
    if (crossLenSq > 0.001f) // Need better value for epsilon here
    {
        R2 = XMVector3Normalize(EyeDirection);

        R0 = XMVector3Cross(UpDirection, R2);
        R0 = XMVector3Normalize(R0);

        R1 = XMVector3Cross(R2, R0);
    }
    else
    {
        XMVECTOR vRight = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // Swap right and up vectors

        vCross = XMVector3Cross(EyeDirection, vRight);
        vCrossLenSq = XMVector3LengthSq(vCross);
        XMStoreFloat(&crossLenSq, vCrossLenSq);
        if (crossLenSq > 0.001f) // Need better value for epsilon here
        {
            R1 = XMVector3Normalize(EyeDirection);

            R0 = XMVector3Cross(R1, vRight);
            R0 = XMVector3Normalize(R0);

            R2 = XMVector3Cross(R0, R1);
        }
        else
        {
            assert(false);
        }
    }

    XMVECTOR NegEyePosition = XMVectorNegate(EyePosition);

    XMVECTOR D0 = XMVector3Dot(R0, NegEyePosition);
    XMVECTOR D1 = XMVector3Dot(R1, NegEyePosition);
    XMVECTOR D2 = XMVector3Dot(R2, NegEyePosition);

    XMMATRIX M;
    M.r[0] = XMVectorSelect(D0, R0, g_XMSelect1110.v);
    M.r[1] = XMVectorSelect(D1, R1, g_XMSelect1110.v);
    M.r[2] = XMVectorSelect(D2, R2, g_XMSelect1110.v);
    M.r[3] = g_XMIdentityR3.v;

    M = XMMatrixTranspose(M);

    return M;
}

const XMVECTORF32 origin = { { { 0.0f, -0.5f, 0.0f, 0.0f } } };

const XMMATRIX matNegOrigin = { { 1.0f, 0.0f, 0.0f, 0.0f }, 
                                { 0.0f, 1.0f, 0.0f, 0.0f },
                                { 0.0f, 0.0f, 1.0f, 0.0f },
                                { 0.0f, 0.5f, 0.0f, 1.0f }
                              };

__inline XMMATRIX TEMatrixTransformation
(
    FXMVECTOR Scaling,
    FXMMATRIX RotationMat,
    CXMVECTOR Translation
)
{
    XMMATRIX M;
    XMMATRIX MScaling;
    XMVECTOR VTranslation;

    MScaling = XMMatrixScalingFromVector(Scaling);
    VTranslation = _mm_and_ps(Translation, g_XMMask3);

    M = matNegOrigin;
    M = XMMatrixMultiply(M, MScaling);
    M = XMMatrixMultiply(M, RotationMat);
    M.r[3] = XMVectorAdd(M.r[3], origin);
    M.r[3] = XMVectorAdd(M.r[3], VTranslation);

    return M;
}


HRESULT Tree::ComputeBranchVectorEndPoint(XMVECTOR* vComputedEnd, float time, BranchInstance& frame)
{
    float animScaleFactor = 1.0f;
    if (time - 5 < frame.branch->depth)
    {
        animScaleFactor = (time - frame.branch->depth) / 5;
    }
    else
    {
        animScaleFactor = animScaleFactor;
    }

    ASSERT(animScaleFactor >= 0.0f);

    XMVECTOR vStart = XMLoadFloat3(&frame.startPosition);
    XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(frame.branch->end)) + XMLoadFloat3(&_position);

    // Scale branch
    XMVECTOR vMag = XMVector3Length(vEnd - vStart);
    XMVECTOR vScale = g_XMOne; // *.3f;

    switch (frame.branch->geometryType)
    {
    case Leaf:
    {
        const auto* region = LeafRegion(frame.branch);
        const float widthPerLength = region ? region->widthPerLength : 0.5f;
        const float length = XMVectorGetX(vMag) * (static_cast<size_t>(frame.branch->id) < m_leafScales.size()
            ? m_leafScales[frame.branch->id] : 1.0f);
        vScale = XMVectorSet(length * widthPerLength, length, 0.004f, 0) * animScaleFactor;
        break;
    }
    case Stick:
        vScale = XMVectorSet(frame.branch->thickness, XMVectorGetX(vMag), frame.branch->thickness, 0) * animScaleFactor;
        break;
    }

    // Child start pos
    XMVECTOR startToEnd = vEnd - vStart;

    XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);

    *vComputedEnd = startToEnd * vMagY + vStart;

    XMStoreFloat3(&frame.endPosition, *vComputedEnd);
    XMStoreFloat3(&frame.scale, vScale);

    return S_OK;
}

HRESULT Tree::ComputeTransformationsManual(XMMATRIX* computedTransform, BranchInstance& frame)
{
    XMVECTOR vStart = XMLoadFloat3(&frame.startPosition);
    XMVECTOR vEnd = XMLoadFloat3(&frame.endPosition);

    if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vEnd, vStart))) < 0.001f)
    {
        return S_FALSE;
    }

    XMVECTOR vScale = XMLoadFloat3(&frame.scale);
    XMVECTOR vDir = XMVector3Normalize(vEnd - vStart);
    XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);

//#define NEW_WAY
#ifdef NEW_WAY

    if (XMVector3Equal(startToEnd, XMVectorZero()))
    {
        computedMatrix = XMMatrixScaling(0, 0, 0);
    }
    else
    {
        XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
        computedMatrix = TEMatrixLookToLH(vStart, vDir, vUp);
        computedMatrix.r[3] = vStart;
    }

    *computedTransform = computedMatrix;

#else

    // Determine rotation
    XMVECTOR vCross = XMVector3Normalize(XMVector3Cross(vUp, vDir));
    XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
    XMVECTOR vQuat;
    XMMATRIX matRotation;
    float crossLenSq;
    XMStoreFloat(&crossLenSq, vCrossLenSq);
    if (crossLenSq > 0.001f)
    {
        vDir = XMVectorSelect(g_XMZero, vDir, g_XMSelect1110);

        XMVECTOR vDot = XMVector3Dot(vUp, vDir);
        float angle;
        XMStoreFloat(&angle, vDot);
        angle = acos(angle);
        matRotation = XMMatrixRotationNormal(vCross, angle);
    }
    else if (XMVectorGetY(vDir) < -0.99f)
    {
        XMVECTOR vLeft = XMVectorSet(1, 0, 0, 0);
        vQuat = XMQuaternionRotationAxis(vLeft, XM_PI);
        matRotation = XMMatrixRotationX(XM_PI);
    }
    else
    {
        vQuat = XMQuaternionRotationAxis(vUp, 0);
        matRotation = XMMatrixIdentity();
    }

    *computedTransform = TEMatrixTransformation(vScale, matRotation, vStart);

#endif

    // Compute extents.  Keep these in local coordinates if we can.
    if (XMVectorGetY(vEnd) > _extents[TOP].y)
        XMStoreFloat3(&_extents[TOP], vEnd);

    return S_OK;
}


unsigned int Tree::GetMaxInstances()
{
    if (_params->instanceSource)
        return 1; // The per-draw object transform.
    if (!_treeModel)
        return 0;
    const unsigned int branches = _treeModel->treeData.numBranches;
    const unsigned int atlasRects = static_cast<unsigned int>(_params->leafRegions.size());
    return (m_instanceCount > 0 ? branches * (1 + InstanceLayoutCount) : branches) + atlasRects;
}

unsigned int Tree::GetMaxDrawnInstances()
{
    if (_params->instanceSource)
        return InstanceSource()->GetMaxDrawnInstances();
    return _treeModel ? _treeModel->treeData.numBranches : 0;
}

unsigned int Tree::GetNumInstances()
{
    if (_params->instanceSource)
        return 1;
    if (_treeModel)
    {
        unsigned int numInstances = 0;
        for (auto& instanceData : m_segmentInstanceData)
        {
            numInstances += instanceData.size();
        }

        assert((UINT)_treeModel->treeData.numBranches >= numInstances);
        return numInstances;
    }
    else
        return 0;
}

TreeSegment Tree::SelectLayoutSegment(const BranchInstance& frame, InstanceLayoutKind layout) const
{
    if (frame.branch->geometryType == Leaf)
        return TreeSegment::LeafLOD0;
    if (_params->depthLOD == -1)
        return TreeSegment::BranchLOD1;
    // Matches SelectSegmentType with every branch beyond (far) or within (near) the 10-unit detail distance.
    return layout == NearLayout || frame.branch->depth < _params->depthLOD ? TreeSegment::BranchLOD0 : TreeSegment::BranchLOD1;
}

void Tree::BuildInstanceLayouts(UINT startInstance, int* opacityMicromaps)
{
    const UINT branches = _treeModel->treeData.numBranches;
    if (m_layoutOffsets.size() < static_cast<size_t>(m_numBranchInstances))
        m_layoutOffsets.resize(m_numBranchInstances);

    for (int kind = 0; kind < InstanceLayoutCount; ++kind)
    {
        InstanceLayout& layout = m_instanceLayouts[kind];
        const auto layoutKind = static_cast<InstanceLayoutKind>(kind);

        // Count per segment so each segment's range is contiguous.
        std::array<UINT, TreeSegmentCount> counts = {};
        for (int i = 0; i < m_numBranchInstances; ++i)
        {
            const BranchInstance& frame = m_treeFrames[i];
            if (m_branchPoses[frame.branch->id].renderable)
                counts[TreeSegmentIndex(SelectLayoutSegment(frame, layoutKind))]++;
        }
        UINT next = startInstance + branches * (1 + kind);
        for (size_t segmentIndex = 0; segmentIndex < TreeSegmentCount; ++segmentIndex)
        {
            layout.start[segmentIndex] = next;
            next += counts[segmentIndex];
            layout.segments[segmentIndex].clear();
            layout.branchIds[segmentIndex].clear();
        }

        // Emit with skinning links into this layout's own slots. Parents precede their children.
        layout.leafCount = layout.stickCount = 0;
        for (int i = 0; i < m_numBranchInstances; ++i)
        {
            const BranchInstance& frame = m_treeFrames[i];
            m_layoutOffsets[i] = -1;
            const BranchPose& pose = m_branchPoses[frame.branch->id];
            if (!pose.renderable)
                continue;
            const TreeSegment segment = SelectLayoutSegment(frame, layoutKind);
            const size_t segmentIndex = TreeSegmentIndex(segment);
            const UINT offset = layout.start[segmentIndex] + layout.segments[segmentIndex].size();
            const int parent = frame.parent ? static_cast<int>(frame.parent - &m_treeFrames[0]) : -1;

            InstancedData data = {};
            data.World = pose.world;
            data.AtlasRegion = AtlasRegionSlot(startInstance, frame.branch);
            data.InstanceOffset = offset;
            data.InstanceOffsetPrev = parent >= 0 && m_layoutOffsets[parent] >= 0 ? m_layoutOffsets[parent] : offset;
            layout.segments[segmentIndex].push_back(data);
            layout.branchIds[segmentIndex].push_back(frame.branch->id);
            if (opacityMicromaps)
                opacityMicromaps[offset] = LeafOpacityMicromap(frame.branch);
            m_layoutOffsets[i] = static_cast<int>(offset);

            if (frame.branch->geometryType == Leaf) layout.leafCount++;
            else if (frame.branch->geometryType == Stick) layout.stickCount++;
        }
    }
    m_instanceLayoutsValid = true;
    m_instanceLayoutDepthLOD = _params->depthLOD;
}

// Points each instance written at firstSlot at the same branch's slot last frame and records this frame's slots.
void Tree::LinkPreviousSlots(InstancedData* instances, UINT firstSlot, const std::vector<int>& branchIds,
    SlotHistory& history, UINT frame)
{
    const size_t branches = _treeModel->treeData.numBranches;
    const UINT current = frame & 1, previous = current ^ 1;
    for (int i = 0; i < 2; ++i)
        if (history.slots[i].size() < branches)
        {
            history.slots[i].resize(branches, InstancedData::NoPreviousSlot);
            history.frames[i].resize(branches, 0);
        }
    for (size_t i = 0; i < branchIds.size(); ++i)
    {
        const int id = branchIds[i];
        const UINT slot = firstSlot + static_cast<UINT>(i);
        instances[slot].PreviousSlot = history.frames[previous][id] == frame ? history.slots[previous][id]
            : InstancedData::NoPreviousSlot;
        history.slots[current][id] = slot;
        history.frames[current][id] = frame + 1;
    }
}

HRESULT Tree::ComputeInstanceConstants(IRenderFrame* pFrameConfig)
{
    Tree& source = *InstanceSource();
    UINT transformSlot = 0;
    HRR(pFrameConfig->GetInstanceIndex(this, transformSlot));
    RenderData& renderData = pFrameConfig->GetRenderData();

    // Remove the source's baked scale, rotation, and translation before applying this instance's pose.
    const XMMATRIX sourceFrame = XMMatrixAffineTransformation(XMLoadFloat3(&source.m_lastPoseScale), g_XMZero,
        XMLoadFloat4(&source.m_lastPoseRotation), XMLoadFloat3(&source.m_lastPosePosition));
    const XMMATRIX instanceWorld = XMMatrixAffineTransformation(XMLoadFloat3(&_scale), g_XMZero,
        XMLoadFloat4(&_rotation), XMLoadFloat3(&_position));
    const XMMATRIX rel = XMMatrixMultiply(XMMatrixInverse(nullptr, sourceFrame), instanceWorld);

    InstancedData data = {};
    XMStoreFloat4x4(&data.World, rel);
    data.InstanceOffset = data.InstanceOffsetPrev = data.PreviousSlot = transformSlot;
    renderData.instanceData[transformSlot] = data;

    if (!source.m_instanceLayoutsValid)
    {
        for (RenderUnit* unit : m_segmentRenderUnits)
            pFrameConfig->SetInstances(unit, this, 0, 0, transformSlot);
        return S_OK;
    }

    // Bounding box: the source's box carried through Rel.
    XMVECTOR boxMin = g_XMFltMax, boxMax = -g_XMFltMax.v;
    for (int corner = 0; corner < 8; ++corner)
    {
        const XMVECTOR point = XMVector3Transform(XMVectorSet(
            source._boundingBox[corner & 1].x, source._boundingBox[(corner >> 1) & 1].y, source._boundingBox[corner >> 2].z, 1), rel);
        boxMin = XMVectorMin(boxMin, point);
        boxMax = XMVectorMax(boxMax, point);
    }
    XMStoreFloat3(&_boundingBox[0], boxMin);
    XMStoreFloat3(&_boundingBox[1], boxMax);

    // Whole-instance LOD: full detail when any part of the box is within the 10-unit detail distance.
    const XMVECTOR nearest = XMVectorClamp(renderData.eyePos, boxMin, boxMax);
    const bool nearCamera = XMVectorGetX(XMVector3LengthSq(nearest - renderData.eyePos)) < 100.0f;
    InstanceLayout& layout = source.m_instanceLayouts[nearCamera ? NearLayout : FarLayout];
    for (size_t segmentIndex = 0; segmentIndex < TreeSegmentCount; ++segmentIndex)
    {
        pFrameConfig->SetInstances(m_segmentRenderUnits[segmentIndex], this, layout.start[segmentIndex],
            static_cast<UINT>(layout.segments[segmentIndex].size()), transformSlot);
    }
    renderData.frameStats[NUM_LEAVES_STAT].stat += layout.leafCount;
    renderData.frameStats[NUM_STICKS_STAT].stat += layout.stickCount;
    return S_OK;
}
