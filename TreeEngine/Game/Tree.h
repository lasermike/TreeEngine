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
    std::array<RenderUnit*, TreeSegmentCount> m_segmentRenderUnits = {};
    std::array<int, TreeSegmentCount> m_numInstancesPerSegment = {};

    struct TreeFrame
    {
        Branch* branch;
        TreeFrame* parentFrame;
        XMFLOAT3 startPosition;
        XMFLOAT3 endPosition;
        XMFLOAT3 scale;
        int instanceOffset;
        int instanceOffsetNext;
        int instanceOffsetPrev;
    };

    int m_numTreeFrames;
    std::vector<TreeFrame> m_treeFrames;
    struct BranchPose
    {
        XMFLOAT4X4 world;
        bool renderable = false;
    };
    std::vector<BranchPose> m_branchPoses;
    float m_matureTime = 0;
    float m_lastPoseTime = 0;
    XMFLOAT3 m_lastPosePosition = {};
    XMFLOAT4 m_lastPoseRotation = {};
    bool m_poseValid = false;
    bool m_reusePose = false;
    XMFLOAT3 m_lastEyePosition = {};
    int m_lastDepthLOD = 0;
    UINT m_lastInstanceStart = 0;
    UINT m_cachedLeafCount = 0;
    UINT m_cachedStickCount = 0;

    HRESULT ComputeBranchVectorAtTime(TreeFrame& frame, RenderData* pRenderData);
    HRESULT ComputeBranchInstanceSkinningMatrix(TreeFrame& frame, RenderData* pRenderData, int startInstance);

    TreeSegment SelectSegmentType(TreeFrame& frame, RenderData* pRenderData) const;

    HRESULT ComputeBranchVectorEndPoint(XMVECTOR* vComputedEnd, float time, TreeFrame& frame);
    HRESULT ComputeTransformationsManual(XMMATRIX* computedTransform, TreeFrame& frame);

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
};
