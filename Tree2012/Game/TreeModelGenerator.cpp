#include "pch.h"
#include "TreeModelGenerator.h"
#include "TreeModel.h"
#include <algorithm>

Branch* TreeModelGenerator::AddBranch(Branch* parent, XMFLOAT4& start, XMFLOAT4& end, GeometryType geometryType, float thickness)
{
    int id = _model->treeData.numBranches++;
    Branch* child = &_model->treeData.pBranches[id];

    child->id = id;

    if (parent)
    {
        parent->AddChild(child->id);
    }

    child->parent = parent ? parent->id : -1;
    child->start = start;
    child->end = end;
    child->thickness = thickness;
    child->depth = parent ? parent->depth + 1 : 0;
    child->geometryType = geometryType;

    if (child->depth > _model->treeData.numLevels)
        _model->treeData.numLevels = child->depth;

    return child;
}

FixedTreeModelGenerator::FixedTreeModelGenerator(unsigned int seed)
{
    _seed = seed;
    _model = nullptr;
}

FixedTreeModelGenerator::~FixedTreeModelGenerator(void)
{
}

TreeModel* FixedTreeModelGenerator::Create()
{
    srand(_seed);

    _model = new TreeModel();

    _model->treeData.numLevels = TreeModel::maxLevels;
    for (int i = 0; i < _model->treeData.numLevels; i++)
    {
//        _model->treeData.pLevels[i].pBranchesInLevel = new std::vector<int>();
    }

    XMVECTOR vStart = XMVectorSet(0, 0, 0,0);
    XMVECTOR vEnd = XMVectorSet(0, 1.3f, 0,0);

    // Create trunk
    int id = _model->treeData.numBranches++;
    _model->trunk = &_model->treeData.pBranches[id]; 
    _model->trunk->id = id;
    _model->trunk->parent = -1;

    XMStoreFloat4(&_model->trunk->start, vStart);
    XMStoreFloat4(&_model->trunk->end, vEnd);
    _model->trunk->thickness = .25f;
    _model->trunk->depth = 0;

    GenerateChildrenRecursive(_model->trunk, 1);
    return _model;
}

void FixedTreeModelGenerator::GenerateChildrenRecursive(Branch* parentBranch, int depth)
{
    const int maxDepth = 5;

    if (depth > maxDepth)
        return;
    
    const int maxChildren = 4;

    int numBranches = std::min(depth + rand() % 3, maxChildren);
    //int startRotation = rand() % numBranches;

    XMVECTOR vParentDir = XMVector3Normalize(XMLoadFloat3((XMFLOAT3*)&parentBranch->end) - XMLoadFloat3((XMFLOAT3*)&parentBranch->start));

    for (int i = 0; i < numBranches; i++)
    {
        int id = _model->treeData.numBranches++;
        Branch* child = &_model->treeData.pBranches[id];
        child->id = id;
        //parentBranch->numChildren++;
        parentBranch->AddChild(child->id);
        assert(&_model->treeData.pBranches[child->id] == child);  // Ensure our look up is correct 
//        _model->treeData.pLevels[depth].numBranches++;
//        _model->treeData.pLevels[depth].pBranchesInLevel->push_back(id);

        child->start = parentBranch->end;
        child->depth = depth;
        child->parent = parentBranch->id;

        if (depth < 4)
        {
            child->thickness = parentBranch->thickness * powf(.8f, (float) depth);
        }
        else
        {
            child->thickness = parentBranch->thickness * powf(.7f, (float) depth);
        }

        const XMVECTORF32 vX = { 1, 0, 0, 0 };
        const XMVECTORF32 vZ = { 0, 0, 1, 0 };
        float maxAngle = XM_PIDIV2; // * (1.0f - 1.0f / numBranches);
        float maxAngleDiv2 = maxAngle / 2.0f;

        float randNum = rand() / (float) RAND_MAX;
        XMVECTOR vChildDir = XMVector3Rotate(vParentDir, 
                                             XMQuaternionRotationAxis(vX, (maxAngle * randNum - maxAngleDiv2)));

        randNum = rand() / (float) RAND_MAX; //(((i + startRotation) % numBranches) / (float)numBranches)
        vChildDir = XMVector3Rotate(vChildDir, 
                            XMQuaternionRotationAxis(vZ, maxAngle * randNum - maxAngleDiv2));

        if (depth >= 3)
        {
            float randLen = 0.4f + 0.4f * ((float)rand()) / RAND_MAX;
            vChildDir = XMVectorScale(vChildDir, randLen);
        }
        else if (depth > 0)
        {
            float randLen = 0.5f + 0.4f * ((float)rand()) / RAND_MAX;
            vChildDir = XMVectorScale(vChildDir, randLen);
        }

        child->end.x = parentBranch->end.x + XMVectorGetX(vChildDir) ;
        child->end.y = parentBranch->end.y + XMVectorGetY(vChildDir) ;
        child->end.z = parentBranch->end.z + XMVectorGetZ(vChildDir) ;

        GenerateChildrenRecursive(child, depth + 1);
    }
}

XMVECTOR FixedTreeModelGenerator::CalculateQuaternion(FXMVECTOR vDirection)
{
    // Determine rotation
    XMVECTOR vUp = XMVectorSet(0,1,0,0);
    //XMVECTOR vDiff = child->vEnd - child->vStart;
    XMVECTOR vCross = XMVector3Cross(vUp, XMVector3Normalize(vDirection));
    XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
    XMVECTOR vQuat;
    float crossLenSq;
    XMStoreFloat(&crossLenSq, vCrossLenSq);
    if (crossLenSq > 0.01f) // Need better value for epsilon here
    {
        XMVECTOR vDot = XMVector3Dot(vUp, XMVector3Normalize(vDirection));
        float angle;
        XMStoreFloat(&angle, vDot);
        angle = acos(angle);
        vQuat = XMQuaternionRotationAxis(vCross, angle);
    }
    else
    {
        vQuat = XMQuaternionRotationAxis(vUp, 0);
    }

    return vQuat;
}

