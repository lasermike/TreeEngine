#pragma once
#include "CommonStuff.h"
#include <vector>
#include "Geometry.h"

using namespace DirectX;

struct Branch;
class TreeModel;

class TreeGeometry : public Geometry
{
	ID3D11ShaderResourceView*           _pTextureRV;
	ID3D11SamplerState*                 _pSamplerLinear;

	ID3D11Buffer*                       _pCBTree;
	ID3D11Buffer*						_pCBBranches;

	//bool								_drawInstanced;

	TreeModel*							_model; //Weak reference

public:
	TreeGeometry(TreeModel* model);
	~TreeGeometry();

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();
	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float time);

	virtual HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometyData, int startInstance, int numInstances);

private:

	//HRESULT RenderDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t);
	HRESULT RenderBranchDirect(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX const* world, Branch const* branch, FXMVECTOR start, float time);


};

