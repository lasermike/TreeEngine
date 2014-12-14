#pragma once
#include <directxmath.h>
#include <vector>
#include "TreeGeometry.h"
using namespace DirectX;

struct ID3D11Device;
struct ID3D11DeviceContext;

typedef long HRESULT;

interface ModelGenerator;
class TreeModelGenerator;
class TreeModel;
struct Branch;

interface WorldObject
{
	virtual void Create(ModelGenerator* generator) = 0;

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext) = 0;
	virtual HRESULT CleanUpDeviceObjects() = 0;
	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time) = 0;
};

class Tree : public WorldObject
{
private:

	TreeGeometry*	_geometry;
	TreeModel*		_model;

public:
	Tree(void);
	~Tree(void);

	void Create(ModelGenerator* generator) { return Create((TreeModelGenerator*)generator); }
	void Create(TreeModelGenerator* generator);

	HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float time);
};

