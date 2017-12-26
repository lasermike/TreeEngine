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

    // Create material, mesh, and reserve render unit
    Material* pTrunk = nullptr;
    renderManager.CreateMaterial(L"trunk", _params->textureFilename[0].c_str(), nullptr, nullptr, _params->materials[0], StockRenderState(), &pTrunk);
    Mesh* pNewMesh = nullptr;
    const GeometryBufferData::BufferIndices* pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Cylinder);
    renderManager.CreateMesh(L"trunk", renderManager.GetPlatform()->GetVertexBuffer(), renderManager.GetPlatform()->GetIndexBuffer(), pBufferIndices, &pNewMesh);
    renderManager.ReserveRenderUnit(pTrunk, pNewMesh, this, &m_logUnit);

    Material* pTwig = nullptr;
    renderManager.CreateMaterial(L"twig", _params->textureFilename[0].c_str(), nullptr, nullptr, _params->materials[0], StockRenderState(), &pTwig);
    pNewMesh = nullptr;
    pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Box);
    renderManager.CreateMesh(L"twig", renderManager.GetPlatform()->GetVertexBuffer(), renderManager.GetPlatform()->GetIndexBuffer(), pBufferIndices, &pNewMesh);
    renderManager.ReserveRenderUnit(pTwig, pNewMesh, this, &m_twigUnit);

    Material* pLeaf = nullptr;
    renderManager.CreateMaterial(L"leaf", L"", nullptr, nullptr, _params->materials[1], StockRenderState(), &pLeaf);
    pNewMesh = nullptr;
    pBufferIndices = renderManager.GetGeometryBufferData().GetBufferIndices(PrimitiveType_Box);
    renderManager.CreateMesh(L"leaf", renderManager.GetPlatform()->GetVertexBuffer(), renderManager.GetPlatform()->GetIndexBuffer(), pBufferIndices, &pNewMesh);
    renderManager.ReserveRenderUnit(pLeaf, pNewMesh, this, &m_leafUnit);

    return S_OK;
}

HRESULT Tree::ComputeConstants(IRenderFrame* pFrameConfig)
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants");

    UINT startInstance = 0;
    HRR(pFrameConfig->GetInstanceIndex(this, startInstance));

    InstancedData* dataView = pFrameConfig->GetRenderData().instanceData;

    // TODO handle scale

    // Clear bounding box
    _boundingBox[0] = XMFLOAT3(-1, -1, -1);
    _boundingBox[1] = XMFLOAT3(1, 1, 1);

    for (int i = 0; i < NUM_EXTENTS; i++)
    {
        _extents[i] = XMFLOAT3(0, 0, 0);
    }

    _logInstanceData.clear();
    _twigInstanceData.clear();
    _leafInstanceData.clear();
    int currentBranch = 0;

    // Queue first branch
    TreeFrame frame = { &pFrameConfig->GetRenderData(), currentBranch, _treeModel->trunk, _position };
    m_treeFrames.push(frame);

    while (!m_treeFrames.empty())
    {
        TreeFrame frame = m_treeFrames.front();
        m_treeFrames.pop();
        ComputeBranchInstanceData(frame);
    }

    InstancedData* logBuffer = dataView + startInstance;
    InstancedData* twigBuffer = dataView + startInstance + _logInstanceData.size();
    InstancedData* leafBuffer = dataView + startInstance + _logInstanceData.size() + _twigInstanceData.size();

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Compute Constants memcpy");

    // TODO add to render unit specific data view
    if (_logInstanceData.size())
    {
        memcpy(logBuffer, &_logInstanceData[0], _logInstanceData.size() * sizeof(InstancedData));
    }

    if (_twigInstanceData.size() > 0)
    {
        memcpy(twigBuffer, &_twigInstanceData[0], _twigInstanceData.size() * sizeof(InstancedData));
    }

    if (_leafInstanceData.size() > 0)
    {
        memcpy(leafBuffer, &_leafInstanceData[0], _leafInstanceData.size() * sizeof(InstancedData));
    }

    pFrameConfig->SetInstances(m_logUnit, this, startInstance, (UINT)_logInstanceData.size());
    pFrameConfig->SetInstances(m_twigUnit, this, startInstance + (UINT)_logInstanceData.size(), (UINT)_twigInstanceData.size());
    pFrameConfig->SetInstances(m_leafUnit, this, startInstance + (UINT)_logInstanceData.size() + (UINT)_twigInstanceData.size(), (UINT)_leafInstanceData.size());

    PIXEndEvent();

    return S_OK;
}

HRESULT Tree::ComputeBranchInstanceData(TreeFrame& frame)
{
    return ComputeBranchInstanceData(frame.renderData, frame.currentBranch, frame.branch, &frame.startPosition);
}

HRESULT Tree::ComputeBranchInstanceData(RenderData* pRenderData, int& currentBranch, Branch const* branch, XMFLOAT3* parentStart)
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants ComputeTransformationsManual");

    if (CalcTime(pRenderData->time) < branch->depth)
        return S_OK;

    pRenderData->frameStats[WORLD_MATRIX_COMPUTED_STAT].stat++;

    XMVECTOR vChildStart;
    XMMATRIX localToWorld;
    XMVECTOR vParentStart = XMVectorSelect(g_XMOne, XMLoadFloat3(parentStart), g_XMSelect1110.v);
    
    ComputeTransformationsManual(&localToWorld, &vChildStart, CalcTime(pRenderData->time), branch, &pRenderData->world, vParentStart);

    localToWorld = XMMatrixMultiply(localToWorld, XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation)));

    InstancedData data;
    XMStoreFloat4x4(&data.World, localToWorld);

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants push_back");

    // Decide which geometry model to use
    switch (branch->geometryType)
    {
    case Leaf:
        _leafInstanceData.push_back(data);
        pRenderData->frameStats[NUM_LEAVES_STAT].stat++;
        break;
    case Stick:
        if (_params->depthLOD != -1 && (branch->depth < _params->depthLOD || XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(parentStart) - pRenderData->eyePos)) < 100.0f))
        {
            _logInstanceData.push_back(data);
        }
        else
        {
            _twigInstanceData.push_back(data);
        }
        pRenderData->frameStats[NUM_STICKS_STAT].stat++;

        break;
    }

    PIXEndEvent();
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeConstants ComputeBranchInstanceData");

    //Compute bounding box
    XMStoreFloat3(&_boundingBox[0], XMVectorMin(XMVector3Transform(XMVectorSet(-1.0f, -1.0f, -1.0f, 0), localToWorld),
        XMLoadFloat3(&_boundingBox[0])));
    XMStoreFloat3(&_boundingBox[1], XMVectorMax(XMVector3Transform(XMVectorSet(1.0f, 1.0f, 1.0f, 0), localToWorld),
        XMLoadFloat3(&_boundingBox[1])));

    currentBranch++;

    // Compute child branches
    for (unsigned int c = 0; c < branch->children.size(); c++)
    {
        if (branch->Child(c) != 0)
        {
            Branch* child = &_treeModel->treeData.pBranches[branch->Child(c)];

            XMFLOAT3 childStart;
            XMStoreFloat3(&childStart, vChildStart);

            TreeFrame frame = { pRenderData, currentBranch, child, childStart };
            m_treeFrames.push(frame);
        }
    }

    PIXEndEvent();

    return S_OK;
}

int LookAt(float x1, float y1, float z1, float x2, float y2, float z2, XMFLOAT4X4* matrix)
{
    /* Build a transform as if you were at a point (x1,y1,z1), and
    looking at a point (x2,y2,z2) */

    float ViewOut[3];      // the View or "new Z" vector
    float ViewUp[3];       // the Up or "new Y" vector
    float ViewRight[3];    // the Right or "new X" vector

    float ViewMagnitude;   // for normalizing the View vector
    float UpMagnitude;     // for normalizing the Up vector
    float UpProjection;    // magnitude of projection of View Vector on World UP

    const float WorldUp[3] = { 0, 1, 0 };

    // first, calculate and normalize the view vector
    ViewOut[0] = x2 - x1;
    ViewOut[1] = y2 - y1;
    ViewOut[2] = z2 - z1;
    ViewMagnitude = sqrt(ViewOut[0] * ViewOut[0] + ViewOut[1] * ViewOut[1] +
        ViewOut[2] * ViewOut[2]);

    // invalid points (not far enough apart)
    if (ViewMagnitude < .000001)
        return (-1);

    // normalize. This is the unit vector in the "new Z" direction
    ViewOut[0] = ViewOut[0] / ViewMagnitude;
    ViewOut[1] = ViewOut[1] / ViewMagnitude;
    ViewOut[2] = ViewOut[2] / ViewMagnitude;

    // Now the hard part: The ViewUp or "new Y" vector

    // dot product of ViewOut vector and World Up vector gives projection of
    // of ViewOut on WorldUp
    UpProjection = ViewOut[0] * WorldUp[0] + ViewOut[1] * WorldUp[1] +
        ViewOut[2] * WorldUp[2];

    // first try at making a View Up vector: use World Up
    ViewUp[0] = WorldUp[0] - UpProjection*ViewOut[0];
    ViewUp[1] = WorldUp[1] - UpProjection*ViewOut[1];
    ViewUp[2] = WorldUp[2] - UpProjection*ViewOut[2];

    // Check for validity:
    UpMagnitude = ViewUp[0] * ViewUp[0] + ViewUp[1] * ViewUp[1] + ViewUp[2] * ViewUp[2];

    if (UpMagnitude < .0000001)
    {
        //Second try at making a View Up vector: Use Y axis default  (0,1,0)
        ViewUp[0] = -ViewOut[1] * ViewOut[0];
        ViewUp[1] = 1 - ViewOut[1] * ViewOut[1];
        ViewUp[2] = -ViewOut[1] * ViewOut[2];

        // Check for validity:
        UpMagnitude = ViewUp[0] * ViewUp[0] + ViewUp[1] * ViewUp[1] + ViewUp[2] * ViewUp[2];

        if (UpMagnitude < .0000001)
        {
            //Final try at making a View Up vector: Use Z axis default  (0,0,1)
            ViewUp[0] = -ViewOut[2] * ViewOut[0];
            ViewUp[1] = -ViewOut[2] * ViewOut[1];
            ViewUp[2] = 1 - ViewOut[2] * ViewOut[2];

            // Check for validity:
            UpMagnitude = ViewUp[0] * ViewUp[0] + ViewUp[1] * ViewUp[1] + ViewUp[2] * ViewUp[2];

            if (UpMagnitude < .0000001)
                return(-1);
        }
    }

    // normalize the Up Vector
    UpMagnitude = sqrt(UpMagnitude);
    ViewUp[0] = ViewUp[0] / UpMagnitude;
    ViewUp[1] = ViewUp[1] / UpMagnitude;
    ViewUp[2] = ViewUp[2] / UpMagnitude;

    // Calculate the Right Vector. Use cross product of Out and Up.
    ViewRight[0] = ViewOut[1] * ViewUp[2] + ViewOut[2] * ViewUp[1];
    ViewRight[1] = ViewOut[2] * ViewUp[0] + ViewOut[0] * ViewUp[2];
    ViewRight[2] = ViewOut[0] * ViewUp[1] + ViewOut[1] * ViewUp[0];

    // Plug values into rotation matrix R
    matrix->m[0][0] = ViewRight[0];
    matrix->m[0][1] = ViewRight[1];
    matrix->m[0][2] = ViewRight[2];
    matrix->m[0][3] = 0;

    matrix->m[2][0] = ViewUp[0];
    matrix->m[2][1] = ViewUp[1];
    matrix->m[2][2] = ViewUp[2];
    matrix->m[2][3] = 0;

    matrix->m[1][0] = ViewOut[0];
    matrix->m[1][1] = ViewOut[1];
    matrix->m[1][2] = ViewOut[2];
    matrix->m[1][3] = 0;

    //matrix->m[3][0] = 0;// x1;
    //matrix->m[3][1] = 0;// y1;
    //matrix->m[3][2] = 0;// z1;
    //matrix->m[3][3] = 1;

    // Plug values into translation matrix T
    //MoveFill(ViewMoveMatrix, -x1, -y1, -z1);

    // build the World Transform
    //MatrixMultiply(ViewRotationMatrix, ViewMoveMatrix, WorldTransform);

    return(0);
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
    CXMVECTOR RotationQuaternion,
    FXMMATRIX RotationMat,
    CXMVECTOR Translation
)
{
    XMMATRIX M;
    XMMATRIX MScaling;
    XMMATRIX MRotation;
    XMVECTOR VTranslation;

    MScaling = XMMatrixScalingFromVector(Scaling);
    MRotation = XMMatrixRotationQuaternion(RotationQuaternion);
    VTranslation = _mm_and_ps(Translation, g_XMMask3);

    M = matNegOrigin;
    M = XMMatrixMultiply(M, MScaling);
    M = XMMatrixMultiply(M, MRotation);
    M.r[3] = XMVectorAdd(M.r[3], origin);
    M.r[3] = XMVectorAdd(M.r[3], VTranslation);

    return M;
}


HRESULT Tree::ComputeTransformationsManual(XMMATRIX* computedTransform, XMVECTOR* vComputedEnd, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart)
{
    float animScaleFactor = 1.0f;
    if (time - 5 < branch->depth)
    {
        animScaleFactor = (time - branch->depth) / 5;
    }
    else
    {
        animScaleFactor = animScaleFactor;
    }

    ASSERT(animScaleFactor >= 0.0f);

    XMVECTOR vStart = parentStart;
    XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end)) + XMLoadFloat3(&_position);

    // Scale branch
    XMVECTOR vMag = XMVector3Length(vEnd - vStart);
    XMVECTOR vScale = g_XMOne; // *.3f;

    switch (branch->geometryType)
    {
    case Leaf:
        vScale = XMVectorSet(XMVectorGetX(vMag) * 0.5f, XMVectorGetX(vMag), 0.004f, 0) * animScaleFactor;
        break;
    case Stick:
        vScale = XMVectorSet(branch->thickness, XMVectorGetX(vMag), branch->thickness, 0) * animScaleFactor;
        break;
    }

    // Child start pos
    XMVECTOR startToEnd = vEnd - vStart;

    XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
    *vComputedEnd = startToEnd * vMagY + vStart;

    XMMATRIX computedMatrix;

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
        //XMFLOAT4X4 mat;
        //XMStoreFloat4x4(&mat, XMMatrixIdentity());
        //LookAt(branch->start.x, branch->start.y, branch->start.z,
        //       branch->end.x, branch->end.y, branch->end.z, &mat);
        //computedMatrix = XMLoadFloat4x4(&mat);
        ////computedMatrix = XMMatrixTranspose(computedMatrix);

        XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
        computedMatrix = TEMatrixLookToLH(vStart, vDir, vUp);
        computedMatrix.r[3] = vStart;
    }

//    computedMatrix = XMMatrixMultiply(XMMatrixScalingFromVector(vScale), computedMatrix);

    *computedTransform = computedMatrix;

#else

    // Determine rotation
    XMVECTOR vCross = XMVector3Cross(vUp, vDir);
    XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
    XMVECTOR vQuat;
    XMMATRIX matRotation;
    float crossLenSq;
    XMStoreFloat(&crossLenSq, vCrossLenSq);
    if (crossLenSq > 0.001f) // Need better value for epsilon here
    {
        XMVECTOR vDot = XMVector3Dot(vUp, vDir);
        float angle;
        XMStoreFloat(&angle, vDot);
        angle = acos(angle);
        vQuat = XMQuaternionRotationAxis(vCross, angle);
        matRotation = XMMatrixLookToLH(vStart, vDir, vUp);
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

    *computedTransform = TEMatrixTransformation(vScale, vQuat, matRotation, vStart);
    //*computedTransform = TEMatrixTransformation(vScale, vQuat, vStart);


#endif

    // Apply object local to world transform
//    *computedTransform = *computedTransform * XMLoadFloat4x4(world);

    // Compute extents.  Keep these in local coordinates if we can.
    if (XMVectorGetY(vEnd) > _extents[TOP].y)
        XMStoreFloat3(&_extents[TOP], vEnd);

    return S_OK;
}

HRESULT Tree::ComputeTransformations(XMMATRIX* transform, XMMATRIX* normalTransform, XMVECTOR* vChildStart, float time, Branch const* branch, XMFLOAT4X4* world, FXMVECTOR parentStart)
{
    float animScaleFactor = 1.0f;
    if (time - 5 < branch->depth)
    {
        animScaleFactor = (time - branch->depth) / 5;
    }

    XMVECTOR vStart = parentStart;
    XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end)) + XMLoadFloat3(&_position);

    // Scale branch
    XMVECTOR vMag = XMVector3Length(vEnd - vStart);
    float magY = XMVectorGetX(vMag);
    float magXZ = branch->thickness;
    magY *= animScaleFactor;
    magXZ *= animScaleFactor;
    XMVECTOR vScale = XMVectorSet(magXZ, magY, magXZ, 0);

    // Child start pos
    XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
    *vChildStart = (vEnd - vStart) * vMagY + vStart;

    // Determine rotation
    XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
    XMVECTOR vDiff = vEnd - vStart;
    XMVECTOR vCross = XMVector3Cross(vUp, XMVector3Normalize(vDiff));
    XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
    XMVECTOR vQuat;
    float crossLenSq;
    XMStoreFloat(&crossLenSq, vCrossLenSq);
    if (crossLenSq > 0.01f) // Need better value for epsilon here
    {
        XMVECTOR vDot = XMVector3Dot(vUp, XMVector3Normalize(vDiff));
        float angle;
        XMStoreFloat(&angle, vDot);
        angle = acos(angle);
        vQuat = XMQuaternionRotationAxis(vCross, angle);
    }
    else
    {
        vQuat = XMQuaternionRotationAxis(vUp, 0);
    }

    const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0);
    const XMVECTOR vScaleCenter = XMVectorSet(0, -0.5, 0, 0);
    *transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

    *transform = *transform * XMLoadFloat4x4(world);  //TODO
    *normalTransform = MathHelper::InverseTranspose(XMMatrixTranspose(*transform));

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
        assert((UINT)_treeModel->treeData.numBranches >= _logInstanceData.size() + _twigInstanceData.size() + _leafInstanceData.size());
        return (unsigned int)(_logInstanceData.size() + _twigInstanceData.size() + _leafInstanceData.size());
    }
    else
        return 0;
}
