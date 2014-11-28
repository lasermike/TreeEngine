#include "TreeModelGenerator.h"
#include "TreeModel.h"
#include <algorithm>

TreeModelGenerator::TreeModelGenerator(unsigned int seed)
{
	_seed = seed;
}


TreeModelGenerator::~TreeModelGenerator(void)
{
}


TreeModel* TreeModelGenerator::Create()
{
	srand(_seed);

	model = new TreeModel();
	XMVECTOR vStart = XMVectorSet(0, 0, 0,0);
	XMVECTOR vEnd = XMVectorSet(0, 1.3, 0,0);
	//XMVECTOR vDir = vEnd - vStart;
	// Create trunk
	int id = model->treeData.numBranches++;
	model->trunk = &model->pBranches[id]; //new Branch();
	model->trunk->id = id;
	//model->branchMap.push_back(model->trunk);
	XMStoreFloat4(&model->trunk->start, vStart);
	XMStoreFloat4(&model->trunk->end, vEnd);
	model->trunk->thickness = .3;
	model->trunk->depth = 0;
	//XMStoreFloat4(&model->trunk->quaternion, CalculateQuaternion(vDir));

	GenerateRecursive(model->trunk, 1);
	return model;
}

void TreeModelGenerator::GenerateRecursive(Branch* branch, int depth)
{
	if (depth > 5)
		return;

	int numBranches = std::min(depth + rand() % 3, 4);

	for (int i = 0; i < numBranches; i++)
	{
		int id = model->treeData.numBranches++;
		Branch* child = &model->pBranches[id];
		child->id = id; 
		branch->SetChild(i, child->id);
		assert(&model->pBranches[child->id] == child);  // Ensure our look up is correct 

		child->start = branch->end;
		child->depth = depth;

		if (depth < 4)
		{
			child->thickness = branch->thickness * powf(.8f, depth);
		}
		else
		{
			child->thickness = branch->thickness * powf(.7f, depth);
		}

		const XMVECTORF32 vX = { 1, 0, 0, 0 };
		const XMVECTORF32 vZ = { 0, 0, 1, 0 };
		const double maxAngle = XM_PIDIV2;
		const double maxAngleDiv2 = maxAngle / 2.0;

		XMVECTOR vParentDir = XMVector3Normalize(XMLoadFloat3((XMFLOAT3*)&branch->end) - XMLoadFloat3((XMFLOAT3*)&branch->start));
		XMVECTOR vChildDir = XMVector3Rotate(vParentDir, XMQuaternionRotationAxis(vX, (maxAngle * rand()) / RAND_MAX - maxAngleDiv2));
		vChildDir = XMVector3Rotate(vChildDir, XMQuaternionRotationAxis(vZ, (maxAngle * rand()) / RAND_MAX - maxAngleDiv2));
		child->end = branch->end;
		
		child->end.x = branch->end.x + XMVectorGetX(vChildDir) ;
		child->end.y = branch->end.y + XMVectorGetY(vChildDir) ;
		child->end.z = branch->end.z + XMVectorGetZ(vChildDir) ;
		
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

