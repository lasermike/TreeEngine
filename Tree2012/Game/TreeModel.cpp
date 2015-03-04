#include "pch.h"
#include "TreeModel.h"


TreeModel::TreeModel(void) 
{
	trunk = nullptr;
	treeData.numBranches = 0;
	treeData.numLevels = 0;
	treeData.pBranches = new Branch[maxBranches];
	treeData.pLevels = new BranchLevelData[maxLevels];

}


TreeModel::~TreeModel(void)
{
	SafeDelete(&treeData.pBranches);
	SafeDelete(&treeData.pLevels);
}

