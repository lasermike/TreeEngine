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
	XMVECTOR vStart = XMVectorSet(0, 0, 0,0);
	XMVECTOR vEnd = XMVectorSet(0, 1.3, 0,0);
	XMVECTOR vDir = vEnd - vStart;
	// Create trunk
	model->trunk = new Branch();
	XMStoreFloat3(&model->trunk->start, vStart);
	XMStoreFloat3(&model->trunk->end, vEnd);
	model->trunk->thickness = .3;
	model->trunk->depth = 0;
	XMStoreFloat4(&model->trunk->quaternion, CalculateQuaternion(vDir));

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

		XMStoreFloat4(&child->quaternion, CalculateQuaternion(vChildDir));
		
		GenerateRecursive(child, depth + 1);
	}

}

XMVECTOR TreeModelGenerator::CalculateQuaternion(FXMVECTOR vDirection)
{
	// Determine rotation
	XMMATRIX mRot;
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
