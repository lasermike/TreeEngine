#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "RenderManager.h"
#include "ThreadPool.h"
#include "Tree.h"
#include <chrono>

SceneRoot::~SceneRoot()
{
    for (auto i = _children.begin(); i != _children.end(); i++)
    {
        SafeDelete(&(*i));
    }
}

XMVECTOR SceneRoot::GetExtents(Extent extent)
{
    XSF_ASSERT(extent == TOP);

    auto i = _children.begin() ;
    XMVECTOR retval = (*i)->GetExtents(extent);
    i++;
    for (; i != _children.end(); i++)
    {
        XMVECTOR cur = (*i)->GetExtents(extent);
        if (XMVectorGetY(cur) > XMVectorGetY(retval))
        {
            retval = cur;
        }
    }

    return retval;
}

UINT32 SceneRoot::GetMaxInstances()
{
    // Determine number of instances
    unsigned int numInstances = 0;
    for (auto j : _children)
    {
        numInstances += j->GetMaxInstances();
    }

    return numInstances;
}

void SceneRoot::AppendGeometry(GeometryBufferData& geometryData)
{
    for (auto child : _children)
    {
        child->AppendGeometry(geometryData);
    }
}

HRESULT SceneRoot::InitGraphics(RenderManager& renderManager)
{
    for (auto i : _children)
    {
        HRR(i->InitGraphics(renderManager));
    }

    return S_OK;
}

HRESULT SceneRoot::CleanUpDeviceObjects()
{
    for (auto child : _children)
    {
        child->CleanUpDeviceObjects();
    }

    return S_OK;
}

void SceneRoot::DeleteAllChildren()
{
    CleanUpDeviceObjects();
    for (WorldObject* child : _children)
    {
        delete child;
    }
    _children.clear();
}


HRESULT SceneRoot::Update(IRenderFrame& renderFrame, ThreadPool& threadPool, bool synchronous)
{
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"SceneRoot::Update");

    HRESULT hr = S_OK;
    auto& profile = renderFrame.GetRenderData().profile;
    std::chrono::steady_clock::time_point updateStart;
    if (profile.enabled)
        updateStart = std::chrono::steady_clock::now();

    if (synchronous)
    {
        for (WorldObject* child : _children)
            child->ComputeConstants(&renderFrame);
    }
    else
    {
        UINT numObjs = (UINT) _children.size();
        UINT objsPerThread = std::max(1U, numObjs / threadPool.GetNumWorkers());

        WorkData threadData = { 0, 0 /*first*/, objsPerThread /*end*/, &renderFrame };

        //std::list<WorkData> workData;

        for (UINT i = 0; i < threadPool.GetNumWorkers() && threadData.end <= numObjs; i++)
        {
            if (i + 1 == threadPool.GetNumWorkers()) // Make sure the last iteration has all remaining objs
            {
                threadData.end = numObjs;
            }

            //workData.push_back(threadData);
            threadPool.enqueue([this](WorkData data)
                               {
                                   UINT num = 0;
                                   for (auto c : _children)
                                   {
                                       if (num >= data.start && num < data.end)
                                       {
                                           c->ComputeConstants((IRenderFrame*) data.param1);
                                       }
                                       num++;
                                   }
                               }, threadData);
            threadData.start  += objsPerThread;
            threadData.end  += objsPerThread;
        }

        threadPool.WaitTilDone();
    }

    _boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);
    XMVECTOR bbmin = XMLoadFloat3(&_boundingBox[0]);
    XMVECTOR bbmax = XMLoadFloat3(&_boundingBox[1]);

    for (WorldObject* c : _children)
    {
        bbmin = XMVectorMin(bbmin, XMLoadFloat3(&c->GetBoundingBox()[0]));
        bbmax = XMVectorMax(bbmax, XMLoadFloat3(&c->GetBoundingBox()[1]));
        if (profile.enabled && c->GetObjectType() == ObjectType_Tree)
            static_cast<Tree*>(c)->AccumulateProfile(profile);
    }

    XMStoreFloat3(&_boundingBox[0], bbmin);
    XMStoreFloat3(&_boundingBox[1], bbmax);
    if (profile.enabled)
        profile.sceneUpdateMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - updateStart).count();

    return hr;
}
