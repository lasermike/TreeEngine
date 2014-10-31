#pragma once
#include <directxmath.h>
#include <vector>

using namespace DirectX;

typedef long HRESULT;

class TreeModelGenerator;
class TreeModel;
struct Branch;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
struct ID3D11Buffer;

class InputLayoutDesc
{
public:
	static const D3D11_INPUT_ELEMENT_DESC InstancedBasic16[6];
};

class InputLayouts
{
public:
	static void InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen);
	static void DestroyAll();

	static ID3D11InputLayout* InstancedBasic16;
};

struct InstancedData
{
	XMFLOAT4X4 World;
};

class Tree
{
private:
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

	TreeModel*							_model;
	bool								_drawInstanced;

	std::vector<InstancedData>			instancedData;

public:
	Tree(void);
	~Tree(void);

	void Create(TreeModelGenerator* generator);

	HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	HRESULT CleanUpDeviceObjects();
	HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, float time);

private:

	HRESULT RenderDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t);
	HRESULT RenderBranchDirect(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX const* world, Branch const* branch, FXMVECTOR start, float time);

	HRESULT RenderIndirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t);
	HRESULT ComputeBranchIndirect(int& currentBranch, XMMATRIX const* world, Branch const* branch, const FXMVECTOR parentStart, float time);

	HRESULT ComputeTransformations(XMMATRIX* transform, XMVECTOR* vChildStart, float time, Branch const* branch, XMMATRIX const* world, const FXMVECTOR parentStart);
};

