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

struct Branch
{
	int id;
	int depth;
	int parent;

	XMFLOAT4 start;
	float	thickness;
	XMFLOAT4 end;

	XMINT4 children;
	int numChildren;

	Branch()
	{
		memset(this, 0, sizeof Branch);
	}

	int Child(int i) const 
	{ 
		switch (i) {
		case 0:
			return children.x;
		case 1:
			return children.y;
		case 2:
			return children.z;
		case 3:
			return children.w;
		}
		throw;
	}
	void SetChild(int i, int c) 
	{ 
		switch (i) {
		case 0:
			children.x = c;
			break;
		case 1:
			children.y = c;
			break;
		case 2:
			children.z = c;
			break;
		case 3:
			children.w = c;
			break;
		default:
			throw;
		}
	}
};

struct BranchLevelData
{
	int depth;
	int numBranches;
	std::vector<int>* pBranchesInLevel;

	BranchLevelData() : depth(0), numBranches(0) { }
	~BranchLevelData() { SafeDelete(&pBranchesInLevel); }
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

	static const int maxChildBranches = 4;
	static const int maxLevels = 6;
	Branch* trunk;

	TreeData treeData;

};

