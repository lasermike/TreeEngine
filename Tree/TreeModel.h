#pragma once
#include <vector>
#include <directxmath.h>

using namespace DirectX;

struct Branch
{
	XMFLOAT3 start;
	XMFLOAT3 end;
	int depth;
	float	thickness;

	std::vector<Branch*> branches;
};

class TreeModel
{
public:
	TreeModel(void);
	~TreeModel(void);

	Branch* trunk;
};

