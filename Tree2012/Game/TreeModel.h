#pragma once
#include <vector>
#include <directxmath.h>
#include "Model.h"

using namespace DirectX;

const int maxBranches = 13000; //3^6 + 1 

struct cbBranch
{
    int id[maxBranches];
    int depth[maxBranches];
    XMFLOAT4 start[maxBranches];
    XMFLOAT4 end[maxBranches];
    XMFLOAT4 children[maxBranches];
};


enum GeometryType
{
    Stick,
    Leaf,
    SkinnedStick
};

struct Branch
{
    int id;
    int depth;
    int parent;
    GeometryType geometryType;

    XMFLOAT4 start;
    float    thickness;
    XMFLOAT4 end;

    vector<int> children;

    Branch() : id(0), depth(0), parent(0), geometryType(Stick)
    {
    }

    ~Branch()
    {
        children.clear();
    }

    int Child(int i) const 
    { 
        return children[i];
    }
    void AddChild(int c) 
    {
        children.push_back(c);
    }
};

struct BranchLevelData
{
    int depth;
    int numBranches;

    BranchLevelData() : depth(0), numBranches(0) { }
    ~BranchLevelData() { /*SafeDelete(&pBranchesInLevel);*/ }
};

struct TreeData
{
    int numBranches;
    int numLevels;
    Branch* pBranches;  
    BranchLevelData* pLevels;

    TreeData() : numBranches(0), numLevels(0), pBranches(nullptr), pLevels(nullptr) { }
};

class TreeModel : public Model
{
public:
    TreeModel(void);
    ~TreeModel(void);

    static const int maxLevels = 6;
    Branch* trunk;

    TreeData treeData;

};

