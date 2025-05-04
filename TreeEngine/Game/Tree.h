#pragma once
#include "pch.h"
#include <vector>
#include <queue>
#include "WorldObject.h"
#include "TreeModel.h"
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

    InstanceDataArray                      m_logInstanceData;
    InstanceDataArray                      m_twigInstanceData;
    InstanceDataArray                      m_leafInstanceData;

    RenderUnit*                            m_logUnit;
    RenderUnit*                            m_twigUnit;
    RenderUnit*                            m_leafUnit;

    enum
    {
        LOG_BRANCH_TYPE,
        TWIG_BRANCH_TYPE,
        LEAF_BRANCH_TYPE,
        MAX_BRANCH_TYPE
    };

    int                                    m_numInstancesPerType[MAX_BRANCH_TYPE];

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

    HRESULT ComputeBranchVectorAtTime(TreeFrame& frame, RenderData* pRenderData);
    HRESULT ComputeBranchInstanceSkinningMatrix(TreeFrame& frame, RenderData* pRenderData, int startInstance);

    bool IsTwig(TreeFrame& frame, RenderData* pRenderData);

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
    virtual unsigned int GetNumInstances();
    virtual unsigned int GetMaxInstances();
};

