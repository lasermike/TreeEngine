#pragma once
#include <vector>
#include <directxmath.h>

using namespace DirectX;

const int maxBranches = 2000; //3^6 + 1 


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

	XMFLOAT4 start;
	float	thickness;
	XMFLOAT4 end;

	XMINT4 children;

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

struct TreeData
{
	int numBranches;

};

class TreeModel
{
public:
	TreeModel(void);
	~TreeModel(void);

	//const int maxBranches;
	Branch* trunk;

	TreeData treeData;
	Branch* pBranches;  

	//std::vector<Branch*> branchMap;
};

