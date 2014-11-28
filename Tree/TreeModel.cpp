#include "TreeModel.h"


TreeModel::TreeModel(void) {

	treeData.numBranches = 0;
	trunk = nullptr;
	pBranches = new Branch[maxBranches];
}


TreeModel::~TreeModel(void)
{
}

