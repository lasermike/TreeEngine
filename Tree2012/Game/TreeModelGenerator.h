#pragma once
#include "pch.h"
#include "TreeModel.h"

struct Branch;
	
class PlaneModelGenerator : public ModelGenerator, public Model
{
	Model* Create() { return this; }
};

class TreeModelGenerator : public ModelGenerator
{
public:
	virtual TreeModel* Create() = 0;
};

class FixedTreeModelGenerator : public TreeModelGenerator
{
public:
	FixedTreeModelGenerator(unsigned int seed);
	~FixedTreeModelGenerator(void);
	TreeModel* Create();

private:
	TreeModel* CreateTestTree();
	void GenerateChildrenRecursive(Branch* branch, int depth);
	XMVECTOR CalculateQuaternion(FXMVECTOR vDirection);
	TreeModel* _model;

	unsigned int _seed;
};

