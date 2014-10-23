#include "TreeModel.h"


TreeModel::TreeModel(void) {

	treeData.numBranches = 0;
	trunk = nullptr;
	pBranches = new Branch[maxBranches];
	//memset(&branches, 0, sizeof(Branch) * 250);
}


TreeModel::~TreeModel(void)
{
}

