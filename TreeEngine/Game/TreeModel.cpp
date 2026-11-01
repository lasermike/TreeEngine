#include "pch.h"
#include "TreeModel.h"


TreeModel::TreeModel(int branchCapacity) 
{
    trunk = nullptr;
    treeData.numBranches = 0;
    treeData.numLevels = 0;
    treeData.branchCapacity = branchCapacity;
    treeData.pBranches = new Branch[branchCapacity];
    treeData.pLevels = new BranchLevelData[maxLevels];

}


TreeModel::~TreeModel(void)
{
    delete [] treeData.pBranches;
    delete [] treeData.pLevels ;
}

