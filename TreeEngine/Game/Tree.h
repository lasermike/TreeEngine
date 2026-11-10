#pragma once
#include <array>
#include "pch.h"
#include <vector>
#include <queue>
#include "WorldObject.h"
#include "TreeModel.h"
#include "TreeSegment.h"
#include "Materials.h"

typedef long HRESULT;

class TreeModelGenerator;
struct Branch;
struct RenderUnit;

class Tree : public WorldObject
{
private:

    TreeModel* _treeModel;

    // STL vector who's allocation never shrinks
    class InstanceDataArray
    {
    public:
        std::vector<InstancedData> m_data;
        int m_size;

        InstanceDataArray() : m_size(0) {}

        void clear()
        {
            m_size = 0;
        }

        int size()
        {
            return m_size;
        }

        void push_back(InstancedData& newData)
        {
            if (m_size == m_data.size())
            {
                m_data.push_back(newData);
                m_size++;
            }
            else
            {
                m_data[m_size++] = newData;
            }
        }

        InstancedData& operator[](int pos)
        {
            return m_data[pos];
        }
    };

    std::array<InstanceDataArray, TreeSegmentCount> m_segmentInstanceData;
    std::array<std::vector<int>, TreeSegmentCount> m_segmentOpacityMicromaps; // Per instance in m_segmentInstanceData.
    std::array<std::vector<int>, TreeSegmentCount> m_segmentBranchIds; // Per instance in m_segmentInstanceData.
    std::array<RenderUnit*, TreeSegmentCount> m_segmentRenderUnits = {};
    std::array<int, TreeSegmentCount> m_numInstancesPerSegment = {};

    struct BranchInstance
    {
        Branch* branch;
        BranchInstance* parent;
        XMFLOAT3 startPosition;
        XMFLOAT3 endPosition;
        XMFLOAT3 scale;
        int instanceOffset;
        int instanceOffsetPrev;
    };

    int m_numBranchInstances = 0;
    std::vector<BranchInstance> m_treeFrames;
    struct BranchPose
    {
        XMFLOAT4X4 world;
        bool renderable = false;
    };
    std::vector<BranchPose> m_branchPoses;

    // Atlas leaves. The atlas rects occupy the last instance slots this tree reserves; a leaf's
    // InstancedData::AtlasRegion names its rect's slot. Empty when the tree has no atlas.
    std::vector<int> m_leafRegions; // Per branch ID: index into the params' leafRegions, or -1.
    std::vector<float> m_leafTwists; // Per branch ID: roll about the leaf's axis, in radians. Empty without twist.
    std::vector<float> m_leafScales; // Per branch ID: leaf length scale. Empty without size variation.
    const WorldObjectParams::LeafAtlasRegion* LeafRegion(const Branch* branch) const;
    UINT AtlasRegionSlot(UINT startInstance, const Branch* branch) const; // InstancedData::AtlasRegion value.
    // Opacity-micromap entries for the leaf material: one per params leafRegions entry, or one for the whole texture.
    std::vector<int> m_leafOpacityMicromaps;
    int LeafOpacityMicromap(const Branch* branch) const; // RenderData::instanceOpacityMicromap value.
    UINT AtlasRectStart(UINT startInstance) const;
    float m_matureTime = 0;
    float m_lastPoseTime = 0;
    XMFLOAT3 m_lastPosePosition = {};
    XMFLOAT4 m_lastPoseRotation = {};
    XMFLOAT3 m_lastPoseScale = XMFLOAT3(1, 1, 1);
    bool m_poseValid = false;
    bool m_reusePose = false;
    XMFLOAT3 m_lastEyePosition = {};
    int m_lastDepthLOD = 0;
    UINT m_lastInstanceStart = 0;
    UINT m_cachedLeafCount = 0;
    UINT m_cachedStickCount = 0;

    // Instancing. A source tree with instances publishes two camera-independent branch layouts after its own:
    // far (depth-only LOD) and near (every stick at full detail). Each instance draws one of them with its own
    // object transform, so instances need no per-branch CPU work.
    enum InstanceLayoutKind { FarLayout, NearLayout, InstanceLayoutCount };
    struct InstanceLayout
    {
        std::array<InstanceDataArray, TreeSegmentCount> segments;
        std::array<std::vector<int>, TreeSegmentCount> branchIds; // Per instance in segments.
        std::array<UINT, TreeSegmentCount> start = {};
        UINT leafCount = 0;
        UINT stickCount = 0;
    };
    std::array<InstanceLayout, InstanceLayoutCount> m_instanceLayouts;
    std::vector<int> m_layoutOffsets; // Scratch: each branch frame's slot in the layout being built.

    // Motion vectors: each branch's instance slot in the last two frames, for the tree's own instances and for each
    // layout. Branch slots shift as branches appear or change segment type, so InstancedData::PreviousSlot is looked
    // up here by branch ID.
    struct SlotHistory
    {
        std::vector<UINT> slots[2];
        std::vector<UINT> frames[2]; // Frame number plus one; zero is never written.
    };
    std::array<SlotHistory, 1 + InstanceLayoutCount> m_slotHistory;
    void LinkPreviousSlots(InstancedData* instances, UINT firstSlot, const std::vector<int>& branchIds,
        SlotHistory& history, UINT frame);
    int m_instanceCount = 0;
    bool m_instanceLayoutsValid = false;
    int m_instanceLayoutDepthLOD = 0;

    Tree* InstanceSource() const { return static_cast<Tree*>(_params->instanceSource); }
    TreeSegment SelectLayoutSegment(const BranchInstance& frame, InstanceLayoutKind layout) const;
    void BuildInstanceLayouts(UINT startInstance, int* opacityMicromaps);
    HRESULT ComputeInstanceConstants(IRenderFrame* pFrameConfig);

    HRESULT ComputeBranchVectorAtTime(BranchInstance& frame, RenderData* pRenderData);
    HRESULT ComputeBranchInstanceSkinningMatrix(BranchInstance& frame, RenderData* pRenderData, int startInstance);

    TreeSegment SelectSegmentType(BranchInstance& frame, RenderData* pRenderData) const;

    HRESULT ComputeBranchVectorEndPoint(XMVECTOR* vComputedEnd, float time, BranchInstance& frame);
    HRESULT ComputeTransformationsManual(XMMATRIX* computedTransform, BranchInstance& frame);

public:
    Tree(WorldObjectParams* pParams);
    virtual ~Tree(void);

    virtual void Create(ModelGenerator* generator) { return Create((TreeModelGenerator*)generator); }
    void Create(TreeModelGenerator* generator);
    virtual ObjectType GetObjectType() { return ObjectType_Tree; }

    virtual HRESULT InitGraphics(RenderManager& renderManager);

    virtual HRESULT ComputeConstants(IRenderFrame* pFrameConfig) override;
    void AccumulateProfile(RenderProfile& profile) const;
    virtual unsigned int GetNumInstances();
    virtual unsigned int GetMaxInstances();
    unsigned int GetMaxDrawnInstances() override;

    // Called once per instance before graphics are initialized; reserves the instance layouts.
    void RegisterInstance() { ++m_instanceCount; }
};
