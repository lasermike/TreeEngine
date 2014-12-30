#pragma once 

#define NOMINMAX 
#include <Windows.h>
#include <d3d11_1.h>
#include <directxmath.h>
#include <assert.h>
#include <iostream>

using namespace DirectX;

#if defined(DEBUG) | defined(_DEBUG)
#ifndef HR
#define HR(x)                                              \
	{                                                          \
		HRESULT hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
		std::cout << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		return hr; \
		}                                                      \
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


