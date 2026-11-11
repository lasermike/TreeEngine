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
    bool m_branchInstancesValid = false;    // m_segmentInstanceData holds this pose's per-branch instances.
    XMFLOAT3 m_lastEyePosition = {};
    int m_lastDepthLOD = 0;
    float m_lodScale = 1;           // This frame's Lod::Scale for the tree (see LodScale).
    float m_lastLodScale = 0;
    float LodScale(const RenderData& renderData, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax, bool boxValid) const;
    int m_instanceLodLevel = -1;    // An instance's last whole-instance level, for hysteresis; -1 before the first.
    UINT m_lastInstanceStart = 0;
    // The instance slots written this frame (see AppendInstanceUploadRanges), and the frame they were written for.
    std::vector<std::pair<UINT, UINT>> m_uploadRanges;
    UINT m_uploadRangesFrame = UINT_MAX;
    // This frame's whole-tree LOD level (-1 for none) and the leaves and sticks drawn, for the profile.
    int m_profileLevel = -1;
    UINT m_profileLeaves = 0;
    UINT m_profileSticks = 0;
    UINT m_cachedLeafCount = 0;
    UINT m_cachedStickCount = 0;

    // Instancing. A source tree with instances publishes camera-independent branch layouts after its own: far
    // (depth-only LOD) and near (every stick at full detail), then one reduced layout per lod distance beyond the
    // second (see Lod::Reduction). Each instance draws one with its own object transform, chosen by its LOD level,
    // so instances need no per-branch CPU work.
    enum InstanceLayoutKind { FarLayout = 0, NearLayout = 1, FirstReducedLayout = 2 };
    size_t InstanceLayoutCount() const
    {
        return FirstReducedLayout + (_params->lod.distances.size() > 1 ? _params->lod.distances.size() - 1 : 0);
    }
    struct InstanceLayout
    {
        std::array<InstanceDataArray, TreeSegmentCount> segments;
        std::array<std::vector<int>, TreeSegmentCount> branchIds; // Per instance in segments.
        std::array<std::vector<int>, TreeSegmentCount> opacityMicromaps; // Per instance in segments.
        std::array<UINT, TreeSegmentCount> start = {};
        UINT leafCount = 0;
        UINT stickCount = 0;
        bool valid = false;     // Built for the current pose (a tree without instances builds only the one it draws).
    };
    std::vector<InstanceLayout> m_instanceLayouts;
    // A layout's branches before slots are assigned, parents before children.
    struct LayoutNode
    {
        XMFLOAT4X4 world;
        const Branch* branch;   // The branch it draws, or the first of those it merges.
        int parent;             // Index in the node list, or -1.
        TreeSegment segment;
    };
    std::vector<LayoutNode> m_layoutNodes;          // Scratch for BuildInstanceLayouts.
    std::vector<std::vector<int>> m_frameChildren;  // Scratch: each branch frame's child frames.
    std::vector<int> m_branchHeights;               // Per branch ID: the longest path down to a tip (a leaf is 0).
    void CollectFullLayout(InstanceLayoutKind kind);
    void CollectReducedLayout(int reduction);
    // Reduced layouts drop sticks narrower than this many pixels where they are first drawn.
    static constexpr float MinStickPixels = 0.5f;

    // Motion vectors: each branch's instance slot in the last two frames, for the tree's own instances and for each
    // layout. Branch slots shift as branches appear or change segment type, so InstancedData::PreviousSlot is looked
    // up here by branch ID.
    struct SlotHistory
    {
        std::vector<UINT> slots[2];
        std::vector<UINT> frames[2]; // Frame number plus one; zero is never written.
    };
    std::vector<SlotHistory> m_slotHistory;         // The tree's own instances, then each layout.
    void LinkPreviousSlots(InstancedData* instances, UINT firstSlot, const std::vector<int>& branchIds,
        SlotHistory& history, UINT frame);
    int m_instanceCount = 0;
    // _boundingBox holds every drawn branch once a pose has been computed (see ComputeBranchInstanceSkinningMatrix).
    bool m_boundsValid = false;
    bool m_instanceLayoutsValid = false;
    int m_instanceLayoutDepthLOD = 0;
    UINT m_instanceLayoutStart = 0;

    Tree* InstanceSource() const { return static_cast<Tree*>(_params->instanceSource); }
    TreeSegment SelectLayoutSegment(const Branch* branch, bool nearLayout) const;
    static constexpr size_t AllLayouts = SIZE_MAX;
    void BuildInstanceLayouts(UINT startInstance, int* opacityMicromaps, size_t onlyKind = AllLayouts);
    void InvalidateInstanceLayouts();
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
    void AppendInstanceUploadRanges(UINT start, UINT count, UINT frame, std::vector<std::pair<UINT, UINT>>& ranges) override;
    virtual unsigned int GetNumInstances();
    virtual unsigned int GetMaxInstances();
    unsigned int GetMaxDrawnInstances() override;
    bool GetCullBounds(XMFLOAT3 bounds[2]) const override
    {
        if (!m_boundsValid) return false;
        bounds[0] = _boundingBox[0];
        bounds[1] = _boundingBox[1];
        return true;
    }

    // Called once per instance before graphics are initialized; reserves the instance layouts.
    void RegisterInstance() { ++m_instanceCount; }
};
