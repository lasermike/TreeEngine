#include "TreeModelGenerator.h"
#include "TreeModel.h"
#include <time.h>

TreeModelGenerator::TreeModelGenerator(void)
{
}


TreeModelGenerator::~TreeModelGenerator(void)
{
}


TreeModel* TreeModelGenerator::Create()
{
	srand((unsigned int) time(NULL));

	TreeModel* model = new TreeModel();

	// Create trunk
	model->trunk = new Branch();
	model->trunk->start = XMFLOAT3(0, 0, 0);
	model->trunk->end = XMFLOAT3(0, 1.3, 0);
	model->trunk->thickness = .3;
	model->trunk->depth = 0;

	GenerateRecursive(model->trunk, 1);
	return model;
}

void TreeModelGenerator::GenerateRecursive(Branch* branch, int depth)
{
	if (depth > 5)
		return;

	int numBranches = depth + rand() % 3;

	for (int i = 0; i < numBranches; i++)
	{
		Branch* child = new Branch();
		branch->branches.push_back(child);
		child->depth = depth;
		child->thickness = branch->thickness / (depth * 1.1f);
		child->start = branch->end;

		const XMVECTORF32 vX = { 1, 0, 0, 0 };
		const XMVECTORF32 vZ = { 0, 0, 1, 0 };
		const double maxAngle = XM_PIDIV2;
		const double maxAngleDiv2 = maxAngle / 2.0;

		XMVECTOR vParentDir = XMVector3Normalize(XMLoadFloat3(&branch->end) - XMLoadFloat3(&branch->start));
		XMVECTOR vChildDir = XMVector3Rotate(vParentDir, XMQuaternionRotationAxis(vX, (maxAngle * rand()) / RAND_MAX - maxAngleDiv2));
		vChildDir = XMVector3Rotate(vChildDir, XMQuaternionRotationAxis(vZ, (maxAngle * rand()) / RAND_MAX - maxAngleDiv2));
		child->end = branch->end;
		
		child->end.x = branch->end.x + XMVectorGetX(vChildDir) ;
		child->end.y = branch->end.y + XMVectorGetY(vChildDir) ;
		child->end.z = branch->end.z + XMVectorGetZ(vChildDir) ;

		child->relStart = XMFLOAT3(0,0,0);
		XMStoreFloat3(&child->relEnd, vChildDir);


		/*
		const float randScale = RAND_MAX ;		
		XMVECTOR vRand = XMVectorSet(
			rand() / randScale - 0.5f,
			rand() / randScale - 0.1f,
			rand() / randScale - 0.5f, 
			0);
		vRand = XMVector3Normalize(vRand);
		vRand = vRand * (0.5f + (0.5f / depth));

		child->end.x = branch->end.x + XMVectorGetX(vRand);
		child->end.y = branch->end.y + XMVectorGetY(vRand);
		child->end.z = branch->end.z + XMVectorGetZ(vRand);
		*/

		GenerateRecursive(child, depth + 1);
	}

}

TreeModel* TreeModelGenerator::CreateTestTree()
{
	TreeModel* model = new TreeModel();

	// Create trunk
	model->trunk = new Branch();
	model->trunk->start = XMFLOAT3(0, 0, 0);
	model->trunk->end = XMFLOAT3(0, 1, 0);
	model->trunk->thickness = .2;

		Branch* branch;
		branch = new Branch();
		branch->start = XMFLOAT3(0, 1, 0);
		branch->end = XMFLOAT3(.7, 1.5, 1);
		branch->thickness = .1;
		model->trunk->branches.push_back(branch);

			Branch* childBranch = new Branch();
			childBranch->start = XMFLOAT3(.7, 1.5, 1);
			childBranch->end = XMFLOAT3(.5, 2.1, 1.1);
			childBranch->thickness = .05;
			branch->branches.push_back(childBranch);

		branch = new Branch();
		branch->start = XMFLOAT3(0, 1, 0);
		branch->end = XMFLOAT3(1.4, 1.5, 0);
		branch->thickness = .1;
		model->trunk->branches.push_back(branch);

			childBranch = new Branch();
			childBranch->start = XMFLOAT3(1.4, 1.5, 0);
			childBranch->end = XMFLOAT3(1.6, 2.0, 0);
			childBranch->thickness = .05;
			branch->branches.push_back(childBranch);

	return model;
}
