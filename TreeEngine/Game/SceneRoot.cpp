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

    for (auto& phase : _updatePhases)
        phase.clear();
    for (WorldObject* child : _children)
        _updatePhases[child->IsInstance() ? 1 : 0].push_back(child);
    for (const auto& phase : _updatePhases)
        HRR(ComputeConstants(phase, renderFrame, threadPool, synchronous));

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
    renderFrame.GetRenderData().sceneBoxMin = _boundingBox[0];
    renderFrame.GetRenderData().sceneBoxMax = _boundingBox[1];
    if (profile.enabled)
        profile.sceneUpdateMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - updateStart).count();

    return hr;
}

HRESULT SceneRoot::ComputeConstants(const std::vector<WorldObject*>& objects, IRenderFrame& renderFrame, ThreadPool& threadPool, bool synchronous)
{
    if (synchronous)
    {
        for (WorldObject* object : objects)
            object->ComputeConstants(&renderFrame);
        return S_OK;
    }

    UINT numObjs = (UINT) objects.size();
    if (numObjs == 0)
        return S_OK;
    UINT objsPerThread = std::max(1U, numObjs / threadPool.GetNumWorkers());

    WorkData threadData = { 0, 0 /*first*/, objsPerThread /*end*/, &renderFrame };

    for (UINT i = 0; i < threadPool.GetNumWorkers() && threadData.end <= numObjs; i++)
    {
        if (i + 1 == threadPool.GetNumWorkers()) // Make sure the last iteration has all remaining objs
        {
            threadData.end = numObjs;
        }

        threadPool.enqueue([&objects](WorkData data)
                           {
                               for (UINT num = data.start; num < data.end; num++)
                               {
                                   objects[num]->ComputeConstants((IRenderFrame*) data.param1);
                               }
                           }, threadData);
        threadData.start  += objsPerThread;
        threadData.end  += objsPerThread;
    }

    threadPool.WaitTilDone();
    return S_OK;
}

UINT32 SceneRoot::GetMaxDrawnInstances()
{
    unsigned int numInstances = 0;
    for (auto child : _children)
    {
        numInstances += child->GetMaxDrawnInstances();
    }

    return numInstances;
}