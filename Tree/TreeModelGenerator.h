#pragma once

class TreeModel;
struct Branch;

class TreeModelGenerator
{
public:
	TreeModelGenerator(void);
	~TreeModelGenerator(void);
	TreeModel* Create();

private:
	TreeModel* CreateTestTree();
	void GenerateRecursive(Branch* branch, int depth);

};

