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

    std::queue<TreeFrame> m_treeFrames;

    HRESULT ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, XMFLOAT3* parentStart);
    HRESULT ComputeBranchInstanceData(TreeFrame& frame);

    HRESULT ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, const FXMVECTOR parentStart);
    HRESULT ComputeTransformationsManual(XMMATRIX* transform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart);


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

