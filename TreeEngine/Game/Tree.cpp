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

Tree::~Tree(void)
{
    SafeDelete(&_treeModel);
}

void Tree::Create(TreeModelGenerator* generator)
{
    _treeModel = generator->Create();
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

    if (_params->materials.size() < 1)
    {
        ShaderMaterial trunkMaterial;
        trunkMaterial.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
        trunkMaterial.Diffuse = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        trunkMaterial.Specular = XMFLOAT4(0, .1f, .1f, 1.0);
        trunkMaterial.flags.y = 1; //useTextures  TODO
        _params->materials.push_back(trunkMaterial);

        ShaderMaterial leafMaterial;
        XMStoreFloat4(&leafMaterial.Diffuse, Colors::Green);
        leafMaterial.Specular = XMFLOAT4(0, .3f, .1f, 1.0);
        leafMaterial.flags.y = false; //useTextures  TODO
        _params->materials.push_back(leafMaterial);
    }

    // Default textures
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
        geometryBuffer = SKINNED_PRIMITIVE_GEOMETRY_BUFFER;
        vsFilename = L"VSSkinned.cso";
        shadowVsFilename = L"BuildShadowMapVSSkinned.cso";
        inputLayout = SKINNED_INPUT_LAYOUT;
    }

    // Create material, mesh, and reserve render unit
    Material* pTrunk = nullptr;
    renderManager.CreateMaterial(L"trunk", _params->textureFilename[0].c_str(), vsFilename.c_str(), nullptr, shadowVsFilename.c_str(), nullptr, _params->materials[0], StockRenderState(), &pTrunk);
    Mesh* pNewMesh = nullptr;
    const GeometryBufferData::BufferOffsets* pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(_params->meshes[0]);
    renderManager.CreateMesh(L"trunk", renderManager.GetPlatform()->GetVertexBuffer(geometryBuffer), renderManager.GetPlatform()->GetIndexBuffer(geometryBuffer), pBufferOffsets, inputLayout, &pNewMesh);
    renderManager.ReserveRenderUnit(pTrunk, pNewMesh, this, &m_logUnit);

    Material* pTwig = nullptr;
    renderManager.CreateMaterial(L"twig", _params->textureFilename[0].c_str(), nullptr, nullptr, nullptr, nullptr, _params->materials[0], StockRenderState(), &pTwig);
    pNewMesh = nullptr;
    pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(PrimitiveType_Box);
    renderManager.CreateMesh(L"twig", renderManager.GetPlatform()->GetVertexBuffer(geometryBuffer), renderManager.GetPlatform()->GetIndexBuffer(geometryBuffer), pBufferOffsets, BASIC_INPUT_LAYOUT, &pNewMesh);
    renderManager.ReserveRenderUnit(pTwig, pNewMesh, this, &m_twigUnit);

    StockRenderState leafState;
    leafState.blendState = StockBlendStates::Overwrite;
    Material* pLeaf = nullptr;
    const wchar_t* leafTexture = _params->textureFilename.size() > 1 ? _params->textureFilename[1].c_str() : nullptr;
    renderManager.CreateMaterial(L"leaf", leafTexture, nullptr, nullptr, nullptr, nullptr, _params->materials[1], leafState, &pLeaf);
    pNewMesh = nullptr;
    pBufferOffsets = renderManager.GetGeometryBufferData().GetBufferOffsets(PrimitiveType_Sprite);
    renderManager.CreateMesh(L"leaf", renderManager.GetPlatform()->GetVertexBuffer(PRIMITIVE_GEOMETRY_BUFFER), renderManager.GetPlatform()->GetIndexBuffer(PRIMITIVE_GEOMETRY_BUFFER), pBufferOffsets, BASIC_INPUT_LAYOUT, &pNewMesh);
    renderManager.ReserveRenderUnit(pLeaf, pNewMesh, this, &m_leafUnit);

    return S_OK;
}

HRESULT Tree::ComputeConstants(IRenderFrame* pFrameConfig)
{
    // 2 passes
    // 1st pass - animate bones and determine number of model groups
    // 2nd pass - build world matrix for each bone

    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants");

    UINT startInstance = 0;
    HRR(pFrameConfig->GetInstanceIndex(this, startInstance));

    InstancedData* dataView = pFrameConfig->GetRenderData().instanceData;

    // Clear bounding box
    _boundingBox[0] = XMFLOAT3(-1, -1, -1);
    _boundingBox[1] = XMFLOAT3(1, 1, 1);

    for (int i = 0; i < NUM_EXTENTS; i++)
    {
        _extents[i] = XMFLOAT3(0, 0, 0);
    }

    m_logInstanceData.clear();
    m_twigInstanceData.clear();
    m_leafInstanceData.clear();
    ZeroMemory(m_numInstancesPerType, sizeof(m_numInstancesPerType));

    if (m_treeFrames.size() < _treeModel->treeData.numBranches)
    {
        m_treeFrames.resize(_treeModel->treeData.numBranches);
    }

    // Queue first branch
    TreeFrame firstFrame = { _treeModel->trunk, nullptr, _position, XMFLOAT3(), XMFLOAT3(), 0xFFFFFFFF, 0, 0 /* next */ };
    m_treeFrames[0] = firstFrame;
    m_numTreeFrames = 1;

    for (int currentFrame = 0; currentFrame < m_numTreeFrames; currentFrame++)
    {
        TreeFrame& frame = m_treeFrames[currentFrame];
        ComputeBranchVectorAtTime(frame, &pFrameConfig->GetRenderData());
    }

    pFrameConfig->GetRenderData().frameStats[WORLD_MATRIX_COMPUTED_STAT].stat = m_numTreeFrames;

    for (int currentFrame = 0; currentFrame < m_numTreeFrames; currentFrame++)
    {
        TreeFrame& frame = m_treeFrames[currentFrame];
        ComputeBranchInstanceSkinningMatrix(frame, &pFrameConfig->GetRenderData(), startInstance);
    }

    InstancedData* logBuffer = dataView + startInstance;
    InstancedData* twigBuffer = dataView + startInstance + m_logInstanceData.size();
    InstancedData* leafBuffer = dataView + startInstance + m_logInstanceData.size() + m_twigInstanceData.size();

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Compute Constants memcpy");

    // TODO add to render unit specific data view
    if (m_logInstanceData.size())
    {
        memcpy(logBuffer, &m_logInstanceData[0], m_logInstanceData.size() * sizeof(InstancedData));
    }

    if (m_twigInstanceData.size() > 0)
    {
        memcpy(twigBuffer, &m_twigInstanceData[0], m_twigInstanceData.size() * sizeof(InstancedData));
    }

    if (m_leafInstanceData.size() > 0)
    {
        memcpy(leafBuffer, &m_leafInstanceData[0], m_leafInstanceData.size() * sizeof(InstancedData));
    }

    //int numInstances = m_logInstanceData.size() + m_twigInstanceData.size() + m_leafInstanceData.size();
    //for (int i = 0; i < numInstances; i++)
    //{
    //    logBuffer[i].InstanceOffset = startInstance + i;
    //}

    pFrameConfig->SetInstances(m_logUnit, this, startInstance, (UINT)m_logInstanceData.size());
    pFrameConfig->SetInstances(m_twigUnit, this, startInstance + (UINT)m_logInstanceData.size(), (UINT)m_twigInstanceData.size());
    pFrameConfig->SetInstances(m_leafUnit, this, startInstance + (UINT)m_logInstanceData.size() + (UINT)m_twigInstanceData.size(), (UINT)m_leafInstanceData.size());

    PIXEndEvent();

    return S_OK;
}

HRESULT Tree::ComputeBranchVectorAtTime(TreeFrame& frame, RenderData* pRenderData)
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
        m_numInstancesPerType[LEAF_BRANCH_TYPE]++;
        break;
    case Stick:
        m_numInstancesPerType[IsTwig(frame, pRenderData) ? TWIG_BRANCH_TYPE : LOG_BRANCH_TYPE]++;
        break;
    }

    ComputeBranchVectorEndPoint(&vChildStart, CalcTime(pRenderData->time), frame);


    // Compute child branches
    for (unsigned int c = 0; c < frame.branch->children.size(); c++)
    {
        if (frame.branch->Child(c) != 0)
        {
            if (c == 0)
            {
                frame.instanceOffsetNext = m_numTreeFrames;
            }

            Branch* child = &_treeModel->treeData.pBranches[frame.branch->Child(c)];

            XMFLOAT3 childStart;
            XMStoreFloat3(&childStart, vChildStart);

            TreeFrame childFrame = { child, &frame, childStart, XMFLOAT3(), XMFLOAT3(), 0xFFFFFFFF, 0xFFFFFFFF, frame.instanceOffset };
            m_treeFrames[m_numTreeFrames++] = childFrame;
        }
    }

    PIXEndEvent();

    return S_OK;
}

bool Tree::IsTwig(TreeFrame& frame, RenderData* pRenderData)
{
    bool isLog =  _params->depthLOD != -1 &&
            (frame.branch->depth < _params->depthLOD || XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&frame.startPosition) - pRenderData->eyePos)) < 100.0f);

    return !isLog;
}

HRESULT Tree::ComputeBranchInstanceSkinningMatrix(TreeFrame& frame, RenderData* pRenderData, int startInstance)
{
    XMMATRIX localToWorld;
    HRESULT hr = ComputeTransformationsManual(&localToWorld, frame);
    if (hr != S_OK)
    {
        return hr;
    }

    localToWorld = XMMatrixMultiply(localToWorld, XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation)));

    InstancedData data;
    XMStoreFloat4x4(&data.World, localToWorld);


    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants push_back");

    int instanceOffset = 0;
    // Decide which geometry model to use
    switch (frame.branch->geometryType)
    {
    case Leaf:
        instanceOffset = startInstance + m_numInstancesPerType[LOG_BRANCH_TYPE] + m_numInstancesPerType[TWIG_BRANCH_TYPE] + m_leafInstanceData.size();
        data.InstanceOffset = instanceOffset;
        if (frame.parentFrame && frame.parentFrame->instanceOffset != 0xFFFFFFFF)
        {
            data.InstanceOffsetPrev = frame.parentFrame->instanceOffset;
        }
        else
        {
            data.InstanceOffsetPrev = instanceOffset;
        }

        m_leafInstanceData.push_back(data);
        pRenderData->frameStats[NUM_LEAVES_STAT].stat++;
        break;
    case Stick:
        if (IsTwig(frame, pRenderData))
        {
            instanceOffset = startInstance + m_numInstancesPerType[LOG_BRANCH_TYPE] + m_twigInstanceData.size();
            data.InstanceOffset = instanceOffset;
            if (frame.parentFrame && frame.parentFrame->instanceOffset != 0xFFFFFFFF)
            {
                data.InstanceOffsetPrev = frame.parentFrame->instanceOffset;
            }
            else
            {
                data.InstanceOffsetPrev = instanceOffset;
            }
            m_twigInstanceData.push_back(data);
        }
        else
        {
            instanceOffset = startInstance + m_logInstanceData.size();
            data.InstanceOffset = instanceOffset;
            if (frame.parentFrame && frame.parentFrame->instanceOffset != 0xFFFFFFFF)
            {
                data.InstanceOffsetPrev = frame.parentFrame->instanceOffset;
            }
            else
            {
                data.InstanceOffsetPrev = instanceOffset;
            }

            data.InstanceOffsetNext = frame.instanceOffsetNext;

            m_logInstanceData.push_back(data);
        }
        pRenderData->frameStats[NUM_STICKS_STAT].stat++;

        break;
    }

    frame.instanceOffset = instanceOffset;

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants ComputeBranchInstanceData");

    //Compute bounding box
    XMStoreFloat3(&_boundingBox[0], XMVectorMin(XMVector3Transform(XMVectorSet(-1.0f, -1.0f, -1.0f, 0), localToWorld),
        XMLoadFloat3(&_boundingBox[0])));
    XMStoreFloat3(&_boundingBox[1], XMVectorMax(XMVector3Transform(XMVectorSet(1.0f, 1.0f, 1.0f, 0), localToWorld),
        XMLoadFloat3(&_boundingBox[1])));

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


HRESULT Tree::ComputeBranchVectorEndPoint(XMVECTOR* vComputedEnd, float time, TreeFrame& frame)
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
        vScale = XMVectorSet(XMVectorGetX(vMag) * 0.5f, XMVectorGetX(vMag), 0.004f, 0) * animScaleFactor;
        break;
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

HRESULT Tree::ComputeTransformationsManual(XMMATRIX* computedTransform, TreeFrame& frame)
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
    if (_treeModel)
    {
        return _treeModel->treeData.numBranches;
    }
    else
        return 0;

}

unsigned int Tree::GetNumInstances()
{
    if (_treeModel)
    {
        assert((UINT)_treeModel->treeData.numBranches >= m_logInstanceData.size() + m_twigInstanceData.size() + m_leafInstanceData.size());
        return (unsigned int)(m_logInstanceData.size() + m_twigInstanceData.size() + m_leafInstanceData.size());
    }
    else
        return 0;
}
