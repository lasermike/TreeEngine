#pragma once
#include "pch.h"
#include <directxmath.h>
#include "TreeModel.h"

using namespace DirectX;

//interface Model;
//class TreeModel;
struct Branch;
	
class PlaneModelGenerator : public ModelGenerator, public Model
{
	Model* Create() { return this; }
};

class TreeModelGenerator : ModelGenerator
{
public:
	TreeModelGenerator(unsigned int seed);
	~TreeModelGenerator(void);
	TreeModel* Create();

private:
	TreeModel* CreateTestTree();
	void GenerateChildrenRecursive(Branch* branch, int depth);
	XMVECTOR CalculateQuaternion(FXMVECTOR vDirection);
	TreeModel* _model;

	unsigned int _seed;
};

