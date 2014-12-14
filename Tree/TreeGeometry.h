#pragma once
#include <directxmath.h>
#include "treegeometry.h"
#include "GeometryGenerator.h"
#include <vector>
#include "Materials.h"
using namespace DirectX;

typedef long HRESULT;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
struct ID3D11Buffer;
struct Branch;
class TreeModel;

struct InstancedData
{
	XMFLOAT4X4 World;
};

class TreeGeometry
{
	ID3D11VertexShader*                 _pVertexShader;
	ID3D11PixelShader*                  _pPixelShader;
	ID3D11InputLayout*                  _pVertexLayout;
	ID3D11Buffer*                       _pVertexBuffer;
	ID3D11Buffer*                       _pIndexBuffer;
	ID3D11ShaderResourceView*           _pTextureRV;
	ID3D11SamplerState*                 _pSamplerLinear;
	ID3D11Buffer*                       _pCBChangesEveryFrame;
	ID3D11Buffer*                       _pCBTree;
	ID3D11Buffer*						_pCBBranches;
	ID3D11Buffer*						_pInstancedBuffer;

	bool								_drawInstanced;
	std::vector<InstancedData>			_logInstanceData;
	std::vector<InstancedData>			_twigInstanceData;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

	TreeModel*							_model; //Weak reference

	Material							_trunkMaterial;
	DirectionalLight					_light;  // Doesn't belong here, will move later

public:
	TreeGeometry(TreeModel* model);
	~TreeGeometry();

	HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time);

private:

	HRESULT RenderDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t);
	HRESULT RenderBranchDirect(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX const* world, Branch const* branch, FXMVECTOR start, float time);

	HRESULT RenderIndirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t);
	HRESULT ComputeBranchIndirect(int& currentBranch, XMMATRIX const* world, Branch const* branch, const FXMVECTOR parentStart, float time);

	HRESULT ComputeTransformations(XMMATRIX* transform, XMVECTOR* vChildStart, float time, Branch const* branch, XMMATRIX const* world, const FXMVECTOR parentStart);

};

