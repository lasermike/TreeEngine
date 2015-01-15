#include "pch.h"
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
	XMVECTOR vEnd = XMVectorSet(0, 1.3f, 0,0);

	// Create trunk
	int id = model->treeData.numBranches++;
	model->trunk = &model->pBranches[id]; 
	model->trunk->id = id;

	XMStoreFloat4(&model->trunk->start, vStart);
	XMStoreFloat4(&model->trunk->end, vEnd);
	model->trunk->thickness = .3f;
	model->trunk->depth = 0;

	GenerateChildrenRecursive(model->trunk, 1);
	return model;
}

void TreeModelGenerator::GenerateChildrenRecursive(Branch* parentBranch, int depth)
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
		int id = model->treeData.numBranches++;
		Branch* child = &model->pBranches[id];
		child->id = id; 
		parentBranch->SetChild(i, child->id);
		assert(&model->pBranches[child->id] == child);  // Ensure our look up is correct 

		child->start = parentBranch->end;
		child->depth = depth;

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
			float randLen = 0.2f + 0.3f * ((float)rand()) / RAND_MAX;
			vChildDir = XMVectorScale(vChildDir, randLen);
		}
		else if (depth > 0)
		{
			float randLen = 0.5f + 0.3f * ((float)rand()) / RAND_MAX;
			vChildDir = XMVectorScale(vChildDir, randLen);
		}

		child->end.x = parentBranch->end.x + XMVectorGetX(vChildDir) ;
		child->end.y = parentBranch->end.y + XMVectorGetY(vChildDir) ;
		child->end.z = parentBranch->end.z + XMVectorGetZ(vChildDir) ;

		GenerateChildrenRecursive(child, depth + 1);
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

