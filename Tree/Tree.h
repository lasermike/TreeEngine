#pragma once
#include <directxmath.h>
#include <vector>
#include "TreeGeometry.h"
using namespace DirectX;

struct ID3D11Device;
struct ID3D11DeviceContext;

typedef long HRESULT;

class TreeModelGenerator;
class TreeModel;
struct Branch;


class Tree
{
private:

	TreeGeometry*	_geometry;
	TreeModel*		_model;

public:
	Tree(void);
	~Tree(void);

	void Create(TreeModelGenerator* generator);

	HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, float time);
};

