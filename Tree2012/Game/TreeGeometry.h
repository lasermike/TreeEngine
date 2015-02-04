#pragma once
#include "pch.h"
#include <vector>
#include "Geometry.h"

using namespace DirectX;

struct Branch;
class TreeModel;

class TreeGeometry : public Geometry
{
	ID3D11ShaderResourceView*   _pTextureRV;
	ID3D11SamplerState*         _pSamplerLinear;

	ID3D11Buffer*               _pCBTree;
	ID3D11Buffer*				_pCBBranches;

	TreeModel*					_model; //Weak reference

	Material					_trunkMaterial;
	Material					_shadowMaterial;
	bool						_drawShadow;

public:
	TreeGeometry(TreeModel* model);
	~TreeGeometry();

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();

	virtual HRESULT DrawInstanced(ID3D11DeviceContext* pImmediateContext, RenderData* pRenderData, GeometryBufferData* pGeometyData, int startInstance, int numInstances);

};

