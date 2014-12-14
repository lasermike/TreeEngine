#pragma once
#include "CommonStuff.h"
#include <directxmath.h>
#include "TreeModel.h"

using namespace DirectX;

//interface Model;
//class TreeModel;
struct Branch;
	
interface ModelGenerator
{
	virtual Model* Create() = 0;
};

class TreeModelGenerator : ModelGenerator
{
public:
	TreeModelGenerator(unsigned int seed);
	~TreeModelGenerator(void);
	TreeModel* Create();

private:
	TreeModel* CreateTestTree();
	void GenerateRecursive(Branch* branch, int depth);
	XMVECTOR CalculateQuaternion(FXMVECTOR vDirection);
	TreeModel* model;

	unsigned int _seed;
};

