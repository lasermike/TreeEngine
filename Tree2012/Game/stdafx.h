#pragma once 

//#define NOMINMAX 
//#include <Windows.h>
#include <assert.h>
#include <iostream>
#include <vector>

using namespace DirectX;

#if defined(DEBUG) | defined(_DEBUG)
#ifndef HR
#define HR(x)                                              \
	{                                                          \
		HRESULT hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
		std::cerr << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		return hr; \
		}                                                      \
	}
#endif

#ifndef HRC
#define HRC(x)                                              \
		hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
		std::cerr << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		goto Cleanup; \
		}                                                      
#endif

#else
#ifndef HR
#define HR(x) (x)
#endif
#endif 

template <class T>
void SafeRelease(T* obj)
{
	if (*obj)
	{
		(*obj)->Release();
		(*obj) = nullptr;
	}
}

template <class T>
void SafeDelete(T* obj)
{
	if (*obj)
	{
		delete (*obj);
		(*obj) = nullptr;
	}
}


namespace XboxSampleFramework
{
#if defined(_XBOX_ONE) && defined(_TITLE)
    typedef ID3D11DeviceX           D3DDevice;
    typedef ID3D11DeviceContextX    D3DDeviceContext;
    typedef ID3D11ComputeContextX   D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#elif defined( XSF_USE_DX_11_1 )
    typedef ID3D11Device1           D3DDevice;
    typedef ID3D11DeviceContext1    D3DDeviceContext;
    typedef ID3D11DeviceContext1    D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#else
    typedef ID3D11Device            D3DDevice;
    typedef ID3D11DeviceContext     D3DDeviceContext;
    typedef ID3D11DeviceContext     D3DComputeContext;
    typedef ID3D11RasterizerState   D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC   D3DRasterizerDesc;
    typedef IDXGISwapChain          DXGISwapChain;
#endif

    void DebugPrint( _In_z_ const char* msg, ... );
    void DebugPrint( _In_z_ const wchar_t* msg, ... );
    void PrintNoVarargs( _In_z_ const wchar_t* msg );

	void SetContentFileRoot();

    _Check_return_
    HRESULT LoadBlob( _In_z_ const wchar_t* pFilename, std::vector< BYTE >& data );
    _Check_return_
    HRESULT LoadPixelShader( _In_ ID3D11Device* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11PixelShader** ppPS, _In_opt_ std::vector< BYTE >* pData = nullptr );
    _Check_return_
    HRESULT LoadVertexShader( _In_ ID3D11Device* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11VertexShader** ppVS,
                              _In_opt_ const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc = NULL, _In_opt_ UINT numElements = 0, _COM_Outptr_ ID3D11InputLayout** ppInputLayout = NULL, _In_opt_ std::vector< BYTE >* pData = nullptr );
}

namespace XSF = XboxSampleFramework;

