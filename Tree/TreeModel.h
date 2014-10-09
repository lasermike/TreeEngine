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

	XMFLOAT3 relStart;
	XMFLOAT3 relEnd;


	std::vector<Branch*> branches;
};

class TreeModel
{
public:
	TreeModel(void);
	~TreeModel(void);

	Branch* trunk;
};

