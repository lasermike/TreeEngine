#define D3D_DEBUG_INFO
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include "DDSTextureLoader.h"
#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "CommonStuff.h"

#include <iostream>

using namespace DirectX;

#include "Tree.h"

struct SimpleVertex
{
    XMFLOAT3 Pos;
    XMFLOAT2 Tex;
};

struct CBChangesEveryFrame
{
    XMMATRIX mWorld;
	XMFLOAT2 time;
};

#pragma region InputLayouts

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::InstancedBasic16[6] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
};

ID3D11InputLayout* InputLayouts::InstancedBasic16 = 0;

void InputLayouts::InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen)
{
	HRESULT hr = (device->CreateInputLayout(InputLayoutDesc::InstancedBasic16, ARRAYSIZE(InputLayoutDesc::InstancedBasic16), pShaderBytecodeWithInputSignature /*passDesc.pIAInputSignature*/,
		byteCodeLen /*passDesc.IAInputSignatureSize*/, &InstancedBasic16));
	assert(SUCCEEDED(hr));
}

void InputLayouts::DestroyAll()
{
	if (InstancedBasic16 != nullptr)
	{
		InstancedBasic16->Release();
		InstancedBasic16 = nullptr;
	}
}

#pragma endregion



Tree::Tree(void)
{
	_pVertexShader = nullptr;
	_pPixelShader = nullptr;
	_pVertexLayout = nullptr;
	_pVertexBuffer = nullptr;
	_pIndexBuffer = nullptr;
	_pTextureRV = nullptr;
	_pSamplerLinear = nullptr;
	_pCBChangesEveryFrame = nullptr;
	_pCBTree = nullptr;
	_pCBBranches = nullptr;
	_model = nullptr;
	_pInstancedBuffer = nullptr;
	_drawInstanced = true;
}


Tree::~Tree(void)
{
	if (_model)
	{
		delete _model ;
	}
}

void Tree::Create(TreeModelGenerator* generator)
{
	_model = generator->Create();
}

//--------------------------------------------------------------------------------------
// Helper for compiling shaders with D3DCompile
//
// With VS 11, we could load up prebuilt .cso files instead...
//--------------------------------------------------------------------------------------
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
    HRESULT hr = S_OK;

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;

    // Disable optimizations to further improve shader debugging
    dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pErrorBlob = nullptr;
    hr = D3DCompileFromFile( szFileName, nullptr, nullptr, szEntryPoint, szShaderModel, 
        dwShaderFlags, 0, ppBlobOut, &pErrorBlob );
    if( FAILED(hr) )
    {
        if( pErrorBlob )
        {
            OutputDebugStringA( reinterpret_cast<const char*>( pErrorBlob->GetBufferPointer() ) );
            pErrorBlob->Release();
        }
        return hr;
    }
    if( pErrorBlob ) pErrorBlob->Release();

    return S_OK;
}


HRESULT Tree::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	// Compile the vertex shader
    ID3DBlob* pVSBlob = nullptr;
    HRESULT hr = CompileShaderFromFile( L"Tutorial07.fx", "VS", "vs_4_0", &pVSBlob );
    if( FAILED( hr ) )
    {
        MessageBox( nullptr,
                    L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
        return hr;
    }

    // Create the vertex shader
    hr = device->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &_pVertexShader );
    if( FAILED( hr ) )
    {    
        pVSBlob->Release();
        return hr;
    }

    // Define the input layout
	if (!_drawInstanced)
	{
		D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		UINT numElements = ARRAYSIZE(layout);

		// Create the input layout
		hr = device->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(),
			pVSBlob->GetBufferSize(), &_pVertexLayout);
		if (FAILED(hr))
			return hr;

		// Set the input layout
		pImmediateContext->IASetInputLayout(_pVertexLayout);
	}
	else
	{
		// Create Instanced draw data layout
		InputLayouts::InitAll(device, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize());

		pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
	}
	pVSBlob->Release();



    // Compile the pixel shader
    ID3DBlob* pPSBlob = nullptr;
    hr = CompileShaderFromFile( L"Tutorial07.fx", "PS", "ps_4_0", &pPSBlob );
    if( FAILED( hr ) )
    {
        MessageBox( nullptr,
                    L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
        return hr;
    }

    // Create the pixel shader
    hr = device->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &_pPixelShader );
    pPSBlob->Release();
    if( FAILED( hr ) )
        return hr;

    // Create vertex buffer
    SimpleVertex vertices[] =
    {
        { XMFLOAT3( -0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.0f ) },
        { XMFLOAT3( 0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.0f ) },
        { XMFLOAT3( 0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.5f ) },
        { XMFLOAT3( -0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.5f ) },

        { XMFLOAT3( -0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.0f ) },
        { XMFLOAT3( 0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.0f ) },
        { XMFLOAT3( 0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.5f ) },
        { XMFLOAT3( -0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.5f ) },

        { XMFLOAT3( -0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.5f ) },
        { XMFLOAT3( -0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.5f ) },
        { XMFLOAT3( -0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.0f ) },
        { XMFLOAT3( -0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.0f ) },

        { XMFLOAT3( 0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.5f ) },
        { XMFLOAT3( 0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.5f ) },
        { XMFLOAT3( 0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.0f ) },
        { XMFLOAT3( 0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.0f ) },

        { XMFLOAT3( -0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.5f ) },
        { XMFLOAT3( 0.5f, -0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.5f ) },
        { XMFLOAT3( 0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.5f, 0.0f ) },
        { XMFLOAT3( -0.5f, 0.5f, -0.5f ), XMFLOAT2( 0.0f, 0.0f ) },

        { XMFLOAT3( -0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.5f ) },
        { XMFLOAT3( 0.5f, -0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.5f ) },
        { XMFLOAT3( 0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.0f, 0.0f ) },
        { XMFLOAT3( -0.5f, 0.5f, 0.5f ), XMFLOAT2( 0.5f, 0.0f ) },
    };

    D3D11_BUFFER_DESC bd;
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( SimpleVertex ) * 24;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    ZeroMemory( &InitData, sizeof(InitData) );
    InitData.pSysMem = vertices;
    hr = device->CreateBuffer( &bd, &InitData, &_pVertexBuffer );
    if( FAILED( hr ) )
        return hr;

    // Set vertex buffer
	if (!_drawInstanced)
	{
		UINT stride = sizeof( SimpleVertex );
		UINT offset = 0;
		pImmediateContext->IASetVertexBuffers( 0, 1, &_pVertexBuffer, &stride, &offset );
	}
	else
	{
		// Create instanced buffer
		instancedData.resize(_model->treeData.numBranches);
		D3D11_BUFFER_DESC vbd;
		vbd.Usage = D3D11_USAGE_DYNAMIC;
		vbd.ByteWidth = sizeof(InstancedData) * instancedData.size();
		vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		vbd.MiscFlags = 0;
		vbd.StructureByteStride = 0;

		hr = device->CreateBuffer(&vbd, 0, &_pInstancedBuffer);
		if (FAILED(hr))
			return hr;

		UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
		UINT offset[2] = { 0, 0 };
		ID3D11Buffer* vbs[2] = { _pVertexBuffer, _pInstancedBuffer };

		pImmediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);
	}

    // Create index buffer
    // Create vertex buffer
    WORD indices[] =
    {
        3,1,0,
        2,1,3,

        6,4,5,
        7,4,6,

        11,9,8,
        10,9,11,

        14,12,13,
        15,12,14,

        19,17,16,
        18,17,19,

        22,20,21,
        23,20,22
    };

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( WORD ) * 36;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    InitData.pSysMem = indices;
    hr = device->CreateBuffer( &bd, &InitData, &_pIndexBuffer );
    if( FAILED( hr ) )
        return hr;

    // Set index buffer
    pImmediateContext->IASetIndexBuffer( _pIndexBuffer, DXGI_FORMAT_R16_UINT, 0 );

    // Set primitive topology
    pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // Load the Texture
    hr = CreateDDSTextureFromFile( device, L"bark2.dds", nullptr, &_pTextureRV );
    if( FAILED( hr ) )
        return hr;

    // Create the sample state
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory( &sampDesc, sizeof(sampDesc) );
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState( &sampDesc, &_pSamplerLinear );
    if( FAILED( hr ) )
        return hr;

	// Create constants for per frame 
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;   
    bd.ByteWidth = sizeof(CBChangesEveryFrame);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    hr = device->CreateBuffer( &bd, nullptr, &_pCBChangesEveryFrame );
    if( FAILED( hr ) )
        return hr;

	// Create constants for tree
/*    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;   
    bd.ByteWidth = sizeof(TreeData);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    hr = device->CreateBuffer( &bd, nullptr, &_pCBTree );
    if( FAILED( hr ) )
        return hr;

	pImmediateContext->VSSetConstantBuffers( 2, 1, &_pCBTree);
    pImmediateContext->PSSetConstantBuffers( 2, 1, &_pCBTree);
	pImmediateContext->UpdateSubresource( _pCBTree, 0, nullptr, &_model->treeData, 0, 0 );


	// Create constants for branches
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;   
	bd.ByteWidth = sizeof(Branch) * _model->treeData.numBranches;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    hr = device->CreateBuffer( &bd, nullptr, &_pCBBranches );
    if( FAILED( hr ) )
        return hr;

	pImmediateContext->VSSetConstantBuffers( 2, 1, &_pCBBranches);
    pImmediateContext->PSSetConstantBuffers( 2, 1, &_pCBBranches);
	pImmediateContext->UpdateSubresource( _pCBBranches, 0, nullptr, &_model->pBranches, 0, 0 );
	*/
	return S_OK;
}

HRESULT Tree::Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t)
{
    pImmediateContext->VSSetShader( _pVertexShader, nullptr, 0 );
    pImmediateContext->PSSetShader( _pPixelShader, nullptr, 0 );
    pImmediateContext->PSSetShaderResources( 0, 1, &_pTextureRV );
    pImmediateContext->PSSetSamplers( 0, 1, &_pSamplerLinear );

	HRESULT hr = S_OK;

	if (!_drawInstanced)
	{
		hr = RenderDirect(pImmediateContext, world, t);
	}
	else
	{
		hr = RenderIndirect(pImmediateContext, world, t);

	}
	return hr;
}

HRESULT Tree::RenderIndirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t)
{
	int currentBranch = 0;
	XMVECTOR vChildStart;
	CBChangesEveryFrame cb;
	ComputeBranchIndirect(currentBranch, world, _model->trunk, XMVectorSet(0, 0, 0, 0), t);

	D3D11_MAPPED_SUBRESOURCE mappedData;
	HR(pImmediateContext->Map(_pInstancedBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

	for (int i = 0; i < currentBranch; i++)
	{
		dataView[i] = instancedData[i];
	}

	pImmediateContext->Unmap(_pInstancedBuffer, 0);

	//pImmediateContext->VSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	//pImmediateContext->PSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	//pImmediateContext->UpdateSubresource(_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	pImmediateContext->DrawIndexedInstanced(36, currentBranch, 0, 0,0);

	return S_OK;
}

HRESULT Tree::ComputeBranchIndirect(int& currentBranch, XMMATRIX const* world, Branch const* branch, const FXMVECTOR parentStart, float time)
{
	if (time < branch->depth)
		return S_OK;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	XMMATRIX mat;
	ComputeTransformations(&mat, &vChildStart, time, branch, world, parentStart);
	XMStoreFloat4x4(&instancedData.at(currentBranch).World, mat);
	currentBranch++;

	// Render child branches
	const int maxChildren = 4;
	for (int c = 0; c < maxChildren; c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_model->pBranches[branch->Child(c)];
			ComputeBranchIndirect(currentBranch, world, child, vChildStart, time);
		}
	}

	return S_OK;
}

HRESULT Tree::RenderDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float t)
{
	return RenderBranchDirect(pImmediateContext, world, _model->trunk, XMVectorSet(0, 0, 0, 0), t);
}

HRESULT Tree::ComputeTransformations(XMMATRIX* transform, XMVECTOR* vChildStart, float time, Branch const* branch, XMMATRIX const* world, FXMVECTOR parentStart)
{
	float animScaleFactor = 1.0f;
	if (time - 5 < branch->depth)
	{
		animScaleFactor = (time - branch->depth) / 5;
	}

	XMVECTOR vStart = parentStart;
	XMVECTOR vEnd = XMLoadFloat3((XMFLOAT3*)&(branch->end));

	// Scale branch
	XMVECTOR vMag = XMVector3Length(vEnd - vStart);
	float magY = XMVectorGetX(vMag);
	float magXZ = branch->thickness;
	magY *= animScaleFactor;
	magXZ *= animScaleFactor;
	XMVECTOR vScale = XMVectorSet(magXZ, magY, magXZ, 0);

	// Child start pos
	XMVECTOR vMagY = XMVectorSet(animScaleFactor, animScaleFactor, animScaleFactor, 1);
	*vChildStart = (vEnd - vStart) * vMagY + vStart;

	// Determine rotation
	XMMATRIX mRot;
	XMVECTOR vUp = XMVectorSet(0, 1, 0, 0);
	XMVECTOR vDiff = vEnd - vStart;
	XMVECTOR vCross = XMVector3Cross(vUp, XMVector3Normalize(vDiff));
	XMVECTOR vCrossLenSq = XMVector3LengthSq(vCross);
	XMVECTOR vQuat;
	float crossLenSq;
	XMStoreFloat(&crossLenSq, vCrossLenSq);
	if (crossLenSq > 0.01f) // Need better value for epsilon here
	{
		XMVECTOR vDot = XMVector3Dot(vUp, XMVector3Normalize(vDiff));
		float angle;
		XMStoreFloat(&angle, vDot);
		angle = acos(angle);
		vQuat = XMQuaternionRotationAxis(vCross, angle);
	}
	else
	{
		vQuat = XMQuaternionRotationAxis(vUp, 0);
	}

	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0);
	const XMVECTOR vScaleCenter = XMVectorSet(0, -0.5, 0, 0);
	*transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);
	*transform = XMMatrixTranspose(*transform * *world);

	return S_OK;
}

HRESULT Tree::RenderBranchDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX const* world, Branch const* branch, FXMVECTOR parentStart, float time)
{
	if (time < branch->depth)
		return S_OK;

    // Update variables that change once per frame
	XMVECTOR vChildStart;
	CBChangesEveryFrame cb;
	ComputeTransformations(&cb.mWorld, &vChildStart, time, branch, world, parentStart);
	cb.time.x = time;

	pImmediateContext->VSSetConstantBuffers( 2, 1, &_pCBChangesEveryFrame );
    pImmediateContext->PSSetConstantBuffers( 2, 1, &_pCBChangesEveryFrame );
    pImmediateContext->UpdateSubresource( _pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0 );

    pImmediateContext->DrawIndexed( 36, 0, 0 );

	// Render child branches
	const int maxChildren = 4;
	for (int c = 0; c < maxChildren; c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_model->pBranches[branch->Child(c)];
			RenderBranchDirect(pImmediateContext, world, child, vChildStart, time);
		}
	}

	return S_OK;
}



HRESULT Tree::CleanUpDeviceObjects()
{
    if( _pVertexBuffer ) _pVertexBuffer->Release();
    if( _pIndexBuffer ) _pIndexBuffer->Release();
    if( _pVertexLayout ) _pVertexLayout->Release();
    if( _pVertexShader ) _pVertexShader->Release();
    if( _pPixelShader ) _pPixelShader->Release();
    if( _pSamplerLinear ) _pSamplerLinear->Release();
    if( _pTextureRV ) _pTextureRV->Release();
    if( _pCBChangesEveryFrame ) _pCBChangesEveryFrame->Release();
    if( _pCBTree ) _pCBTree->Release();
	if (_pInstancedBuffer) _pInstancedBuffer->Release();
	if (_drawInstanced)
	{
		InputLayouts::DestroyAll();
	}

	return S_OK;
}
