#pragma once
#define NOMINMAX 
#include <wrl/client.h>

#if defined(TREE3D12)
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_4.h>
#else
#include <d3d11_1.h>
#endif

#include <DirectXMath.h>
#include <memory>
#include <agile.h>

// Above this line include platform specific stuff
#define TREENGINE_UNIVERSAL
#undef WIN32

#include <stdafx.h>
