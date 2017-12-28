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
    std::vector<InstancedData>            _logInstanceData;
    std::vector<InstancedData>            _twigInstanceData;
    std::vector<InstancedData>            _leafInstanceData;

    RenderUnit*                            m_logUnit;
    RenderUnit*                            m_twigUnit;
    RenderUnit*                            m_leafUnit;

    struct TreeFrame
    {
        RenderData* renderData;
        int currentBranch;
        Branch* branch;
        XMFLOAT3 startPosition;
    };

    int numTreeFrames;
    static const int maxTreeFrameQueueSize = 256;
    TreeFrame m_treeFrames[maxTreeFrameQueueSize];

    HRESULT ComputeBranchInstanceData(TreeFrame frame);

    HRESULT ComputeTransformationsManual(XMMATRIX* transform, XMVECTOR* vChildStart, float time, Branch const* branch, FXMVECTOR parentStart);


public:
    Tree(WorldObjectParams* pParams);
    virtual ~Tree(void);

    virtual void Create(ModelGenerator* generator) { return Create((TreeModelGenerator*)generator); }
    void Create(TreeModelGenerator* generator);
    virtual ObjectType GetObjectType() { return TreeType; }

    virtual HRESULT InitGraphics(RenderManager& renderManager);

    virtual HRESULT ComputeConstants(IRenderFrame* pFrameConfig) override;
    virtual unsigned int GetNumInstances();
    virtual unsigned int GetMaxInstances();
};

